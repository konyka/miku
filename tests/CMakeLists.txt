add_executable(miku_tests test_foundation.c test_runtime.c test_protocol.c test_storage.c)

target_include_directories(miku_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/foundation
    ${CMAKE_SOURCE_DIR}/src/runtime
    ${CMAKE_SOURCE_DIR}/src/protocol
    ${CMAKE_SOURCE_DIR}/src/storage
    ${CMAKE_SOURCE_DIR}/src/discovery
)

target_link_libraries(miku_tests PRIVATE
    miku_protocol
    miku_storage
    miku_discovery
    miku_runtime
    miku_foundation
)

add_test(NAME foundation COMMAND miku_tests)
add_test(NAME runtime  COMMAND miku_tests)
