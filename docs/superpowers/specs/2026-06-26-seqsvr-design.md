# SeqSvr 设计文档

**日期**：2026-06-26  
**状态**：已审批  
**目标规模**：千万到亿级用户，QPS 十万级

---

## 1. 背景与目标

IM 后台系统需要多类单调递增序列号：

- **per-user seqno**：每个用户独立的消息序列号
- **per-session seqno**：每个会话（私聊/群组）独立的序列号
- **全局 seqno**：有限数量的全局唯一递增序列（如全局消息流水号）

**核心约束**：
- 严格单调递增，允许跳号（重启后从更高位置继续）
- 不允许回退
- P99 延迟 < 3ms，平均延迟 < 1ms
- 可用性 > 99.99%
- 支持水平扩展

---

## 2. 整体架构

### 2.1 分层结构

```
┌──────────────────────────────────────────┐
│              Client SDK                  │
│  • 缓存路由表                             │
│  • 自动重试 + 故障转移                    │
└──────────────┬───────────────────────────┘
               │ brpc（baidu_std 协议 + protobuf）
┌──────────────▼───────────────────────────┐
│           AllocSvr 集群                  │
│  • 按 Section 持有号段，纯内存操作         │
│  • 维护租约，保证 Section 独占            │
│  • 路由表随响应下发                       │
│  节点数：10-20 台（可水平扩展）            │
└──────────────┬───────────────────────────┘
               │ brpc 内部 RPC（仅在 cur_seq 耗尽时）
┌──────────────▼───────────────────────────┐
│           StoreSvr 集群                  │
│  • 持久化每个 Section 的 max_seq          │
│  • 管理 AllocSvr 的租约颁发与续期         │
│  • 存储全局路由表                         │
│  • NRW 多副本（N=3, W=2, R=2）           │
│  节点数：3-5 台                           │
└──────────────────────────────────────────┘
```

### 2.2 核心概念

| 概念 | 定义 |
|------|------|
| **Section** | uid 空间的最小分配单元，每 Section 覆盖 10 万个 uid |
| **cur_seq** | AllocSvr 内存中的当前分配指针，每次分配后递增 |
| **max_seq** | 已持久化到 StoreSvr 的分配上限 |
| **step** | 每次向 StoreSvr 申请的步长，默认 10000（全局 seqno 为 1000） |
| **租约（Lease）** | AllocSvr 持有某 Section 的授权凭证，有效期 30s，每 10s 续期一次 |
| **路由表** | section_id → AllocSvr 地址的映射，带版本号，随响应包下发 |

### 2.3 三类 seqno 处理

| 类型 | key 规则 | Section 分配 | 步长 |
|------|---------|-------------|------|
| per-user | `uid / 100000` | 按 uid 分桶 | 10000 |
| per-session | `hash(session_id) / 100000` | 按 session_id hash 分桶 | 10000 |
| 全局 seqno | 固定 Section（可枚举配置） | 单独 Section，可独立部署 | 1000 |

---

## 3. AllocSvr 设计

### 3.1 内存数据结构

```cpp
struct SectionState {
    uint32_t section_id;
    uint64_t cur_seq;       // 当前已分配的最大值（内存）
    uint64_t max_seq;       // 已持久化的上限（来自 StoreSvr）
    uint64_t step;          // 每次向 StoreSvr 申请的步长
    int64_t  lease_expire;  // 租约到期时间戳（ms）
    std::mutex mu;          // 每个 Section 独立锁
};

std::unordered_map<uint32_t, SectionState> sections_;
```

每台 AllocSvr 可持有数百个 Section，所有操作在内存中完成，只有 `cur_seq > max_seq` 时才产生一次 StoreSvr 写入。

### 3.2 分配流程

```
GetSeq(uid/session_id/global_name):

1. 计算 section_id
2. 查路由表，确认本节点是该 Section 的租约持有者
3. 检查 lease_expire > now，否则返回 LEASE_EXPIRED
4. 加 section 锁：
   cur_seq++
   if cur_seq > max_seq:
       向 StoreSvr 申请: new_max = max_seq + step
       NRW 写入成功后: max_seq = new_max
       失败: cur_seq--，返回 STORE_ERROR
5. 释放锁
6. 返回 cur_seq（附带路由表版本）
```

**单调性保证**：步骤 4 中写 StoreSvr 失败时回退 cur_seq，客户端重试安全。

### 3.3 租约机制

