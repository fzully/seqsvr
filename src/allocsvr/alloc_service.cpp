#include "alloc_service.h"
#include "common/types.h"

namespace seqsvr {

void AllocServiceImpl::GetSeq(::google::protobuf::RpcController*,
                               const GetSeqRequest* req,
                               GetSeqResponse* resp,
                               ::google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);

    uint32_t section_id = 0;
    if (req->has_uid()) {
        section_id = ToSectionId(req->uid());
    } else if (req->has_session_id()) {
        section_id = ToSectionId(req->session_id());
    } else if (req->has_global_key()) {
        section_id = std::hash<std::string>{}(req->global_key()) % 100000;
    } else {
        resp->set_error_code(ErrorCode::REDIRECT);
        resp->set_error_msg("no key specified");
        return;
    }

    auto it = sections_->find(section_id);
    if (it == sections_->end()) {
        resp->set_error_code(ErrorCode::REDIRECT);
        resp->set_error_msg("section not owned by this node");
        return;
    }

    auto result = it->second.TryAlloc([&](uint64_t old_max) -> uint64_t {
        uint64_t new_max = old_max + it->second.step();
        return nrw_->WriteMaxSeq(section_id, new_max) ? new_max : 0;
    });

    if (!result.ok()) {
        resp->set_error_code(result.error_code());
        resp->set_error_msg(result.error_msg());
        return;
    }

    resp->set_error_code(ErrorCode::OK);
    resp->set_seqno(result.value());
    resp->set_route_ver(route_cache_->Version());

    // Only send route table if client's version is stale.
    if (req->route_ver() < route_cache_->Version()) {
        // Build route table for client.
        // (Simplified: just set version; full route table would be populated from cache)
    }
}

}  // namespace seqsvr
