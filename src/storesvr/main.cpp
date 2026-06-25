#include <gflags/gflags.h>
#include <brpc/server.h>
#include <butil/logging.h>
#include "store_service.h"

DEFINE_int32(port, 8200, "StoreSvr listening port");
DEFINE_string(db_path, "/tmp/seqsvr_store", "RocksDB data path");

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    seqsvr::Storage storage;
    if (!storage.Open(FLAGS_db_path)) {
        LOG(ERROR) << "Failed to open RocksDB at " << FLAGS_db_path;
        return 1;
    }
    seqsvr::LeaseManager lm(&storage);
    seqsvr::StoreServiceImpl service(&storage, &lm);

    brpc::Server server;
    if (server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add StoreService";
        return 1;
    }

    brpc::ServerOptions opts;
    opts.num_threads = 4;
    if (server.Start(FLAGS_port, &opts) != 0) {
        LOG(ERROR) << "Failed to start StoreSvr on port " << FLAGS_port;
        return 1;
    }
    LOG(INFO) << "StoreSvr started on port " << FLAGS_port;
    server.RunUntilAskedToQuit();
    return 0;
}