```
生命周期：

启动
  └─► 向 StoreSvr 申请租约
      ├─ 成功：获得 sections 列表 + max_seq_map + expire_time
      └─ 失败：等待并重试（若上一持有者租约未过期，需等其过期）

服务中
  └─► 每 10s 续约一次 (RenewLease)
      ├─ 成功：expire_time 延长 30s
      └─ 失败：继续重试，直到 expire_time 到达

租约到期
  └─► 立即停止分配该 Section
      响应返回 LEASE_EXPIRED（客户端等待后重试，由新持有者处理）

关闭/重启
  └─► 主动释放租约（ReleaseLease），加速新节点接管
```

**安全保证**：同一时刻，一个 Section 只有一个有效租约持有者，由 StoreSvr 的 NRW 写入仲裁保证。

### 3.4 路由表下发

接口以 protobuf 定义，通过 brpc 暴露服务（baidu_std 协议，支持连接复用和流量控制）：

```protobuf
// seqsvr.proto
service AllocService {
    rpc GetSeq(GetSeqRequest) returns (GetSeqResponse);
}

message GetSeqRequest {
    uint64 uid        = 1;  // per-user / per-session 场景
    string global_key = 2;  // 全局 seqno 场景，与 uid 二选一
}

message GetSeqResponse {
    uint64     seqno     = 1;
    int32      route_ver = 2;  // 客户端当前路由表版本
    RouteTable route     = 3;  // 仅在服务端版本更新时附带
}

message RouteTable {
    int32             version = 1;
    repeated RouteEntry entries = 2;
}

message RouteEntry {
    uint32 section_id = 1;
    string allocsvr   = 2;  // "ip:port"
}
```

brpc 服务端通过 `brpc::ServerOptions` 配置线程池大小和最大并发数，Client SDK 使用 `brpc::Channel` 连接 AllocSvr，连接池和超时由 brpc 框架管理。

---

## 4. StoreSvr 设计

### 4.1 存储数据模型

```
max_seq 表:
  section_id (uint32) → max_seq (uint64)
  总数据量：~10000 Section × 8B = ~80KB

租约表:
  section_id (uint32) → {holder_addr, expire_time_ms}

路由表:
  version (int32) + section_id → allocsvr_addr
```

底层存储：**RocksDB**（嵌入式，低延迟，成熟稳定）。

### 4.2 NRW 多副本策略

3 台 StoreSvr，写操作需 2 台确认（W=2），读操作需 2 台一致（R=2）：

```
写 max_seq:
  并发发送给 3 台 StoreSvr
  ≥ 2 台 ACK → 返回成功
  < 2 台 ACK → 返回失败（AllocSvr 重试或回退）

读 max_seq（仅在 AllocSvr 启动时）:
  从 3 台读取，取 2 台一致的值
  若 3 台值不同：取最大值（保证不回退）
```

### 4.3 租约仲裁

```
RequestLease(section_id, addr):
  1. 检查当前持有者：
     ├─ 无持有者或已过期 → 颁发新租约（NRW 写入 W=2）
     └─ 未过期且持有者 ≠ 请求方 → 返回 CONFLICT（附当前持有者地址）

RenewLease(section_id, addr):
  1. 验证请求方是当前持有者
  2. 原子更新 expire_time = now + 30s（NRW 写入 W=2）

ReleaseLease(section_id, addr):
  1. 验证请求方是当前持有者
  2. 清除租约记录（NRW 写入 W=2）
```

### 4.4 重启恢复

```
StoreSvr 重启:
  1. 读本地 RocksDB，加载所有 Section 的 max_seq
  2. 清理 expire_time < now 的租约记录
  3. 就绪，接受请求

AllocSvr 重启:
  1. 向 StoreSvr 申请租约（若上次租约未过期，最多等待 30s）
  2. 读取所负责 Section 的 max_seq
  3. 设置 cur_seq = max_seq（跳过重启前未持久化的号段）
  4. 就绪，接受 Client 请求
```

---

## 5. Client SDK 设计

### 5.1 接口

```cpp
class SeqClient {
public:
    // 初始化，传入任意一台 AllocSvr 地址作为种子节点
    explicit SeqClient(const std::string& bootstrap_addr);

    StatusOr<uint64_t> GetSeq(uint64_t uid);
    StatusOr<uint64_t> GetSessionSeq(uint64_t session_id);
    StatusOr<uint64_t> GetGlobalSeq(const std::string& name);

private:
    RouteTable    route_table_;
    brpc::Channel channels_[MAX_ALLOCSVR]; // brpc Channel 连接池
    uint64_t ToSectionId(uint64_t uid);
};
```

