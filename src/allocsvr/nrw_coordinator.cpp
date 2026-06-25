#include "nrw_coordinator.h"
#include <atomic>
#include <thread>
#include <brpc/controller.h>

namespace seqsvr {

NRWCoordinator::NRWCoordinator(const std::vector<std::string>& store_addrs) {
    for (const auto& addr : store_addrs) {
        Peer p;
        p.channel = std::make_unique<brpc::Channel>();
        brpc::ChannelOptions opts;
        opts.protocol   = "baidu_std";
        opts.timeout_ms = 300;
        opts.max_retry  = 0;
        p.channel->Init(addr.c_str(), &opts);
        p.stub = std::make_unique<StoreService_Stub>(p.channel.get());
        peers_.push_back(std::move(p));
    }
}

bool NRWCoordinator::WriteMaxSeq(uint32_t section_id, uint64_t new_max_seq) {
    std::atomic<int> success{0};
    std::vector<std::thread> threads;
    for (size_t i = 0; i < peers_.size(); i++) {
        threads.emplace_back([&, i]() {
            brpc::Controller cntl;
            UpdateMaxSeqRequest req;
            UpdateMaxSeqResponse resp;
            req.set_section_id(section_id);
            req.set_new_max_seq(new_max_seq);
            peers_[i].stub->UpdateMaxSeq(&cntl, &req, &resp, nullptr);
            if (!cntl.Failed() && resp.error_code() == ErrorCode::OK) {
                success.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : threads) t.join();
    return success.load() >= w_;
}

uint64_t NRWCoordinator::ReadMaxSeq(uint32_t section_id) {
    std::atomic<uint64_t> max_val{0};
    std::vector<std::thread> threads;
    for (size_t i = 0; i < peers_.size(); i++) {
        threads.emplace_back([&, i]() {
            brpc::Controller cntl;
            GetMaxSeqRequest req;
            GetMaxSeqResponse resp;
            req.set_section_id(section_id);
            peers_[i].stub->GetMaxSeq(&cntl, &req, &resp, nullptr);
            if (!cntl.Failed() && resp.error_code() == ErrorCode::OK) {
                uint64_t v = resp.max_seq();
                uint64_t cur = max_val.load();
                while (v > cur && !max_val.compare_exchange_weak(cur, v)) {}
            }
        });
    }
    for (auto& t : threads) t.join();
    return max_val.load();
}

RequestLeaseResponse NRWCoordinator::RequestLease(uint32_t section_id,
                                                    const std::string& addr) {
    std::atomic<int> success{0};
    std::vector<RequestLeaseResponse> resps(peers_.size());
    std::vector<std::thread> threads;
    for (size_t i = 0; i < peers_.size(); i++) {
        threads.emplace_back([&, i]() {
            brpc::Controller cntl;
            RequestLeaseRequest req;
            req.set_section_id(section_id);
            req.set_requester_addr(addr);
            peers_[i].stub->RequestLease(&cntl, &req, &resps[i], nullptr);
            if (!cntl.Failed() && resps[i].error_code() == ErrorCode::OK) {
                success.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : threads) t.join();

    if (success.load() >= w_) {
        // Return the response with max_seq (take the highest seen).
        RequestLeaseResponse best;
        best.set_error_code(ErrorCode::OK);
        for (auto& r : resps) {
            if (r.error_code() == ErrorCode::OK && r.max_seq() > best.max_seq()) {
                best.set_max_seq(r.max_seq());
                best.set_expire_time_ms(r.expire_time_ms());
            }
        }
        return best;
    }
    // Return first CONFLICT if any.
    for (auto& r : resps) {
        if (r.error_code() == ErrorCode::CONFLICT) return r;
    }
    RequestLeaseResponse err;
    err.set_error_code(ErrorCode::STORE_ERROR);
    err.set_error_msg("NRW quorum not met");
    return err;
}

RenewLeaseResponse NRWCoordinator::RenewLease(uint32_t section_id,
                                               const std::string& addr) {
    std::atomic<int> success{0};
    std::vector<RenewLeaseResponse> resps(peers_.size());
    std::vector<std::thread> threads;
    for (size_t i = 0; i < peers_.size(); i++) {
        threads.emplace_back([&, i]() {
            brpc::Controller cntl;
            RenewLeaseRequest req;
            req.set_section_id(section_id);
            req.set_holder_addr(addr);
            peers_[i].stub->RenewLease(&cntl, &req, &resps[i], nullptr);
            if (!cntl.Failed() && resps[i].error_code() == ErrorCode::OK) {
                success.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : threads) t.join();

    if (success.load() >= w_) {
        RenewLeaseResponse r;
        r.set_error_code(ErrorCode::OK);
        int64_t max_exp = 0;
        for (auto& resp : resps) {
            if (resp.new_expire_time_ms() > max_exp) max_exp = resp.new_expire_time_ms();
        }
        r.set_new_expire_time_ms(max_exp);
        return r;
    }
    RenewLeaseResponse r;
    r.set_error_code(ErrorCode::LEASE_EXPIRED);
    return r;
}

void NRWCoordinator::ReleaseLease(uint32_t section_id, const std::string& addr) {
    for (size_t i = 0; i < peers_.size(); i++) {
        std::thread([this, i, section_id, addr]() {
            brpc::Controller cntl;
            ReleaseLeaseRequest req;
            ReleaseLeaseResponse resp;
            req.set_section_id(section_id);
            req.set_holder_addr(addr);
            peers_[i].stub->ReleaseLease(&cntl, &req, &resp, nullptr);
        }).detach();
    }
}

}  // namespace seqsvr
