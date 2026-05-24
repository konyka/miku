# RPC Services
set(MIKU_SERVICES_SOURCES
    auth/miku_auth.c
    user/miku_user.c
    friend/miku_friend.c
    group/miku_group.c
    conversation/miku_conversation.c
    msg/miku_msg.c
    third/miku_third.c
)

add_library(miku_services STATIC ${MIKU_SERVICES_SOURCES})

target_include_directories(miku_services PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/protocol
    ${CMAKE_SOURCE_DIR}/src/models
    ${CMAKE_SOURCE_DIR}/src/services/auth
    ${CMAKE_SOURCE_DIR}/src/services/user
    ${CMAKE_SOURCE_DIR}/src/services/friend
    ${CMAKE_SOURCE_DIR}/src/services/group
    ${CMAKE_CURRENT_SOURCE_DIR}/conversation
    ${CMAKE_CURRENT_SOURCE_DIR}/msg
    ${CMAKE_CURRENT_SOURCE_DIR}/third
)

target_link_libraries(miku_services PUBLIC
    miku_protocol
    miku_storage
    miku_models
    miku_discovery
    miku_runtime
    miku_foundation
)
