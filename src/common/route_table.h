#pragma once
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include "seqsvr.pb.h"

namespace seqsvr {

class RouteTableCache {
public:
    RouteTableCache() = default;

    // Returns address of AllocSvr for given section_id, or "" if not found.
    std::string Lookup(uint32_t section_id) const {
        std::shared_lock lock(mu_);
        auto it = table_.find(section_id);
        return (it != table_.end()) ? it->second : "";
    }

    // Atomically update if new_version > current version.
    bool Update(const RouteTable& rt) {
        std::unique_lock lock(mu_);
        if (rt.version() <= version_) return false;
        version_ = rt.version();
        table_.clear();
        for (const auto& e : rt.entries()) {
            table_[e.section_id()] = e.allocsvr_addr();
        }
        return true;
    }

    int32_t Version() const {
        std::shared_lock lock(mu_);
        return version_;
    }

    void SetBootstrap(const std::string& addr) {
        std::unique_lock lock(mu_);
        bootstrap_addr_ = addr;
    }

    std::string Bootstrap() const {
        std::shared_lock lock(mu_);
        return bootstrap_addr_;
    }

private:
    mutable std::shared_mutex mu_;
    int32_t version_{-1};
    std::unordered_map<uint32_t, std::string> table_;
    std::string bootstrap_addr_;
};

}  // namespace seqsvr
