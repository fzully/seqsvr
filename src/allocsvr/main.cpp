#include <gflags/gflags.h>
#include <brpc/server.h>
#include <butil/logging.h>
#include <sstream>
#include "section_state.h"
#include "nrw_coordinator.h"
#include "lease_holder.h"
#include "alloc_service.h"
#include "common/route_table.h"

DEFINE_int32(port, 9100, "AllocSvr listening port");
DEFINE_string(store_addrs, "", "Comma-separated StoreSvr addresses, e.g. 127.0.0.1:8200,127.0.0.1:8201,127.0.0.1:8202");
DEFINE_string(sections, "0", "Comma-separated section IDs to own");
DEFINE_string(my_addr, "", "This node's address (ip:port), auto-detected if empty");
DEFINE_uint64(step, 10000, "Seqno step for normal sections");
DEFINE_uint64(global_step, 1000, "Seqno step for global-key sections");

static std::vector<std::string> Split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) if (!item.empty()) out.push_back(item);
    return out;
}

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    auto store_list      = Split(FLAGS_store_addrs, ',');
    auto section_ids_str = Split(FLAGS_sections, ',');

    if (store_list.size() < 3) {
        LOG(ERROR) << "--store_addrs must have 3 addresses";
        return 1;
    }

    std::vector<uint32_t> section_ids;
    for (const auto& s : section_ids_str) {
        section_ids.push_back(std::stoul(s));
    }

    std::unordered_map<uint32_t, seqsvr::SectionState> sections;
    for (uint32_t sid : section_ids) {
        sections.emplace(std::piecewise_construct,
                         std::forward_as_tuple(sid),
                         std::forward_as_tuple(sid, FLAGS_step));
    }

    seqsvr::NRWCoordinator nrw(store_list);
    seqsvr::RouteTableCache route_cache;

    std::string my_addr = FLAGS_my_addr;
    if (my_addr.empty()) my_addr = "127.0.0.1:" + std::to_string(FLAGS_port);

    seqsvr::LeaseHolder lh(&nrw, &sections);
    if (!lh.Start(section_ids, my_addr)) {
        LOG(ERROR) << "Failed to acquire leases";
        return 1;
    }

    seqsvr::AllocServiceImpl service(&sections, &nrw, &route_cache);
    brpc::Server server;
    if (server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        return 1;
    }

    brpc::ServerOptions opts;
    opts.num_threads = 8;
    if (server.Start(FLAGS_port, &opts) != 0) {
        LOG(ERROR) << "Failed to start AllocSvr on port " << FLAGS_port;
        return 1;
    }
    LOG(INFO) << "AllocSvr started on port " << FLAGS_port;
    server.RunUntilAskedToQuit();
    lh.Stop();
    return 0;
}
