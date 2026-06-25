add_library(allocsvr_lib STATIC
    section_state.cpp
    nrw_coordinator.cpp
    lease_holder.cpp
    alloc_service.cpp
)
target_include_directories(allocsvr_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(allocsvr_lib PUBLIC
    seqsvr_proto seqsvr_common ${BRPC_LINK_LIBS}
)

add_executable(allocsvr main.cpp)
target_link_libraries(allocsvr PRIVATE allocsvr_lib)
