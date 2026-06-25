#include <gtest/gtest.h>
#include <thread>
#include "common/route_table.h"

using namespace seqsvr;

TEST(RouteTableCache, LookupMissReturnsEmpty) {
    RouteTableCache cache;
    EXPECT_EQ(cache.Lookup(0), "");
}

TEST(RouteTableCache, UpdateAndLookup) {
    RouteTableCache cache;
    RouteTable rt;
    rt.set_version(1);
    auto* e = rt.add_entries();
    e->set_section_id(5);
    e->set_allocsvr_addr("127.0.0.1:8001");

    EXPECT_TRUE(cache.Update(rt));
    EXPECT_EQ(cache.Lookup(5), "127.0.0.1:8001");
    EXPECT_EQ(cache.Version(), 1);
}

TEST(RouteTableCache, OlderVersionIgnored) {
    RouteTableCache cache;
    RouteTable rt;
    rt.set_version(5);
    auto* e = rt.add_entries();
    e->set_section_id(0);
    e->set_allocsvr_addr("a:1");
    cache.Update(rt);

    RouteTable old;
    old.set_version(3);
    auto* e2 = old.add_entries();
    e2->set_section_id(0);
    e2->set_allocsvr_addr("b:2");
    EXPECT_FALSE(cache.Update(old));
    EXPECT_EQ(cache.Lookup(0), "a:1");
}

TEST(RouteTableCache, ConcurrentReadsAreSafe) {
    RouteTableCache cache;
    RouteTable rt;
    rt.set_version(1);
    for (int i = 0; i < 100; i++) {
        auto* e = rt.add_entries();
        e->set_section_id(i);
        e->set_allocsvr_addr("x:" + std::to_string(i));
    }
    cache.Update(rt);

    std::vector<std::thread> threads;
    for (int t = 0; t < 10; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 100; i++) {
                EXPECT_EQ(cache.Lookup(i), "x:" + std::to_string(i));
            }
        });
    }
    for (auto& th : threads) th.join();
}
