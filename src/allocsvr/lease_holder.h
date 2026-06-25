#pragma once
#include <atomic>
#include <thread>
#include <unordered_map>
#include <vector>
#include "section_state.h"
#include "nrw_coordinator.h"

namespace seqsvr {

class LeaseHolder {
public:
    static constexpr int64_t kRenewalIntervalMs = 10000;

    LeaseHolder(NRWCoordinator* nrw,
                std::unordered_map<uint32_t, SectionState>* sections)
        : nrw_(nrw), sections_(sections) {}

    ~LeaseHolder() { Stop(); }

    // Acquires leases for the given sections. my_addr is "ip:port" of this AllocSvr.
    // Blocks until all leases are acquired or timeout (max_wait_ms).
    bool Start(const std::vector<uint32_t>& section_ids,
               const std::string& my_addr,
               int64_t max_wait_ms = 35000);

    void Stop();

private:
    void RenewalLoop();
    bool AcquireLease(uint32_t section_id, int64_t max_wait_ms);

    NRWCoordinator* nrw_;
    std::unordered_map<uint32_t, SectionState>* sections_;
    std::string my_addr_;
    std::atomic<bool> running_{false};
    std::thread renewal_thread_;
};

}  // namespace seqsvr
