# Service executables

# Common link libraries for all services
set(MIKU_SERVICE_LIBS
    miku_gateway
    miku_services
    miku_protocol
    miku_storage
    miku_discovery
    miku_models
    miku_runtime
    miku_foundation
)

include_directories(
    ${CMAKE_SOURCE_DIR}/src/gateway/api
    ${CMAKE_SOURCE_DIR}/src/gateway/msggateway
    ${CMAKE_SOURCE_DIR}/src/gateway/msgtransfer
    ${CMAKE_SOURCE_DIR}/src/gateway/push
    ${CMAKE_SOURCE_DIR}/src/gateway/crontask
    ${CMAKE_SOURCE_DIR}/src/services/auth
    ${CMAKE_SOURCE_DIR}/src/services/user
    ${CMAKE_SOURCE_DIR}/src/services/friend
    ${CMAKE_SOURCE_DIR}/src/services/group
    ${CMAKE_SOURCE_DIR}/src/services/conversation
    ${CMAKE_SOURCE_DIR}/src/services/msg
    ${CMAKE_SOURCE_DIR}/src/services/third
    ${CMAKE_SOURCE_DIR}/src/models
    ${CMAKE_SOURCE_DIR}/src/protocol
)

# ── HTTP API Gateway ────────────────────────────────────────
add_executable(miku-api miku-api/main.c)
target_link_libraries(miku-api PRIVATE ${MIKU_SERVICE_LIBS})

# ── WebSocket Message Gateway ───────────────────────────────
add_executable(miku-msggateway miku-msggateway/main.c)
target_link_libraries(miku-msggateway PRIVATE ${MIKU_SERVICE_LIBS})

# ── Message Transfer Service ────────────────────────────────
add_executable(miku-msgtransfer miku-msgtransfer/main.c)
target_link_libraries(miku-msgtransfer PRIVATE ${MIKU_SERVICE_LIBS})

# ── Push Notification Service ───────────────────────────────
add_executable(miku-push miku-push/main.c)
target_link_libraries(miku-push PRIVATE ${MIKU_SERVICE_LIBS})

# ── Cron Task Service ──────────────────────────────────────
add_executable(miku-crontask miku-crontask/main.c)
target_link_libraries(miku-crontask PRIVATE ${MIKU_SERVICE_LIBS})

# ── RPC: Auth ──────────────────────────────────────────────
add_executable(miku-rpc-auth miku-rpc-auth/main.c)
target_link_libraries(miku-rpc-auth PRIVATE ${MIKU_SERVICE_LIBS})

# ── RPC: User ──────────────────────────────────────────────
add_executable(miku-rpc-user miku-rpc-user/main.c)
target_link_libraries(miku-rpc-user PRIVATE ${MIKU_SERVICE_LIBS})

# ── RPC: Friend ────────────────────────────────────────────
add_executable(miku-rpc-friend miku-rpc-friend/main.c)
target_link_libraries(miku-rpc-friend PRIVATE ${MIKU_SERVICE_LIBS})

# ── RPC: Group ─────────────────────────────────────────────
add_executable(miku-rpc-group miku-rpc-group/main.c)
target_link_libraries(miku-rpc-group PRIVATE ${MIKU_SERVICE_LIBS})

# ── RPC: Conversation ──────────────────────────────────────
add_executable(miku-rpc-conversation miku-rpc-conversation/main.c)
target_link_libraries(miku-rpc-conversation PRIVATE ${MIKU_SERVICE_LIBS})

# ── RPC: Message ───────────────────────────────────────────
add_executable(miku-rpc-msg miku-rpc-msg/main.c)
target_link_libraries(miku-rpc-msg PRIVATE ${MIKU_SERVICE_LIBS})

# ── RPC: Third Party ───────────────────────────────────────
add_executable(miku-rpc-third miku-rpc-third/main.c)
target_link_libraries(miku-rpc-third PRIVATE ${MIKU_SERVICE_LIBS})

add_executable(miku-dev miku-dev/main.c)
target_link_libraries(miku-dev PRIVATE ${MIKU_SERVICE_LIBS})
