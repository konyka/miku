set(MIKU_TEST_LIBS
    miku_gateway
    miku_services
    miku_models
    miku_protocol
    miku_storage
    miku_discovery
    miku_runtime
    miku_foundation
)

set(MIKU_TEST_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/foundation
    ${CMAKE_SOURCE_DIR}/src/runtime
    ${CMAKE_SOURCE_DIR}/src/protocol
    ${CMAKE_SOURCE_DIR}/src/storage
    ${CMAKE_SOURCE_DIR}/src/discovery
    ${CMAKE_SOURCE_DIR}/src/models
    ${CMAKE_SOURCE_DIR}/src/services/auth
    ${CMAKE_SOURCE_DIR}/src/services/user
    ${CMAKE_SOURCE_DIR}/src/services/friend
    ${CMAKE_SOURCE_DIR}/src/services/group
    ${CMAKE_SOURCE_DIR}/src/services/conversation
    ${CMAKE_SOURCE_DIR}/src/services/msg
    ${CMAKE_SOURCE_DIR}/src/services/third
    ${CMAKE_SOURCE_DIR}/src/gateway/api
    ${CMAKE_SOURCE_DIR}/src/gateway/msggateway
    ${CMAKE_SOURCE_DIR}/src/gateway/msgtransfer
    ${CMAKE_SOURCE_DIR}/src/gateway/push
    ${CMAKE_SOURCE_DIR}/src/gateway/crontask
)

add_executable(miku_tests test_foundation.c test_runtime.c test_protocol.c test_storage.c test_services.c test_new_modules.c)

target_include_directories(miku_tests PRIVATE ${MIKU_TEST_INCLUDES})

target_link_libraries(miku_tests PRIVATE ${MIKU_TEST_LIBS})

add_test(NAME miku_tests COMMAND miku_tests)
set_tests_properties(miku_tests PROPERTIES LABELS "functional" TIMEOUT 60)

if(MIKU_ENABLE_BENCHMARKS)
    add_executable(miku_bench test_benchmark_main.c test_benchmark.c)
    target_include_directories(miku_bench PRIVATE ${MIKU_TEST_INCLUDES})
    target_link_libraries(miku_bench PRIVATE ${MIKU_TEST_LIBS})
    add_test(NAME miku_benchmarks COMMAND miku_bench)
    set_tests_properties(miku_benchmarks PROPERTIES LABELS "benchmark" TIMEOUT 30)
endif()
