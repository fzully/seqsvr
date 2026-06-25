#pragma once
#include <brpc/channel.h>
#include <memory>
#include <unordered_map>
#include <string>
#include "seqsvr.pb.h"
#include "common/types.h"
#include "common/route_table.h"

namespace seqsvr {

class SeqClient {
public:
    static constexpr int kMaxRetry     = 3;
    static constexpr int kTimeoutMs    = 500;
    static constexpr int kInitBackoff  = 50;

    explicit SeqClient(const std::string& bootstrap_addr);

    StatusOr<uint64_t> GetSeq(uint64_t uid);
    StatusOr<uint64_t> GetSessionSeq(uint64_t session_id);
    StatusOr<uint64_t> GetGlobalSeq(const std::string& name);

private:
    StatusOr<uint64_t> DoGetSeq(const GetSeqRequest& req);
    brpc::Channel* GetOrCreateChannel(const std::string& addr);

    RouteTableCache route_cache_;
    std::unordered_map<std::string, std::unique_ptr<brpc::Channel>> channels_;
    std::mutex channels_mu_;
};

}  // namespace seqsvr
