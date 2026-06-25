#pragma once
#include <brpc/server.h>
#include <unordered_map>
#include "seqsvr.pb.h"
#include "section_state.h"
#include "nrw_coordinator.h"
#include "common/route_table.h"

namespace seqsvr {

class AllocServiceImpl : public AllocService {
public:
    AllocServiceImpl(std::unordered_map<uint32_t, SectionState>* sections,
                     NRWCoordinator* nrw,
                     RouteTableCache* route_cache)
        : sections_(sections), nrw_(nrw), route_cache_(route_cache) {}

    void GetSeq(::google::protobuf::RpcController* ctrl,
                const GetSeqRequest* req,
                GetSeqResponse* resp,
                ::google::protobuf::Closure* done) override;

private:
    uint32_t ToSectionId(uint64_t id) const { return static_cast<uint32_t>(id / 100000); }

    std::unordered_map<uint32_t, SectionState>* sections_;
    NRWCoordinator* nrw_;
    RouteTableCache* route_cache_;
};

}  // namespace seqsvr
