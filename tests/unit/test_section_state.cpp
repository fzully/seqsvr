#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include "allocsvr/section_state.h"

using namespace seqsvr;

TEST(SectionState, BasicAlloc) {
    SectionState s(1, /*step=*/10);
    s.Reset(100);
    s.SetLease(NowMs() + 30000);

    int fetch_calls = 0;
    auto fetch = [&](uint64_t /*old_max*/) -> uint64_t {
        fetch_calls++;
        return 200;
    };

    // First alloc: cur_seq goes from 100 to 101; 101 > 100 → fetch!
    auto r1 = s.TryAlloc(fetch);
    ASSERT_TRUE(r1.ok());
    EXPECT_EQ(r1.value(), 101u);
    EXPECT_EQ(fetch_calls, 1);

    // Now max_seq = 200. Next allocs up to 200 should not trigger fetch.
    for (int i = 0; i < 98; i++) {
        auto r = s.TryAlloc(fetch);
        ASSERT_TRUE(r.ok());
    }
    EXPECT_EQ(fetch_calls, 1);
}

TEST(SectionState, LeaseExpired) {
    SectionState s(1, 10);
    s.Reset(0);
    s.SetLease(NowMs() - 1);  // already expired

    auto r = s.TryAlloc([](uint64_t) { return 100; });
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error_code(), ErrorCode::LEASE_EXPIRED);
}

TEST(SectionState, FetchFailureRollsBack) {
    SectionState s(1, 10);
    s.Reset(0);
    s.SetLease(NowMs() + 30000);

    auto r = s.TryAlloc([](uint64_t) -> uint64_t { return 0; });  // 0 signals failure
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error_code(), ErrorCode::STORE_ERROR);
}

TEST(SectionState, MonotonicUnderConcurrency) {
    SectionState s(1, 100);
    s.Reset(0);
    s.SetLease(NowMs() + 30000);

    std::atomic<int> fetch_calls{0};
    auto fetch = [&](uint64_t old_max) -> uint64_t {
        fetch_calls++;
        return old_max + 1000;
    };

    std::vector<uint64_t> results;
    std::mutex results_mu;
    std::vector<std::thread> threads;

    for (int t = 0; t < 8; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 1000; i++) {
                auto r = s.TryAlloc(fetch);
                if (r.ok()) {
                    std::lock_guard<std::mutex> lock(results_mu);
                    results.push_back(r.value());
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    // All seqnos must be distinct and > 0
    std::sort(results.begin(), results.end());
    for (size_t i = 1; i < results.size(); i++) {
        EXPECT_GT(results[i], results[i-1]) << "duplicate or non-monotonic seqno";
    }
}
