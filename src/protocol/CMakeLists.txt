# Protocol library: HTTP parser, WebSocket, RPC framework, Protobuf, JSON
set(MIKU_PROTOCOL_SOURCES
    miku_protocol.c
)

add_library(miku_protocol STATIC ${MIKU_PROTOCOL_SOURCES})

target_include_directories(miku_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(miku_protocol PUBLIC
    miku_runtime
    miku_foundation
)
