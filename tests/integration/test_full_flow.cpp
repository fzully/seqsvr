#include <gtest/gtest.h>
#include <brpc/server.h>
#include <gflags/gflags.h>
#include <filesystem>
#include <thread>
#include <vector>
#include <unordered_map>
#include "storesvr/storage.h"
#include "storesvr/lease_manager.h"
#include "storesvr/store_service.h"
#include "allocsvr/section_state.h"
#include "allocsvr/nrw_coordinator.h"
#include "allocsvr/lease_holder.h"
#include "allocsvr/alloc_service.h"
#include "client/seq_client.h"
#include "common/route_table.h"

using namespace seqsvr;

struct StoreSvrInstance {
    std::string db_path;
    Storage storage;
    std::unique_ptr<LeaseManager> lm;
    std::unique_ptr<StoreServiceImpl> service;
    brpc::Server server;
    int port;

    bool Start(int p, const std::string& path) {
        port = p;
        db_path = path;
        std::filesystem::remove_all(db_path);
        if (!storage.Open(db_path)) return false;
        lm      = std::make_unique<LeaseManager>(&storage);
        service = std::make_unique<StoreServiceImpl>(&storage, lm.get());
        if (server.AddService(service.get(), brpc::SERVER_DOESNT_OWN_SERVICE) != 0) return false;
        brpc::ServerOptions opts; opts.num_threads = 2;
        return server.Start(port, &opts) == 0;
    }
    ~StoreSvrInstance() {
        server.Stop(0); server.Join();
        std::filesystem::remove_all(db_path);
    }
};

struct AllocSvrInstance {
    std::unordered_map<uint32_t, SectionState> sections;
    std::unique_ptr<NRWCoordinator> nrw;
    std::unique_ptr<LeaseHolder> lh;
    RouteTableCache route_cache;
    std::unique_ptr<AllocServiceImpl> service;
    brpc::Server server;
    int port;

    bool Start(int p, const std::vector<std::string>& store_addrs,
               const std::vector<uint32_t>& sids, const std::string& my_addr) {
        port = p;
        for (uint32_t sid : sids) {
            sections.emplace(std::piecewise_construct,
                             std::forward_as_tuple(sid),
                             std::forward_as_tuple(sid, /*step=*/10000));
        }
        nrw     = std::make_unique<NRWCoordinator>(store_addrs);
        lh      = std::make_unique<LeaseHolder>(nrw.get(), &sections);
        if (!lh->Start(sids, my_addr)) return false;
        service = std::make_unique<AllocServiceImpl>(&sections, nrw.get(), &route_cache);
        if (server.AddService(service.get(), brpc::SERVER_DOESNT_OWN_SERVICE) != 0) return false;
        brpc::ServerOptions opts; opts.num_threads = 4;
        return server.Start(port, &opts) == 0;
    }
    ~AllocSvrInstance() {
        if (lh) lh->Stop();
        server.Stop(0); server.Join();
    }
};

class FullFlowTest : public ::testing::Test {
protected:
    std::vector<std::unique_ptr<StoreSvrInstance>> stores_;
    std::unique_ptr<AllocSvrInstance> alloc_;

    void SetUp() override {
        // Start 3 StoreSvr on ports 18200-18202
        for (int i = 0; i < 3; i++) {
            auto s = std::make_unique<StoreSvrInstance>();
            ASSERT_TRUE(s->Start(18200 + i, "/tmp/seqsvr_itest_store" + std::to_string(i)));
            stores_.push_back(std::move(s));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Start AllocSvr on port 19100 owning sections 0 and 1
        alloc_ = std::make_unique<AllocSvrInstance>();
        ASSERT_TRUE(alloc_->Start(19100,
            {"127.0.0.1:18200", "127.0.0.1:18201", "127.0.0.1:18202"},
            {0, 1}, "127.0.0.1:19100"));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void TearDown() override {
        alloc_.reset();
        stores_.clear();
    }
};

TEST_F(FullFlowTest, BasicGetSeq) {
    SeqClient client("127.0.0.1:19100");
    auto r1 = client.GetSeq(0);
    ASSERT_TRUE(r1.ok()) << r1.error_msg();
    EXPECT_GT(r1.value(), 0u);

    auto r2 = client.GetSeq(0);
    ASSERT_TRUE(r2.ok());
    EXPECT_GT(r2.value(), r1.value());
}

TEST_F(FullFlowTest, MonotonicAcrossUIDs) {
    SeqClient client("127.0.0.1:19100");
    uint64_t last = 0;
    for (int i = 0; i < 50; i++) {
        auto r = client.GetSeq(i);  // all in section 0
        ASSERT_TRUE(r.ok());
        EXPECT_GT(r.value(), last);
        last = r.value();
    }
}

TEST_F(FullFlowTest, MultiSectionIndependent) {
    SeqClient client("127.0.0.1:19100");
    // Section 0: uid 0; Section 1: uid 100000
    auto r0 = client.GetSeq(0);
    auto r1 = client.GetSeq(100000);
    ASSERT_TRUE(r0.ok());
    ASSERT_TRUE(r1.ok());
    // Both start from their own max_seq, so just verify they're > 0
    EXPECT_GT(r0.value(), 0u);
    EXPECT_GT(r1.value(), 0u);
}

TEST_F(FullFlowTest, StoreSvrOneNodeDown) {
    // Kill one StoreSvr — NRW W=2 should still work
    stores_[2]->server.Stop(0);
    stores_[2]->server.Join();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    SeqClient client("127.0.0.1:19100");
    // Force a StoreSvr write by exhausting cur section step
    for (int i = 0; i < 10010; i++) {
        auto r = client.GetSeq(0);
        ASSERT_TRUE(r.ok()) << "failed at i=" << i << " err=" << r.error_msg();
    }
}

TEST_F(FullFlowTest, SeqnoNeverGoesBackward) {
    SeqClient client("127.0.0.1:19100");
    std::vector<uint64_t> seqs;
    std::mutex mu;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 500; i++) {
                auto r = client.GetSeq(0);
                if (r.ok()) {
                    std::lock_guard<std::mutex> lock(mu);
                    seqs.push_back(r.value());
                }
            }
        });
    }
    for (auto& th : threads) th.join();
    std::sort(seqs.begin(), seqs.end());
    for (size_t i = 1; i < seqs.size(); i++) {
        EXPECT_GT(seqs[i], seqs[i-1]) << "seqno went backward at index " << i;
    }
}
