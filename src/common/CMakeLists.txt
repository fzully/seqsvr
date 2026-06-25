add_library(seqsvr_common INTERFACE)
target_include_directories(seqsvr_common INTERFACE
    ${CMAKE_SOURCE_DIR}/src
    ${GENERATED_DIR}
    ${Protobuf_INCLUDE_DIRS}
    ${BRPC_INCLUDE}
)