### 5.2 路由决策（纯本地，无 proxy）

```
GetSeq(uid):
  section_id = uid / 100000
  addr = route_table_.Lookup(section_id)
  resp = Send(addr, GetSeqRequest{uid})
  if resp.route_ver > local_ver:
      route_table_.Update(resp.route)
  return resp.seqno
```

### 5.3 错误处理

| 错误类型 | 触发场景 | 处理策略 |
|---------|---------|---------|
| 网络超时 | AllocSvr 响应慢 | 立即重试另一副本（路由表中的备选地址） |
| REDIRECT | 路由表过期，Section 已迁移 | 响应中含新地址，直接重定向，更新路由表 |
| LEASE_EXPIRED | 租约切换中 | 等待 500ms 后重试（新持有者接管中） |
| STORE_ERROR | StoreSvr 暂时不可写 | 指数退避重试，最多 3 次 |
| 全部副本不可达 | 网络分区 | 返回错误给业务层，上报告警 |

**重试策略**：最多 3 次，退避间隔 50ms / 100ms / 200ms，总超时 500ms。

---

## 6. 关键设计决策

### 6.1 为何选择 per-Section 而非 per-uid

per-uid 会导致 StoreSvr 存储数量与用户数线性增长（10 亿用户 = 10 亿行），per-Section（每 10 万 uid 一个 Section）将数量压缩到 10000 行，且 Section 粒度恰好适合做动态迁移和负载均衡。

### 6.2 为何不用 Raft

Raft 的写路径需要多数节点日志 commit，P99 延迟通常 5-20ms，在 QPS 十万级下成为瓶颈。租约 + NRW 方案将强一致性约束仅施加在"租约颁发"这一低频操作上，高频的 seqno 分配则是纯内存操作，两者不在同一路径上。

### 6.3 为何选择 brpc 而非 gRPC

gRPC 基于 HTTP/2，帧头开销和流量控制机制在 QPS 十万级场景下会带来额外延迟（P99 通常多 0.5-2ms）。brpc 使用 baidu_std 协议（自定义二进制帧，基于 TCP），具备更低的序列化开销、更优的连接复用模型，同等场景下吞吐约为 gRPC 的 2-5 倍。brpc 同时内置 bvar 监控、内置 Web 管理界面和熔断，无需额外组件。

### 6.4 全局 seqno 的热点处理

全局 seqno 的 Section 是单一热点 key，通过两个手段缓解：
1. 步长缩小为 1000，减少 StoreSvr 写入延迟的影响面
2. 该 Section 可独立部署在高配 AllocSvr 节点，与其他 Section 物理隔离

---

## 7. 测试策略

### 7.1 单元测试

- `SectionState`：验证 cur_seq 单调递增、max_seq 触发持久化边界
- 租约管理器：租约过期、续约失败、并发申请冲突
- 路由表：版本更新、并发读写安全
- NRW 写入：模拟 1/3 台失败仍成功，2/3 台失败返回错误

### 7.2 集成测试

- 正常放号链路：1 AllocSvr + 3 StoreSvr 完整链路
- AllocSvr 重启：验证跳号但绝不回退
- StoreSvr 1 台宕机：验证 NRW W=2 仍可正常写入
- Client SDK 重试：模拟 REDIRECT / LEASE_EXPIRED / STORE_ERROR

### 7.3 混沌测试

- 随机 kill AllocSvr：租约过期 → 新节点接管，全程 seqno 不回退
- 随机延迟 StoreSvr 响应：验证 AllocSvr 超时处理不损坏数据
- 并发压测：10 万 QPS，验证 P99 < 3ms

---

## 8. 部署拓扑

```
生产环境（最小部署）：

AllocSvr 集群（10 台，4 核 8G）
  • 每台负责约 1000 个 Section
  • 按 uid 范围分配，支持动态迁移

StoreSvr 集群（3 台，4 核 8G，SSD）
  • NRW N=3, W=2, R=2
  • 数据量极小（< 1MB），SSD 足够

Client SDK
  • 随业务服务部署，无额外节点
  • 通过 bootstrap_addr 获取初始路由表
```

---

## 9. 后续扩展方向

- **多 IDC 容灾**：StoreSvr 跨 IDC 部署，AllocSvr 本 IDC 优先，跨 IDC 备用
- **动态 Section 迁移**：负载均衡工具自动检测热 Section 并迁移
- **监控指标**：per-Section QPS、租约续约失败率、StoreSvr 写延迟、跳号量
