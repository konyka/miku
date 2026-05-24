add_executable(miku_tests test_foundation.c)

target_include_directories(miku_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/foundation
)

target_link_libraries(miku_tests PRIVATE
    miku_foundation
)

add_test(NAME foundation COMMAND miku_tests)
