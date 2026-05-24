# Storage library: MongoDB, Redis, Kafka, local cache, S3
set(MIKU_STORAGE_SOURCES
    miku_storage.c
)

add_library(miku_storage STATIC ${MIKU_STORAGE_SOURCES})

target_include_directories(miku_storage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(miku_storage PUBLIC
    miku_runtime
    miku_foundation
)


