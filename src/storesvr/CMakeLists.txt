add_library(storesvr_lib STATIC
    storage.cpp
    lease_manager.cpp
    store_service.cpp
)
target_include_directories(storesvr_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(storesvr_lib PUBLIC
    seqsvr_proto seqsvr_common
    ${ROCKSDB_LIB} ${BRPC_LINK_LIBS}
)

add_executable(storesvr main.cpp)
target_link_libraries(storesvr PRIVATE storesvr_lib)
