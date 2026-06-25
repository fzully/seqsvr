#include "storage.h"
#include <cstring>
#include <rocksdb/db.h>
#include <rocksdb/options.h>

namespace seqsvr {

Storage::~Storage() {
    delete db_;
}

bool Storage::Open(const std::string& db_path) {
    rocksdb::Options opts;
    opts.create_if_missing = true;
    auto s = rocksdb::DB::Open(opts, db_path, &db_);
    return s.ok();
}

std::string Storage::MaxSeqKey(uint32_t section_id) {
    return "maxseq:" + std::to_string(section_id);
}

std::string Storage::LeaseKey(uint32_t section_id) {
    return "lease:" + std::to_string(section_id);
}

uint64_t Storage::GetMaxSeq(uint32_t section_id) {
    std::string val;
    auto s = db_->Get(rocksdb::ReadOptions(), MaxSeqKey(section_id), &val);
    if (!s.ok()) return 0;
    uint64_t seq = 0;
    if (val.size() == sizeof(uint64_t)) {
        memcpy(&seq, val.data(), sizeof(uint64_t));
    }
    return seq;
}

bool Storage::SetMaxSeq(uint32_t section_id, uint64_t max_seq) {
    std::string val(reinterpret_cast<const char*>(&max_seq), sizeof(uint64_t));
    auto s = db_->Put(rocksdb::WriteOptions(), MaxSeqKey(section_id), val);
    return s.ok();
}

std::optional<LeaseRecord> Storage::GetLease(uint32_t section_id) {
    std::string val;
    auto s = db_->Get(rocksdb::ReadOptions(), LeaseKey(section_id), &val);
    if (!s.ok()) return std::nullopt;
    LeaseRecord rec;
    if (!rec.ParseFromString(val)) return std::nullopt;
    return rec;
}

bool Storage::SetLease(uint32_t section_id, const LeaseRecord& rec) {
    std::string val;
    if (!rec.SerializeToString(&val)) return false;
    return db_->Put(rocksdb::WriteOptions(), LeaseKey(section_id), val).ok();
}

bool Storage::DeleteLease(uint32_t section_id) {
    return db_->Delete(rocksdb::WriteOptions(), LeaseKey(section_id)).ok();
}

std::optional<RouteTable> Storage::GetRouteTable() {
    std::string val;
    auto s = db_->Get(rocksdb::ReadOptions(), kRouteTableKey, &val);
    if (!s.ok()) return std::nullopt;
    RouteTable rt;
    if (!rt.ParseFromString(val)) return std::nullopt;
    return rt;
}

bool Storage::SetRouteTable(const RouteTable& rt) {
    std::string val;
    if (!rt.SerializeToString(&val)) return false;
    return db_->Put(rocksdb::WriteOptions(), kRouteTableKey, val).ok();
}

int Storage::CleanExpiredLeases(int64_t now_ms) {
    int count = 0;
    rocksdb::Iterator* it = db_->NewIterator(rocksdb::ReadOptions());
    std::string prefix = "lease:";
    std::vector<std::string> to_delete;
    for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix); it->Next()) {
        LeaseRecord rec;
        if (rec.ParseFromString(it->value().ToString()) && rec.expire_time_ms() < now_ms) {
            to_delete.push_back(it->key().ToString());
        }
    }
    delete it;
    for (const auto& key : to_delete) {
        if (db_->Delete(rocksdb::WriteOptions(), key).ok()) count++;
    }
    return count;
}

}  // namespace seqsvr
