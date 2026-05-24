# Runtime library: coroutine scheduler, thread pool, I/O multiplexing, timer
set(MIKU_RUNTIME_SOURCES
    miku_runtime.c
)

if(MIKU_LINUX)
    list(APPEND MIKU_RUNTIME_SOURCES miku_io_epoll.c)
elseif(MIKU_MACOS)
    list(APPEND MIKU_RUNTIME_SOURCES miku_io_kqueue.c)
elseif(MIKU_WINDOWS)
    list(APPEND MIKU_RUNTIME_SOURCES miku_io_iocp.c)
endif()

add_library(miku_runtime STATIC ${MIKU_RUNTIME_SOURCES})

target_include_directories(miku_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(miku_runtime PUBLIC
    miku_foundation
)
