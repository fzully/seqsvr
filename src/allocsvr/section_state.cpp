#include "section_state.h"

namespace seqsvr {

void SectionState::Reset(uint64_t max_seq) {
    std::lock_guard<std::mutex> lock(mu_);
    cur_seq_ = max_seq;
    max_seq_ = max_seq;
}

void SectionState::SetLease(int64_t expire_ms) {
    std::lock_guard<std::mutex> lock(mu_);
    lease_expire_ms_ = expire_ms;
}

void SectionState::ClearLease() {
    std::lock_guard<std::mutex> lock(mu_);
    lease_expire_ms_ = 0;
}

bool SectionState::IsLeaseValid(int64_t now_ms) const {
    std::lock_guard<std::mutex> lock(mu_);
    return lease_expire_ms_ > now_ms;
}

StatusOr<uint64_t> SectionState::TryAlloc(std::function<uint64_t(uint64_t)> fetch_fn) {
    std::lock_guard<std::mutex> lock(mu_);

    if (lease_expire_ms_ <= NowMs()) {
        return StatusOr<uint64_t>(ErrorCode::LEASE_EXPIRED, "lease expired");
    }

    cur_seq_++;

    if (cur_seq_ > max_seq_) {
        uint64_t new_max = fetch_fn(max_seq_);
        if (new_max == 0 || new_max <= max_seq_) {
            cur_seq_--;
            return StatusOr<uint64_t>(ErrorCode::STORE_ERROR, "StoreSvr write failed");
        }
        max_seq_ = new_max;
    }

    return StatusOr<uint64_t>(cur_seq_);
}

}  // namespace seqsvr
