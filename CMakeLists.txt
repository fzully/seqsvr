cmake_minimum_required(VERSION 3.16)
project(seqsvr CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Prefer devlibs (local installs without sudo) then system paths
set(DEVLIBS_ROOT "$ENV{HOME}/devlibs")
list(PREPEND CMAKE_PREFIX_PATH "${DEVLIBS_ROOT}/usr/local" "${DEVLIBS_ROOT}/usr")
list(PREPEND CMAKE_LIBRARY_PATH
    "${DEVLIBS_ROOT}/usr/local/lib"
    "${DEVLIBS_ROOT}/usr/lib/x86_64-linux-gnu"
    "${DEVLIBS_ROOT}/usr/lib")
list(PREPEND CMAKE_INCLUDE_PATH
    "${DEVLIBS_ROOT}/usr/local/include"
    "${DEVLIBS_ROOT}/usr/include")

find_package(Protobuf REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(GTest REQUIRED)

find_library(BRPC_LIB brpc REQUIRED
    HINTS "${DEVLIBS_ROOT}/usr/local/lib" /usr/local/lib)
find_path(BRPC_INCLUDE brpc/server.h REQUIRED
    HINTS "${DEVLIBS_ROOT}/usr/local/include" /usr/local/include)
find_library(ROCKSDB_LIB rocksdb REQUIRED
    HINTS "${DEVLIBS_ROOT}/usr/lib/x86_64-linux-gnu" /usr/lib/x86_64-linux-gnu)
find_library(GFLAGS_LIB gflags REQUIRED
    HINTS "${DEVLIBS_ROOT}/usr/lib/x86_64-linux-gnu" /usr/lib/x86_64-linux-gnu)
find_library(LEVELDB_LIB leveldb REQUIRED
    HINTS "${DEVLIBS_ROOT}/usr/lib/x86_64-linux-gnu" /usr/lib/x86_64-linux-gnu)

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(DEVLIBS_LIB "${DEVLIBS_ROOT}/usr/lib/x86_64-linux-gnu")
add_custom_command(
    OUTPUT
        ${GENERATED_DIR}/seqsvr.pb.cc
        ${GENERATED_DIR}/seqsvr.pb.h
    COMMAND env
        "LD_LIBRARY_PATH=${DEVLIBS_LIB}:$ENV{LD_LIBRARY_PATH}"
        ${Protobuf_PROTOC_EXECUTABLE}
        --cpp_out=${GENERATED_DIR}
        -I${CMAKE_SOURCE_DIR}/proto
        ${CMAKE_SOURCE_DIR}/proto/seqsvr.proto
    DEPENDS ${CMAKE_SOURCE_DIR}/proto/seqsvr.proto
    VERBATIM
)

add_library(seqsvr_proto STATIC ${GENERATED_DIR}/seqsvr.pb.cc)
target_include_directories(seqsvr_proto PUBLIC
    ${GENERATED_DIR}
    ${Protobuf_INCLUDE_DIRS}
    ${BRPC_INCLUDE}
)
target_link_libraries(seqsvr_proto PUBLIC ${Protobuf_LIBRARIES})

set(BRPC_LINK_LIBS
    ${BRPC_LIB}
    ${LEVELDB_LIB}
    ${GFLAGS_LIB}
    OpenSSL::SSL
    OpenSSL::Crypto
    ${Protobuf_LIBRARIES}
    Threads::Threads
    dl z
)

enable_testing()

add_subdirectory(src/common)
add_subdirectory(src/storesvr)
add_subdirectory(src/allocsvr)
add_subdirectory(src/client)
add_subdirectory(tests)
