#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <gflags/gflags.h>
#include "client/seq_client.h"

DEFINE_string(allocsvr, "127.0.0.1:19100", "AllocSvr address");
DEFINE_int32(threads, 8,    "Number of client threads");
DEFINE_int32(duration, 30,  "Benchmark duration in seconds");
DEFINE_uint64(uid, 0,       "Base UID for GetSeq calls");

using namespace seqsvr;
using namespace std::chrono;

struct Stats {
    std::vector<int64_t> latencies_us;
    uint64_t errors{0};
    uint64_t err_store{0};
    uint64_t err_lease{0};
    uint64_t err_other{0};
};

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    std::cout << "=== SeqSvr Performance Benchmark ===\n"
              << "AllocSvr: " << FLAGS_allocsvr << "\n"
              << "Threads:  " << FLAGS_threads << "\n"
              << "Duration: " << FLAGS_duration << "s\n\n";

    SeqClient client(FLAGS_allocsvr);

    std::atomic<bool> running{true};
    std::vector<Stats> per_thread(FLAGS_threads);

    auto worker = [&](int tid) {
        Stats& stat = per_thread[tid];
        stat.latencies_us.reserve(200000);
        // Spread threads across distinct sections (section_id = uid / 100000).
        // The AllocSvr must own sections 0..threads-1 for this to avoid REDIRECT.
        uint64_t uid = FLAGS_uid + static_cast<uint64_t>(tid) * 100000;
        while (running.load(std::memory_order_relaxed)) {
            auto t0 = steady_clock::now();
            auto r  = client.GetSeq(uid);
            auto t1 = steady_clock::now();
            if (r.ok()) {
                stat.latencies_us.push_back(
                    duration_cast<microseconds>(t1 - t0).count());
            } else {
                stat.errors++;
                switch (r.error_code()) {
                case seqsvr::ErrorCode::STORE_ERROR:  stat.err_store++; break;
                case seqsvr::ErrorCode::LEASE_EXPIRED: stat.err_lease++; break;
                default: stat.err_other++; break;
                }
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < FLAGS_threads; i++) threads.emplace_back(worker, i);

    std::this_thread::sleep_for(seconds(FLAGS_duration));
    running = false;
    for (auto& t : threads) t.join();

    // Aggregate
    std::vector<int64_t> all;
    uint64_t total_errors = 0, total_err_store = 0, total_err_lease = 0, total_err_other = 0;
    for (auto& s : per_thread) {
        all.insert(all.end(), s.latencies_us.begin(), s.latencies_us.end());
        total_errors     += s.errors;
        total_err_store  += s.err_store;
        total_err_lease  += s.err_lease;
        total_err_other  += s.err_other;
    }
    std::sort(all.begin(), all.end());

    uint64_t total_ops = all.size();
    double qps         = static_cast<double>(total_ops) / FLAGS_duration;
    double avg_us      = 0;
    for (int64_t v : all) avg_us += v;
    avg_us /= all.size();

    auto percentile = [&](double p) -> int64_t {
        if (all.empty()) return 0;
        size_t idx = static_cast<size_t>(p / 100.0 * all.size());
        return all[std::min(idx, all.size() - 1)];
    };

    std::cout << "=== Results ===\n"
              << "Total ops:  " << total_ops << "\n"
              << "Errors:     " << total_errors
              << " (STORE_ERROR=" << total_err_store
              << " LEASE_EXPIRED=" << total_err_lease
              << " other=" << total_err_other << ")\n"
              << "QPS:        " << static_cast<int>(qps) << "\n"
              << "Avg lat:    " << static_cast<int>(avg_us) << " us ("
              << avg_us / 1000.0 << " ms)\n"
              << "P50 lat:    " << percentile(50) << " us\n"
              << "P95 lat:    " << percentile(95) << " us\n"
              << "P99 lat:    " << percentile(99) << " us\n"
              << "P999 lat:   " << percentile(99.9) << " us\n";

    // Write report. The canonical perf_report.md is overwritten each run; a
    // timestamped copy is also saved under test_results/ so multiple runs
    // accumulate for comparison.
    auto write_report = [&](FILE* f) {
        fprintf(f, "# SeqSvr Performance Report\n\n");
        fprintf(f, "## Configuration\n\n");
        fprintf(f, "| Parameter | Value |\n|-----------|-------|\n");
        fprintf(f, "| AllocSvr  | %s |\n", FLAGS_allocsvr.c_str());
        fprintf(f, "| Threads   | %d |\n", FLAGS_threads);
        fprintf(f, "| Duration  | %ds |\n\n", FLAGS_duration);
        fprintf(f, "## Results\n\n");
        fprintf(f, "| Metric | Value |\n|--------|-------|\n");
        fprintf(f, "| Total operations | %lu |\n", (unsigned long)total_ops);
        fprintf(f, "| Errors           | %lu (STORE_ERROR=%lu LEASE_EXPIRED=%lu other=%lu) |\n",
                (unsigned long)total_errors,
                (unsigned long)total_err_store,
                (unsigned long)total_err_lease,
                (unsigned long)total_err_other);
        fprintf(f, "| QPS              | %.0f |\n", qps);
        fprintf(f, "| Avg latency      | %.2f ms |\n", avg_us / 1000.0);
        fprintf(f, "| P50 latency      | %.2f ms |\n", percentile(50) / 1000.0);
        fprintf(f, "| P95 latency      | %.2f ms |\n", percentile(95) / 1000.0);
        fprintf(f, "| P99 latency      | %.2f ms |\n", percentile(99) / 1000.0);
        fprintf(f, "| P99.9 latency    | %.2f ms |\n", percentile(99.9) / 1000.0);
    };

    if (FILE* f = fopen("perf_report.md", "w")) {
        write_report(f);
        fclose(f);
        std::cout << "\nReport written to perf_report.md\n";
    }

    // Timestamped archive copy: test_results/bench_YYYYmmdd-HHMMSS.md
    {
        std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", &tm);
        std::string path = std::string("test_results/bench_") + ts + ".md";
        if (FILE* f = fopen(path.c_str(), "w")) {
            write_report(f);
            fprintf(f, "\n## Run metadata\n\n");
            fprintf(f, "| Field | Value |\n|-------|-------|\n");
            fprintf(f, "| Timestamp | %s |\n", ts);
            fclose(f);
            std::cout << "Archived copy written to " << path << "\n";
        } else {
            std::cout << "Could not open " << path
                      << " (create the test_results/ directory first)\n";
        }
    }
    return 0;
}
