#include "section_state.h"
#include <butil/logging.h>

namespace seqsvr {

void SectionState::Reset(uint64_t max_seq) {
    std::lock_guard<std::mutex> lock(mu_);
    cur_seq_ = max_seq;
    max_seq_ = max_seq;
    if (!bg_thread_.joinable()) {
        bg_thread_ = std::thread(&SectionState::BgLoop, this);
    }
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

    int64_t now = NowMs();
    if (lease_expire_ms_ <= now) {
        LOG(WARNING) << "[section=" << section_id_ << "] LEASE_EXPIRED"
                     << " lease_expire=" << lease_expire_ms_ << " now=" << now;
        return StatusOr<uint64_t>(ErrorCode::LEASE_EXPIRED, "lease expired");
    }

    cur_seq_++;

    // Decide whether a background refill is needed. We trigger a prefetch
    // when the buffer is half-used (so the ~300ms StoreSvr write usually
    // completes before exhaustion) and also when the buffer is already
    // exhausted (the cold-start / slow-refill path).
    bool need_refill = false;
    if (cur_seq_ > max_seq_) {
        need_refill = true;  // buffer exhausted
    } else if (max_seq_ >= step_ && cur_seq_ >= max_seq_ - step_ / 2) {
        need_refill = true;  // past 50% watermark
    }

    if (need_refill && !fetching_) {
        fetch_fn_ = std::move(fetch_fn);
        fetching_ = true;
        bg_cv_.notify_one();
    }

    if (cur_seq_ > max_seq_) {
        // Buffer exhausted before the refill completed. Roll back and ask the
        // caller to retry; the background fetch is already in flight.
        cur_seq_--;
        return StatusOr<uint64_t>(ErrorCode::STORE_ERROR,
                                   "refilling, retry shortly");
    }

    return StatusOr<uint64_t>(cur_seq_);
}

void SectionState::BgLoop() {
    std::unique_lock<std::mutex> lock(mu_);
    while (!bg_stop_) {
        bg_cv_.wait(lock, [this] {
            return bg_stop_ || (fetching_ && static_cast<bool>(fetch_fn_));
        });
        if (bg_stop_) return;

        auto fn = fetch_fn_;
        uint64_t old_max = max_seq_;
        // Release the lock for the slow StoreSvr write: this is the whole
        // point of the refactor. Other TryAlloc callers can keep serving
        // seqnos from the remaining buffer, and no brpc bthread is held.
        lock.unlock();

        uint64_t new_max = fn(old_max);

        lock.lock();
        fetching_ = false;
        if (new_max != 0 && new_max > max_seq_) {
            max_seq_ = new_max;
            LOG(INFO) << "[section=" << section_id_ << "] async refill OK"
                      << " new_max=" << new_max;
        } else {
            LOG(WARNING) << "[section=" << section_id_ << "] async refill FAILED"
                         << " fetch_fn returned " << new_max
                         << " (cur max_seq=" << max_seq_ << ")";
        }
    }
}

void SectionState::JoinBackground() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        bg_stop_ = true;
    }
    bg_cv_.notify_all();
    if (bg_thread_.joinable()) {
        bg_thread_.join();
    }
}

}  // namespace seqsvr
