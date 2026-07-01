#include "seq_client.h"
#include <thread>
#include <butil/logging.h>

namespace seqsvr {

SeqClient::SeqClient(const std::string& bootstrap_addr) {
    route_cache_.SetBootstrap(bootstrap_addr);
    GetOrCreateChannel(bootstrap_addr);
}

brpc::Channel* SeqClient::GetOrCreateChannel(const std::string& addr) {
    std::lock_guard<std::mutex> lock(channels_mu_);
    auto it = channels_.find(addr);
    if (it != channels_.end()) return it->second.get();
    auto ch = std::make_unique<brpc::Channel>();
    brpc::ChannelOptions opts;
    opts.protocol   = "baidu_std";
    opts.timeout_ms = kTimeoutMs;
    opts.max_retry  = 0;
    ch->Init(addr.c_str(), &opts);
    auto* ptr = ch.get();
    channels_[addr] = std::move(ch);
    return ptr;
}

StatusOr<uint64_t> SeqClient::DoGetSeq(const GetSeqRequest& req) {
    int backoff_ms = kInitBackoff;
    for (int attempt = 0; attempt < kMaxRetry; attempt++) {
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            backoff_ms *= 2;
        }

        // Determine target address.
        uint32_t section_id = 0;
        if (req.has_uid())        section_id = static_cast<uint32_t>(req.uid() / 100000);
        else if (req.has_session_id()) section_id = static_cast<uint32_t>(req.session_id() / 100000);
        else if (req.has_global_key()) section_id = static_cast<uint32_t>(
            std::hash<std::string>{}(req.global_key()) % 100000);

        std::string addr = route_cache_.Lookup(section_id);
        if (addr.empty()) addr = route_cache_.Bootstrap();
        if (addr.empty()) return StatusOr<uint64_t>(ErrorCode::STORE_ERROR, "no route");

        brpc::Controller cntl;
        GetSeqResponse resp;
        AllocService_Stub stub(GetOrCreateChannel(addr));
        stub.GetSeq(&cntl, &req, &resp, nullptr);

        if (cntl.Failed()) {
            LOG(WARNING) << "[client] RPC failed attempt=" << attempt
                         << " err=" << cntl.ErrorText();
            continue;  // network error → retry
        }

        if (resp.route_ver() > route_cache_.Version() && resp.has_route()) {
            route_cache_.Update(resp.route());
        }

        switch (resp.error_code()) {
        case ErrorCode::OK:
            return StatusOr<uint64_t>(resp.seqno());
        case ErrorCode::REDIRECT:
            if (!resp.redirect_addr().empty()) {
                GetOrCreateChannel(resp.redirect_addr());
            }
            continue;
        case ErrorCode::LEASE_EXPIRED:
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        case ErrorCode::STORE_ERROR:
            LOG(WARNING) << "[client] server STORE_ERROR attempt=" << attempt
                         << " msg=" << resp.error_msg();
            continue;
        default:
            return StatusOr<uint64_t>(resp.error_code(), resp.error_msg());
        }
    }
    return StatusOr<uint64_t>(ErrorCode::STORE_ERROR, "max retries exceeded");
}

StatusOr<uint64_t> SeqClient::GetSeq(uint64_t uid) {
    GetSeqRequest req;
    req.set_uid(uid);
    req.set_route_ver(route_cache_.Version());
    return DoGetSeq(req);
}

StatusOr<uint64_t> SeqClient::GetSessionSeq(uint64_t session_id) {
    GetSeqRequest req;
    req.set_session_id(session_id);
    req.set_route_ver(route_cache_.Version());
    return DoGetSeq(req);
}

StatusOr<uint64_t> SeqClient::GetGlobalSeq(const std::string& name) {
    GetSeqRequest req;
    req.set_global_key(name);
    req.set_route_ver(route_cache_.Version());
    return DoGetSeq(req);
}

}  // namespace seqsvr
