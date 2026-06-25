#include "store_service.h"
#include "common/types.h"

namespace seqsvr {

void StoreServiceImpl::UpdateMaxSeq(::google::protobuf::RpcController*,
                                     const UpdateMaxSeqRequest* req,
                                     UpdateMaxSeqResponse* resp,
                                     ::google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    if (storage_->SetMaxSeq(req->section_id(), req->new_max_seq())) {
        resp->set_error_code(ErrorCode::OK);
    } else {
        resp->set_error_code(ErrorCode::STORE_ERROR);
        resp->set_error_msg("rocksdb write failed");
    }
}

void StoreServiceImpl::GetMaxSeq(::google::protobuf::RpcController*,
                                  const GetMaxSeqRequest* req,
                                  GetMaxSeqResponse* resp,
                                  ::google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    resp->set_error_code(ErrorCode::OK);
    resp->set_max_seq(storage_->GetMaxSeq(req->section_id()));
}

void StoreServiceImpl::RequestLease(::google::protobuf::RpcController*,
                                     const RequestLeaseRequest* req,
                                     RequestLeaseResponse* resp,
                                     ::google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    auto r = lm_->RequestLease(req->section_id(), req->requester_addr(), NowMs());
    resp->CopyFrom(r);
}

void StoreServiceImpl::RenewLease(::google::protobuf::RpcController*,
                                   const RenewLeaseRequest* req,
                                   RenewLeaseResponse* resp,
                                   ::google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    auto r = lm_->RenewLease(req->section_id(), req->holder_addr(), NowMs());
    resp->CopyFrom(r);
}

void StoreServiceImpl::ReleaseLease(::google::protobuf::RpcController*,
                                     const ReleaseLeaseRequest* req,
                                     ReleaseLeaseResponse* resp,
                                     ::google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    auto r = lm_->ReleaseLease(req->section_id(), req->holder_addr());
    resp->CopyFrom(r);
}

void StoreServiceImpl::GetRouteTable(::google::protobuf::RpcController*,
                                      const GetRouteTableRequest*,
                                      GetRouteTableResponse* resp,
                                      ::google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    auto rt = storage_->GetRouteTable();
    if (rt.has_value()) {
        resp->set_error_code(ErrorCode::OK);
        *resp->mutable_route() = std::move(*rt);
    } else {
        resp->set_error_code(ErrorCode::NOT_FOUND);
    }
}

void StoreServiceImpl::UpdateRouteTable(::google::protobuf::RpcController*,
                                         const UpdateRouteTableRequest* req,
                                         UpdateRouteTableResponse* resp,
                                         ::google::protobuf::Closure* done) {
    brpc::ClosureGuard guard(done);
    if (storage_->SetRouteTable(req->route())) {
        resp->set_error_code(ErrorCode::OK);
    } else {
        resp->set_error_code(ErrorCode::STORE_ERROR);
    }
}

}  // namespace seqsvr
