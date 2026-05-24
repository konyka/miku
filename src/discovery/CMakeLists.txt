# Service discovery: etcd client
set(MIKU_DISCOVERY_SOURCES
    miku_discovery.c
)

add_library(miku_discovery STATIC ${MIKU_DISCOVERY_SOURCES})

target_include_directories(miku_discovery PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(miku_discovery PUBLIC
    miku_runtime
    miku_foundation
)


