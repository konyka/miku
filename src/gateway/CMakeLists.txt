# Gateway services
set(MIKU_GATEWAY_SOURCES
    api/miku_api.c
    msggateway/miku_msggateway.c
    msgtransfer/miku_msgtransfer.c
    push/miku_push.c
    crontask/miku_crontask.c
)

add_library(miku_gateway STATIC ${MIKU_GATEWAY_SOURCES})

target_include_directories(miku_gateway PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/protocol
    ${CMAKE_SOURCE_DIR}/src/models
    ${CMAKE_SOURCE_DIR}/src/services/auth
    ${CMAKE_SOURCE_DIR}/src/services/user
    ${CMAKE_SOURCE_DIR}/src/services/friend
    ${CMAKE_SOURCE_DIR}/src/services/group
    ${CMAKE_SOURCE_DIR}/src/services/conversation
    ${CMAKE_SOURCE_DIR}/src/services/msg
    ${CMAKE_SOURCE_DIR}/src/services/third
    ${CMAKE_CURRENT_SOURCE_DIR}/api
    ${CMAKE_CURRENT_SOURCE_DIR}/msggateway
)

target_link_libraries(miku_gateway PUBLIC
    miku_services
    miku_protocol
    miku_storage
    miku_discovery
    miku_runtime
    miku_foundation
)
