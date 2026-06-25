#pragma once
#include <brpc/server.h>
#include "seqsvr.pb.h"
#include "storage.h"
#include "lease_manager.h"

namespace seqsvr {

class StoreServiceImpl : public StoreService {
public:
    StoreServiceImpl(Storage* storage, LeaseManager* lm)
        : storage_(storage), lm_(lm) {}

    void UpdateMaxSeq(::google::protobuf::RpcController* ctrl,
                      const UpdateMaxSeqRequest* req,
                      UpdateMaxSeqResponse* resp,
                      ::google::protobuf::Closure* done) override;

    void GetMaxSeq(::google::protobuf::RpcController* ctrl,
                   const GetMaxSeqRequest* req,
                   GetMaxSeqResponse* resp,
                   ::google::protobuf::Closure* done) override;

    void RequestLease(::google::protobuf::RpcController* ctrl,
                      const RequestLeaseRequest* req,
                      RequestLeaseResponse* resp,
                      ::google::protobuf::Closure* done) override;

    void RenewLease(::google::protobuf::RpcController* ctrl,
                    const RenewLeaseRequest* req,
                    RenewLeaseResponse* resp,
                    ::google::protobuf::Closure* done) override;

    void ReleaseLease(::google::protobuf::RpcController* ctrl,
                      const ReleaseLeaseRequest* req,
                      ReleaseLeaseResponse* resp,
                      ::google::protobuf::Closure* done) override;

    void GetRouteTable(::google::protobuf::RpcController* ctrl,
                       const GetRouteTableRequest* req,
                       GetRouteTableResponse* resp,
                       ::google::protobuf::Closure* done) override;

    void UpdateRouteTable(::google::protobuf::RpcController* ctrl,
                          const UpdateRouteTableRequest* req,
                          UpdateRouteTableResponse* resp,
                          ::google::protobuf::Closure* done) override;

private:
    Storage*      storage_;
    LeaseManager* lm_;
};

}  // namespace seqsvr
