#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
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
        uint64_t uid = FLAGS_uid + static_cast<uint64_t>(tid) * 10000;
        while (running.load(std::memory_order_relaxed)) {
            auto t0 = steady_clock::now();
            auto r  = client.GetSeq(uid);
            auto t1 = steady_clock::now();
            if (r.ok()) {
                stat.latencies_us.push_back(
                    duration_cast<microseconds>(t1 - t0).count());
            } else {
                stat.errors++;
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
    uint64_t total_errors = 0;
    for (auto& s : per_thread) {
        all.insert(all.end(), s.latencies_us.begin(), s.latencies_us.end());
        total_errors += s.errors;
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
              << "Errors:     " << total_errors << "\n"
              << "QPS:        " << static_cast<int>(qps) << "\n"
              << "Avg lat:    " << static_cast<int>(avg_us) << " us ("
              << avg_us / 1000.0 << " ms)\n"
              << "P50 lat:    " << percentile(50) << " us\n"
              << "P95 lat:    " << percentile(95) << " us\n"
              << "P99 lat:    " << percentile(99) << " us\n"
              << "P999 lat:   " << percentile(99.9) << " us\n";

    // Write report
    FILE* f = fopen("perf_report.md", "w");
    if (f) {
        fprintf(f, "# SeqSvr Performance Report\n\n");
        fprintf(f, "## Configuration\n\n");
        fprintf(f, "| Parameter | Value |\n|-----------|-------|\n");
        fprintf(f, "| AllocSvr  | %s |\n", FLAGS_allocsvr.c_str());
        fprintf(f, "| Threads   | %d |\n", FLAGS_threads);
        fprintf(f, "| Duration  | %ds |\n\n", FLAGS_duration);
        fprintf(f, "## Results\n\n");
        fprintf(f, "| Metric | Value |\n|--------|-------|\n");
        fprintf(f, "| Total operations | %lu |\n", (unsigned long)total_ops);
        fprintf(f, "| Errors           | %lu |\n", (unsigned long)total_errors);
        fprintf(f, "| QPS              | %.0f |\n", qps);
        fprintf(f, "| Avg latency      | %.2f ms |\n", avg_us / 1000.0);
        fprintf(f, "| P50 latency      | %.2f ms |\n", percentile(50) / 1000.0);
        fprintf(f, "| P95 latency      | %.2f ms |\n", percentile(95) / 1000.0);
        fprintf(f, "| P99 latency      | %.2f ms |\n", percentile(99) / 1000.0);
        fprintf(f, "| P99.9 latency    | %.2f ms |\n", percentile(99.9) / 1000.0);
        fclose(f);
        std::cout << "\nReport written to perf_report.md\n";
    }
    return 0;
}
