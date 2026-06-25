add_library(seqsvr_client STATIC seq_client.cpp)
target_include_directories(seqsvr_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(seqsvr_client PUBLIC
    seqsvr_proto seqsvr_common ${BRPC_LINK_LIBS}
)
