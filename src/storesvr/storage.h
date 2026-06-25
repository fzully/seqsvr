#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include "seqsvr.pb.h"

namespace rocksdb { class DB; }

namespace seqsvr {

class Storage {
public:
    Storage() = default;
    ~Storage();

    bool Open(const std::string& db_path);

    uint64_t GetMaxSeq(uint32_t section_id);
    bool SetMaxSeq(uint32_t section_id, uint64_t max_seq);

    std::optional<LeaseRecord> GetLease(uint32_t section_id);
    bool SetLease(uint32_t section_id, const LeaseRecord& rec);
    bool DeleteLease(uint32_t section_id);

    std::optional<RouteTable> GetRouteTable();
    bool SetRouteTable(const RouteTable& rt);

    int CleanExpiredLeases(int64_t now_ms);

private:
    rocksdb::DB* db_{nullptr};

    static std::string MaxSeqKey(uint32_t section_id);
    static std::string LeaseKey(uint32_t section_id);
    static constexpr const char* kRouteTableKey = "route_table";
};

}  // namespace seqsvr
