# Data models
set(MIKU_MODELS_SOURCES
    miku_models.c
)

add_library(miku_models STATIC ${MIKU_MODELS_SOURCES})

target_include_directories(miku_models PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(miku_models PUBLIC
    miku_protocol
    miku_foundation
)
