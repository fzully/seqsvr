#include "lease_holder.h"
#include <butil/logging.h>
#include <thread>
#include "common/types.h"

namespace seqsvr {

bool LeaseHolder::AcquireLease(uint32_t section_id, int64_t max_wait_ms) {
    int64_t deadline = NowMs() + max_wait_ms;
    while (NowMs() < deadline) {
        auto resp = nrw_->RequestLease(section_id, my_addr_);
        if (resp.error_code() == ErrorCode::OK) {
            auto it = sections_->find(section_id);
            if (it == sections_->end()) return false;
            uint64_t max_seq = std::max(resp.max_seq(), nrw_->ReadMaxSeq(section_id));
            it->second.Reset(max_seq);
            it->second.SetLease(resp.expire_time_ms());
            LOG(INFO) << "Acquired lease for section " << section_id
                      << " max_seq=" << max_seq;
            return true;
        }
        if (resp.error_code() == ErrorCode::CONFLICT) {
            int64_t wait_until = resp.current().expire_time_ms() + 100;
            int64_t sleep_ms = std::min(wait_until - NowMs(), (int64_t)500);
            if (sleep_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    return false;
}

bool LeaseHolder::Start(const std::vector<uint32_t>& section_ids,
                        const std::string& my_addr,
                        int64_t max_wait_ms) {
    my_addr_ = my_addr;
    for (uint32_t sid : section_ids) {
        if (!AcquireLease(sid, max_wait_ms)) {
            LOG(ERROR) << "Failed to acquire lease for section " << sid;
            return false;
        }
    }
    running_ = true;
    renewal_thread_ = std::thread(&LeaseHolder::RenewalLoop, this);
    return true;
}

void LeaseHolder::Stop() {
    running_ = false;
    if (renewal_thread_.joinable()) renewal_thread_.join();
    for (auto& [sid, state] : *sections_) {
        nrw_->ReleaseLease(sid, my_addr_);
        state.ClearLease();
    }
}

void LeaseHolder::RenewalLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kRenewalIntervalMs));
        if (!running_) break;
        for (auto& [sid, state] : *sections_) {
            if (!state.IsLeaseValid(NowMs() - 5000)) continue;
            auto resp = nrw_->RenewLease(sid, my_addr_);
            if (resp.error_code() == ErrorCode::OK) {
                state.SetLease(resp.new_expire_time_ms());
            } else {
                LOG(WARNING) << "Failed to renew lease for section " << sid;
                state.ClearLease();
            }
        }
    }
}

}  // namespace seqsvr
