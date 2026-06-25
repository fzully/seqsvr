enable_testing()

add_executable(test_section_state unit/test_section_state.cpp)
target_link_libraries(test_section_state PRIVATE allocsvr_lib GTest::GTest GTest::Main)
add_test(NAME test_section_state COMMAND test_section_state)

add_executable(test_storage unit/test_storage.cpp)
target_link_libraries(test_storage PRIVATE storesvr_lib GTest::GTest GTest::Main)
add_test(NAME test_storage COMMAND test_storage)

add_executable(test_route_table unit/test_route_table.cpp)
target_link_libraries(test_route_table PRIVATE seqsvr_client GTest::GTest GTest::Main)
add_test(NAME test_route_table COMMAND test_route_table)

add_executable(test_full_flow integration/test_full_flow.cpp)
target_link_libraries(test_full_flow PRIVATE
    storesvr_lib allocsvr_lib seqsvr_client GTest::GTest GTest::Main)
add_test(NAME test_full_flow COMMAND test_full_flow)

add_executable(bench_client perf/bench_client.cpp)
target_link_libraries(bench_client PRIVATE seqsvr_client allocsvr_lib storesvr_lib)
