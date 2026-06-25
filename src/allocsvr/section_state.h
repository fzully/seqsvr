#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include "common/types.h"
#include "seqsvr.pb.h"

namespace seqsvr {

class SectionState {
public:
    SectionState(uint32_t section_id, uint64_t step)
        : section_id_(section_id), step_(step) {}

    // Must be called before serving (sets cur_seq = max_seq).
    void Reset(uint64_t max_seq);

    // Sets lease expiry; call after acquiring a lease from StoreSvr.
    void SetLease(int64_t expire_ms);
    void ClearLease();
    bool IsLeaseValid(int64_t now_ms) const;

    // Allocates the next seqno.
    // fetch_fn(old_max) is called (under the lock) when cur_seq > max_seq.
    //   Returns new max_seq, or 0 to signal failure.
    // Returns LEASE_EXPIRED or STORE_ERROR on failure.
    StatusOr<uint64_t> TryAlloc(std::function<uint64_t(uint64_t)> fetch_fn);

    uint32_t section_id() const { return section_id_; }
    uint64_t step()        const { return step_; }

private:
    uint32_t section_id_;
    uint64_t step_;
    uint64_t cur_seq_{0};
    uint64_t max_seq_{0};
    int64_t  lease_expire_ms_{0};
    mutable std::mutex mu_;
};

}  // namespace seqsvr
