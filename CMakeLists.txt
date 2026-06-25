cmake_minimum_required(VERSION 3.16)
project(seqsvr CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Protobuf REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(GTest REQUIRED)

find_library(BRPC_LIB brpc REQUIRED HINTS /usr/local/lib)
find_path(BRPC_INCLUDE brpc/server.h REQUIRED HINTS /usr/local/include)
find_library(ROCKSDB_LIB rocksdb REQUIRED)
find_library(GFLAGS_LIB gflags REQUIRED)
find_library(LEVELDB_LIB leveldb REQUIRED)

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${GENERATED_DIR})

add_custom_command(
    OUTPUT
        ${GENERATED_DIR}/seqsvr.pb.cc
        ${GENERATED_DIR}/seqsvr.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
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

add_subdirectory(src/common)
add_subdirectory(src/storesvr)
add_subdirectory(src/allocsvr)
add_subdirectory(src/client)
add_subdirectory(tests)
