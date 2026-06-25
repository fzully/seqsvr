#pragma once
#include <brpc/channel.h>
#include <memory>
#include <string>
#include <vector>
#include "seqsvr.pb.h"

namespace seqsvr {

class NRWCoordinator {
public:
    explicit NRWCoordinator(const std::vector<std::string>& store_addrs);

    // NRW write max_seq; returns true if W=2 nodes confirmed.
    bool WriteMaxSeq(uint32_t section_id, uint64_t new_max_seq);

    // Read max_seq from up to N nodes; return the max seen (ensures no rollback).
    uint64_t ReadMaxSeq(uint32_t section_id);

    // Lease ops with W=2 quorum.
    RequestLeaseResponse RequestLease(uint32_t section_id, const std::string& addr);
    RenewLeaseResponse   RenewLease(uint32_t section_id, const std::string& addr);
    void                 ReleaseLease(uint32_t section_id, const std::string& addr);

private:
    struct Peer {
        std::unique_ptr<brpc::Channel> channel;
        std::unique_ptr<StoreService_Stub> stub;
    };

    std::vector<Peer> peers_;
    int w_{2};  // write quorum
};

}  // namespace seqsvr
