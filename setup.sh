#!/bin/bash
set -e

echo "=== Installing apt dependencies ==="
sudo apt-get update -q
sudo apt-get install -y \
    cmake git build-essential \
    libprotobuf-dev protobuf-compiler \
    libgflags-dev libleveldb-dev \
    libssl-dev libgtest-dev \
    librocksdb-dev \
    libsnappy-dev libbz2-dev zlib1g-dev liblz4-dev libzstd-dev

echo "=== Building brpc from source ==="
BRPC_DIR=/tmp/brpc_build
if [ ! -d "$BRPC_DIR" ]; then
    git clone --depth=1 https://github.com/apache/brpc.git "$BRPC_DIR"
fi
cd "$BRPC_DIR"
mkdir -p build && cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DWITH_GLOG=OFF \
    -DBUILD_SHARED_LIBS=ON
make -j$(nproc)
sudo make install
sudo ldconfig

echo "=== Setup complete: brpc installed at /usr/local ==="
