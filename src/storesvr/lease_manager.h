#pragma once
#include <mutex>
#include <string>
#include "seqsvr.pb.h"
#include "storage.h"

namespace seqsvr {

class LeaseManager {
public:
    static constexpr int64_t kLeaseDurationMs = 30000;

    explicit LeaseManager(Storage* storage) : storage_(storage) {}

    RequestLeaseResponse RequestLease(uint32_t section_id,
                                      const std::string& addr,
                                      int64_t now_ms);

    RenewLeaseResponse RenewLease(uint32_t section_id,
                                  const std::string& addr,
                                  int64_t now_ms);

    ReleaseLeaseResponse ReleaseLease(uint32_t section_id,
                                      const std::string& addr);

private:
    Storage* storage_;
    std::mutex mu_;
};

}  // namespace seqsvr
