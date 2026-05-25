# Protocol library: HTTP parser, WebSocket, RPC framework, Protobuf, JSON
set(MIKU_PROTOCOL_SOURCES
    miku_http.c
    miku_http_server.c
    miku_json.c
    miku_websocket.c
    miku_rpc.c
    miku_rpc_server.c
    miku_pb.c
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
