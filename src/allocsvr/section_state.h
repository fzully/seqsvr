#pragma once
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include "common/types.h"
#include "seqsvr.pb.h"

namespace seqsvr {

class SectionState {
public:
    SectionState(uint32_t section_id, uint64_t step)
        : section_id_(section_id), step_(step) {}

    // Non-copyable and non-movable: holds std::mutex and std::thread.
    SectionState(const SectionState&) = delete;
    SectionState& operator=(const SectionState&) = delete;

    ~SectionState() { JoinBackground(); }

    // Must be called before serving (sets cur_seq = max_seq). Also lazily
    // starts the background refill thread.
    void Reset(uint64_t max_seq);

    // Sets lease expiry; call after acquiring a lease from StoreSvr.
    void SetLease(int64_t expire_ms);
    void ClearLease();
    bool IsLeaseValid(int64_t now_ms) const;

    // Allocates the next seqno.
    //
    // fetch_fn(old_max) is stored and invoked asynchronously by a background
    // thread when the local buffer crosses the refill watermark, so this call
    // never blocks on the StoreSvr write. This is the key difference from the
    // previous synchronous design: holding the section lock (and a brpc
    // bthread) across a 300ms NRW write starved the bthread pool and caused
    // cascading client timeouts.
    //
    // If the local buffer is exhausted before the background refill finishes,
    // cur_seq is rolled back and a retryable STORE_ERROR is returned so the
    // caller can retry shortly.
    StatusOr<uint64_t> TryAlloc(std::function<uint64_t(uint64_t)> fetch_fn);

    // Blocks until the background refill thread has finished. Must be called
    // before the SectionState is destroyed so the background thread does not
    // touch freed memory (the fetch closure captures pointers to long-lived
    // objects such as NRWCoordinator, which are themselves destroyed only
    // after the caller joins all sections).
    void JoinBackground();

    uint32_t section_id() const { return section_id_; }
    uint64_t step()        const { return step_; }

private:
    void BgLoop();

    uint32_t section_id_;
    uint64_t step_;
    uint64_t cur_seq_{0};
    uint64_t max_seq_{0};
    int64_t  lease_expire_ms_{0};

    // Background refill state. All guarded by mu_ unless noted.
    std::function<uint64_t(uint64_t)> fetch_fn_;
    bool fetching_{false};
    bool bg_stop_{false};
    mutable std::mutex mu_;
    std::condition_variable bg_cv_;
    std::thread bg_thread_;
};

}  // namespace seqsvr
