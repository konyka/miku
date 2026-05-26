set(MIKU_STORAGE_SOURCES
    miku_cache.c
    miku_mongo.c
    miku_redis.c
    miku_kafka.c
    miku_msg_store.c
    miku_session_cache.c
    miku_seq.c
    miku_incr_sync.c
)

add_library(miku_storage STATIC ${MIKU_STORAGE_SOURCES})

target_include_directories(miku_storage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/protocol
)

target_link_libraries(miku_storage PUBLIC
    miku_runtime
    miku_foundation
)

if(MIKU_ENABLE_MONGO)
    target_compile_definitions(miku_storage PUBLIC MIKU_HAS_MONGO=1)
    target_link_libraries(miku_storage PUBLIC PkgConfig::MONGOC)
endif()

if(MIKU_ENABLE_REDIS)
    target_compile_definitions(miku_storage PUBLIC MIKU_HAS_REDIS=1)
    target_link_libraries(miku_storage PUBLIC PkgConfig::HIREDIS)
endif()

if(MIKU_ENABLE_KAFKA)
    target_compile_definitions(miku_storage PUBLIC MIKU_HAS_KAFKA=1)
    target_link_libraries(miku_storage PUBLIC PkgConfig::RDKAFKA)
endif()
