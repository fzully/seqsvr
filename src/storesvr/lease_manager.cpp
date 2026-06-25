#include "lease_manager.h"

namespace seqsvr {

RequestLeaseResponse LeaseManager::RequestLease(uint32_t section_id,
                                                 const std::string& addr,
                                                 int64_t now_ms) {
    RequestLeaseResponse resp;
    std::lock_guard<std::mutex> lock(mu_);

    auto existing = storage_->GetLease(section_id);
    if (existing.has_value() && existing->expire_time_ms() > now_ms &&
        existing->holder_addr() != addr) {
        resp.set_error_code(ErrorCode::CONFLICT);
        resp.set_error_msg("section held by " + existing->holder_addr());
        *resp.mutable_current() = *existing;
        return resp;
    }

    LeaseRecord rec;
    rec.set_holder_addr(addr);
    rec.set_expire_time_ms(now_ms + kLeaseDurationMs);
    if (!storage_->SetLease(section_id, rec)) {
        resp.set_error_code(ErrorCode::STORE_ERROR);
        resp.set_error_msg("failed to persist lease");
        return resp;
    }

    resp.set_error_code(ErrorCode::OK);
    resp.set_max_seq(storage_->GetMaxSeq(section_id));
    resp.set_expire_time_ms(rec.expire_time_ms());
    return resp;
}

RenewLeaseResponse LeaseManager::RenewLease(uint32_t section_id,
                                              const std::string& addr,
                                              int64_t now_ms) {
    RenewLeaseResponse resp;
    std::lock_guard<std::mutex> lock(mu_);

    auto existing = storage_->GetLease(section_id);
    if (!existing.has_value() || existing->holder_addr() != addr) {
        resp.set_error_code(ErrorCode::LEASE_EXPIRED);
        return resp;
    }

    LeaseRecord rec;
    rec.set_holder_addr(addr);
    rec.set_expire_time_ms(now_ms + kLeaseDurationMs);
    if (!storage_->SetLease(section_id, rec)) {
        resp.set_error_code(ErrorCode::STORE_ERROR);
        return resp;
    }

    resp.set_error_code(ErrorCode::OK);
    resp.set_new_expire_time_ms(rec.expire_time_ms());
    return resp;
}

ReleaseLeaseResponse LeaseManager::ReleaseLease(uint32_t section_id,
                                                  const std::string& addr) {
    ReleaseLeaseResponse resp;
    std::lock_guard<std::mutex> lock(mu_);

    auto existing = storage_->GetLease(section_id);
    if (existing.has_value() && existing->holder_addr() == addr) {
        storage_->DeleteLease(section_id);
    }
    resp.set_error_code(ErrorCode::OK);
    return resp;
}

}  // namespace seqsvr
