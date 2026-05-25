# Foundation library: memory, data structures, logging, config, utilities
set(MIKU_FOUNDATION_SOURCES
    miku_arena.c
    miku_slab.c
    miku_memory.c
    miku_log.c
    miku_config.c
    miku_error.c
    miku_string.c
    miku_hashmap.c
    miku_rbtree.c
    miku_thread.c
    miku_uuid.c
    miku_crc32.c
    miku_base64.c
    miku_sha1.c
    miku_service_config.c
    miku_graceful.c
    miku_stats.c
)

add_library(miku_foundation STATIC ${MIKU_FOUNDATION_SOURCES})

target_include_directories(miku_foundation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(miku_foundation PUBLIC
    miku_compiler_flags
    Threads::Threads
)

# Platform-specific libraries
if(MIKU_LINUX)
    target_link_libraries(miku_foundation PUBLIC rt)
endif()

if(ZLIB_FOUND)
    target_link_libraries(miku_foundation PUBLIC ZLIB::ZLIB)
    target_compile_definitions(miku_foundation PUBLIC MIKU_HAS_ZLIB=1)
endif()
