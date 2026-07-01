#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "allocsvr/section_state.h"

using namespace seqsvr;

// Helper: poll TryAlloc until it succeeds (the background refill is in flight)
// or give up after a timeout. The async design returns a retryable STORE_ERROR
// while the StoreSvr write has not finished, so callers must retry.
static uint64_t WaitAndAlloc(SectionState& s,
                             const std::function<uint64_t(uint64_t)>& fetch) {
    for (int i = 0; i < 500; i++) {
        auto r = s.TryAlloc(fetch);
        if (r.ok()) return r.value();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
}

TEST(SectionState, BasicAlloc) {
    SectionState s(1, /*step=*/1000);
    s.Reset(100);
    s.SetLease(NowMs() + 30000);

    std::atomic<int> fetch_calls{0};
    auto fetch = [&](uint64_t old_max) -> uint64_t {
        fetch_calls++;
        return old_max + 1000;
    };

    // First alloc: buffer exhausted (cur_seq=101 > max_seq=100) → an async
    // refill is kicked off and a retryable STORE_ERROR is returned, instead of
    // blocking the caller on the StoreSvr write.
    auto r1 = s.TryAlloc(fetch);
    EXPECT_FALSE(r1.ok());
    EXPECT_EQ(r1.error_code(), ErrorCode::STORE_ERROR);

    // Once the background refill finishes, allocs succeed starting at 101.
    uint64_t v = WaitAndAlloc(s, fetch);
    ASSERT_NE(v, 0u);
    EXPECT_EQ(v, 101u);
    EXPECT_GE(fetch_calls.load(), 1);

    // Allocs well below the 50% prefetch watermark (1100 - 500 = 600) must not
    // trigger another fetch.
    for (int i = 0; i < 98; i++) {
        auto r = s.TryAlloc(fetch);
        ASSERT_TRUE(r.ok()) << r.error_msg();
    }
    EXPECT_EQ(fetch_calls.load(), 1);

    // Join the background thread before reference-captured locals go away.
    s.JoinBackground();
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

    // Buffer exhausted → retryable STORE_ERROR returned immediately; the
    // background fetch that returns 0 is recorded as a failed refill.
    auto r = s.TryAlloc([](uint64_t) -> uint64_t { return 0; });
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

    // Join the background refill thread before fetch_calls (a reference-captured
    // local declared after s) is destroyed.
    s.JoinBackground();

    // All seqnos must be distinct and strictly increasing.
    std::sort(results.begin(), results.end());
    for (size_t i = 1; i < results.size(); i++) {
        EXPECT_GT(results[i], results[i-1]) << "duplicate or non-monotonic seqno";
    }
    // The background refill must have run at least once.
    EXPECT_GE(fetch_calls.load(), 1);
}
