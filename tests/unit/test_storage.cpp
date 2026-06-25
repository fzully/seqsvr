#include <gtest/gtest.h>
#include <filesystem>
#include "storesvr/storage.h"

using namespace seqsvr;

class StorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = "/tmp/seqsvr_test_storage_" + std::to_string(getpid());
        std::filesystem::remove_all(path_);
        ASSERT_TRUE(storage_.Open(path_));
    }
    void TearDown() override {
        std::filesystem::remove_all(path_);
    }
    Storage storage_;
    std::string path_;
};

TEST_F(StorageTest, GetMaxSeqReturnsZeroIfNotFound) {
    EXPECT_EQ(storage_.GetMaxSeq(42), 0u);
}

TEST_F(StorageTest, SetAndGetMaxSeq) {
    ASSERT_TRUE(storage_.SetMaxSeq(1, 10000));
    EXPECT_EQ(storage_.GetMaxSeq(1), 10000u);
    ASSERT_TRUE(storage_.SetMaxSeq(1, 20000));
    EXPECT_EQ(storage_.GetMaxSeq(1), 20000u);
}

TEST_F(StorageTest, LeaseLifecycle) {
    LeaseRecord rec;
    rec.set_holder_addr("127.0.0.1:8100");
    rec.set_expire_time_ms(9999999);
    ASSERT_TRUE(storage_.SetLease(5, rec));
    auto got = storage_.GetLease(5);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->holder_addr(), "127.0.0.1:8100");
    ASSERT_TRUE(storage_.DeleteLease(5));
    EXPECT_FALSE(storage_.GetLease(5).has_value());
}

TEST_F(StorageTest, CleanExpiredLeases) {
    for (int i = 0; i < 5; i++) {
        LeaseRecord r;
        r.set_holder_addr("x");
        r.set_expire_time_ms(i < 3 ? 1 : 9999999999LL);
        storage_.SetLease(i, r);
    }
    int cleaned = storage_.CleanExpiredLeases(1000);
    EXPECT_EQ(cleaned, 3);
    EXPECT_FALSE(storage_.GetLease(0).has_value());
    EXPECT_TRUE(storage_.GetLease(4).has_value());
}

TEST_F(StorageTest, RouteTableRoundtrip) {
    EXPECT_FALSE(storage_.GetRouteTable().has_value());
    RouteTable rt;
    rt.set_version(7);
    auto* e = rt.add_entries();
    e->set_section_id(0);
    e->set_allocsvr_addr("10.0.0.1:9000");
    ASSERT_TRUE(storage_.SetRouteTable(rt));
    auto got = storage_.GetRouteTable();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->version(), 7);
    EXPECT_EQ(got->entries(0).allocsvr_addr(), "10.0.0.1:9000");
}
