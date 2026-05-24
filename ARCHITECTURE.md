# Miku IM Server - Architecture Design Document

> High-performance, high-throughput, distributed IM server in pure C (C99-C23 compatible)
> Rewriting OpenIM Server (Go, 47K LOC, 12 microservices) with memory pool, thread pool, coroutines, and cross-platform support.

## 1. Overview

### 1.1 Source Architecture (Go/OpenIM)
- **12 microservices** communicating via gRPC
- **Infrastructure**: MongoDB, Redis, Kafka, etcd, MinIO/S3, Prometheus
- **100+ HTTP API endpoints** via Gin framework
- **WebSocket gateway** for real-time messaging
- **Kafka-based** message transfer pipeline (Redis → MongoDB)

### 1.2 Target Architecture (C/Miku)
- **Same 12 microservices** as separate executables
- **Custom binary RPC** instead of gRPC (lighter, faster)
- **C native libraries**: mongoc, hiredis, librdkafka, libyaml, zlib, libcurl
- **ucontext stackful coroutines** for async I/O
- **Work-stealing thread pool** for CPU-bound tasks
- **Cross-platform I/O**: epoll (Linux), kqueue (macOS), IOCP (Windows)

---

## 2. Layered Architecture

```
┌─────────────────────────────────────────────────────┐
│                  Application Layer                    │
│  API Gateway | WS Gateway | MsgTransfer | Push | ... │
├─────────────────────────────────────────────────────┤
│                   Service Layer                       │
│  Auth | User | Friend | Group | Conv | Msg | Third   │
├─────────────────────────────────────────────────────┤
│                  Protocol Layer                       │
│  HTTP Parser | WebSocket | Binary RPC | Protobuf     │
├─────────────────────────────────────────────────────┤
│                  Data Access Layer                    │
│  MongoDB | Redis | Kafka | LocalCache | S3/MinIO     │
├─────────────────────────────────────────────────────┤
│               Concurrency Runtime                     │
│  Coroutine Scheduler | Thread Pool | IO Multiplexer  │
├─────────────────────────────────────────────────────┤
│                 Foundation Layer                      │
│  Memory Pool | Logger | Config | Error | RBTree      │
│  String Buffer | Hash Map | List | Atomic | Spinlock │
└─────────────────────────────────────────────────────┘
```

---

## 3. Project Directory Structure

```
miku/
├── CMakeLists.txt                    # Root CMake
├── Makefile                          # Convenience wrapper
├── ARCHITECTURE.md                   # This file
├── LICENSE
├── README.md
├── notes.html                        # Development notes
│
├── build/                            # Build output (gitignored)
├── config/                           # Configuration files
│   ├── share.yml
│   ├── mongodb.yml
│   ├── redis.yml
│   ├── kafka.yml
│   ├── discovery.yml
│   ├── openim-api.yml
│   ├── openim-msggateway.yml
│   ├── openim-msgtransfer.yml
│   ├── openim-push.yml
│   ├── openim-crontask.yml
│   ├── openim-rpc-auth.yml
│   ├── openim-rpc-user.yml
│   ├── openim-rpc-friend.yml
│   ├── openim-rpc-group.yml
│   ├── openim-rpc-conversation.yml
│   ├── openim-rpc-msg.yml
│   └── openim-rpc-third.yml
│
├── src/
│   ├── foundation/                   # Foundation layer
│   │   ├── CMakeLists.txt
│   │   ├── miku_memory.h             # Memory pool API (Arena + Slab)
│   │   ├── miku_memory.c
│   │   ├── miku_arena.h              # Arena allocator
│   │   ├── miku_arena.c
│   │   ├── miku_slab.h               # Slab allocator
│   │   ├── miku_slab.c
│   │   ├── miku_log.h                # Logging system
│   │   ├── miku_log.c
│   │   ├── miku_config.h             # YAML config parser
│   │   ├── miku_config.c
│   │   ├── miku_error.h              # Error handling
│   │   ├── miku_error.c
│   │   ├── miku_string.h             # String buffer (sds-like)
│   │   ├── miku_string.c
│   │   ├── miku_hashmap.h            # Hash map
│   │   ├── miku_hashmap.c
│   │   ├── miku_list.h               # Intrusive doubly-linked list
│   │   ├── miku_rbtree.h             # Red-black tree (timer/ordered structures)
│   │   ├── miku_rbtree.c
│   │   ├── miku_atomic.h             # Atomic operations (C11 + fallback)
│   │   ├── miku_spinlock.h           # Spinlock
│   │   ├── miku_thread.h             # Thread abstraction
│   │   ├── miku_thread.c
│   │   ├── miku_uuid.h               # UUID generation
│   │   ├── miku_uuid.c
│   │   ├── miku_crc32.h              # CRC32 checksum
│   │   ├── miku_crc32.c
│   │   └── miku_base64.h             # Base64 encode/decode
│   │       miku_base64.c
│   │
│   ├── runtime/                      # Concurrency runtime
│   │   ├── CMakeLists.txt
│   │   ├── miku_coroutine.h          # Coroutine API (ucontext)
│   │   ├── miku_coroutine.c
│   │   ├── miku_scheduler.h          # Coroutine scheduler
│   │   ├── miku_scheduler.c
│   │   ├── miku_threadpool.h         # Work-stealing thread pool
│   │   ├── miku_threadpool.c
│   │   ├── miku_io.h                 # I/O multiplexing abstraction
│   │   ├── miku_io_epoll.c           # Linux epoll backend
│   │   ├── miku_io_kqueue.c          # macOS kqueue backend
│   │   ├── miku_io_iocp.c            # Windows IOCP backend
│   │   ├── miku_channel.h            # Coroutine channel (like Go chan)
│   │   ├── miku_channel.c
│   │   ├── miku_timer.h              # Timer wheel / heap
│   │   ├── miku_timer.c
│   │   └── miku_async.h              # Async/await helpers
│   │       miku_async.c
│   │
│   ├── protocol/                     # Protocol layer
│   │   ├── CMakeLists.txt
│   │   ├── miku_http.h               # HTTP/1.1 parser
│   │   ├── miku_http.c
│   │   ├── miku_http_server.h        # HTTP server
│   │   ├── miku_http_server.c
│   │   ├── miku_websocket.h          # WebSocket (RFC 6455)
│   │   ├── miku_websocket.c
│   │   ├── miku_rpc.h                # Binary RPC framework
│   │   ├── miku_rpc.c
│   │   ├── miku_pb.h                 # Protocol Buffers encoder/decoder
│   │   ├── miku_pb.c
│   │   ├── miku_json.h               # JSON parser (lightweight)
│   │   └── miku_json.c
│   │
│   ├── storage/                      # Data access layer
│   │   ├── CMakeLists.txt
│   │   ├── miku_mongo.h              # MongoDB driver wrapper
│   │   ├── miku_mongo.c
│   │   ├── miku_redis.h              # Redis async client wrapper
│   │   ├── miku_redis.c
│   │   ├── miku_kafka.h              # Kafka producer/consumer
│   │   ├── miku_kafka.c
│   │   ├── miku_cache.h              # Local cache (LRU + TTL)
│   │   ├── miku_cache.c
│   │   ├── miku_s3.h                 # S3/MinIO object storage
│   │   └── miku_s3.c
│   │
│   ├── discovery/                    # Service discovery
│   │   ├── CMakeLists.txt
│   │   ├── miku_etcd.h               # etcd client (HTTP API)
│   │   ├── miku_etcd.c
│   │   ├── miku_registry.h           # Service registry interface
│   │   └── miku_registry.c
│   │
│   ├── models/                       # Data models
│   │   ├── CMakeLists.txt
│   │   ├── miku_model_user.h
│   │   ├── miku_model_user.c
│   │   ├── miku_model_friend.h
│   │   ├── miku_model_friend.c
│   │   ├── miku_model_group.h
│   │   ├── miku_model_group.c
│   │   ├── miku_model_msg.h
│   │   ├── miku_model_msg.c
│   │   ├── miku_model_conversation.h
│   │   ├── miku_model_conversation.c
│   │   ├── miku_model_seq.h
│   │   ├── miku_model_seq.c
│   │   ├── miku_model_object.h
│   │   └── miku_model_object.c
│   │
│   ├── services/                     # Service layer (7 RPC services)
│   │   ├── CMakeLists.txt
│   │   ├── auth/
│   │   │   ├── miku_auth.h
│   │   │   └── miku_auth.c
│   │   ├── user/
│   │   │   ├── miku_user.h
│   │   │   └── miku_user.c
│   │   ├── friend/
│   │   │   ├── miku_friend.h
│   │   │   └── miku_friend.c
│   │   ├── group/
│   │   │   ├── miku_group.h
│   │   │   └── miku_group.c
│   │   ├── conversation/
│   │   │   ├── miku_conversation.h
│   │   │   └── miku_conversation.c
│   │   ├── msg/
│   │   │   ├── miku_msg.h
│   │   │   └── miku_msg.c
│   │   └── third/
│   │       ├── miku_third.h
│   │       └── miku_third.c
│   │
│   └── gateway/                      # Gateway services
│       ├── CMakeLists.txt
│       ├── api/                      # HTTP API gateway
│       │   ├── miku_api.h
│       │   ├── miku_api.c
│       │   ├── miku_api_user.c
│       │   ├── miku_api_friend.c
│       │   ├── miku_api_group.c
│       │   ├── miku_api_auth.c
│       │   ├── miku_api_msg.c
│       │   ├── miku_api_conversation.c
│       │   └── miku_api_third.c
│       ├── msggateway/               # WebSocket gateway
│       │   ├── miku_ws.h
│       │   ├── miku_ws.c
│       │   ├── miku_ws_client.h
│       │   └── miku_ws_client.c
│       ├── msgtransfer/              # Kafka → MongoDB transfer
│       │   ├── miku_transfer.h
│       │   └── miku_transfer.c
│       ├── push/                     # Push notification service
│       │   ├── miku_push.h
│       │   └── miku_push.c
│       └── crontask/                 # Scheduled tasks
│           ├── miku_cron.h
│           └── miku_cron.c
│
├── cmd/                              # Service entry points
│   ├── CMakeLists.txt
│   ├── miku-api/                     # HTTP API server
│   │   └── main.c
│   ├── miku-msggateway/              # WebSocket gateway
│   │   └── main.c
│   ├── miku-msgtransfer/             # Message transfer
│   │   └── main.c
│   ├── miku-push/                    # Push service
│   │   └── main.c
│   ├── miku-crontask/                # Cron task service
│   │   └── main.c
│   ├── miku-rpc-auth/               # Auth RPC service
│   │   └── main.c
│   ├── miku-rpc-user/               # User RPC service
│   │   └── main.c
│   ├── miku-rpc-friend/             # Friend RPC service
│   │   └── main.c
│   ├── miku-rpc-group/              # Group RPC service
│   │   └── main.c
│   ├── miku-rpc-conversation/        # Conversation RPC service
│   │   └── main.c
│   ├── miku-rpc-msg/                # Message RPC service
│   │   └── main.c
│   └── miku-rpc-third/              # Third-party RPC service
│       └── main.c
│
├── tests/                            # Test suite
│   ├── CMakeLists.txt
│   ├── test_memory.c
│   ├── test_arena.c
│   ├── test_slab.c
│   ├── test_coroutine.c
│   ├── test_threadpool.c
│   ├── test_io.c
│   ├── test_http.c
│   ├── test_websocket.c
│   ├── test_rpc.c
│   ├── test_mongo.c
│   ├── test_redis.c
│   ├── test_kafka.c
│   ├── test_config.c
│   ├── test_json.c
│   ├── test_hashmap.c
│   ├── test_rbtree.c
│   └── test_integration.c
│
├── docs/                             # Documentation
│   ├── api.md                        # API reference
│   ├── architecture.md               # Architecture deep-dive
│   ├── deployment.md                 # Deployment guide
│   └── development.md                # Developer guide
│
├── scripts/                          # Build/deploy scripts
│   ├── bootstrap.sh
│   ├── build.sh
│   └── test.sh
│
└── third_party/                      # Vendored dependencies (optional)
    ├── CMakeLists.txt
    └── ...
```

---

## 4. Core Subsystem Designs

### 4.1 Memory Management

#### 4.1.1 Arena Allocator
Request-scoped allocation. All allocations within a request share one arena; freed in bulk when the request completes.

```c
typedef struct miku_arena_s {
    uint8_t *base;          // Base pointer of current block
    size_t   used;          // Bytes used in current block
    size_t   capacity;      // Total capacity of current block
    struct miku_arena_s *next;  // Next block (chain)
} miku_arena_t;

miku_arena_t *miku_arena_create(size_t initial_size);
void         *miku_arena_alloc(miku_arena_t *arena, size_t size);
void          miku_arena_reset(miku_arena_t *arena);  // Reuse arena
void          miku_arena_destroy(miku_arena_t *arena);
```

#### 4.1.2 Slab Allocator
Fixed-size object pool for frequent allocations (connections, messages, buffers).

```c
typedef struct miku_slab_s {
    size_t        obj_size;     // Size of each object
    size_t        alignment;    // Alignment requirement
    void        **free_list;    // Free object stack
    size_t        free_count;   // Available objects
    size_t        total_count;  // Total allocated objects
    miku_mutex_t  lock;         // Thread safety
} miku_slab_t;

miku_slab_t *miku_slab_create(size_t obj_size, size_t capacity);
void        *miku_slab_alloc(miku_slab_t *slab);
void         miku_slab_free(miku_slab_t *slab, void *obj);
void         miku_slab_destroy(miku_slab_t *slab);
```

#### 4.1.3 Thread-Local Memory Pool
Each thread gets its own arena for lock-free allocation in the hot path.

```c
typedef struct miku_tls_pool_s {
    miku_arena_t  *request_arena;  // Per-request arena
    miku_slab_t   *conn_slab;      // Connection objects
    miku_slab_t   *msg_slab;       // Message objects
    miku_slab_t   *buf_slab;       // I/O buffers
} miku_tls_pool_t;

miku_tls_pool_t *miku_tls_pool_get(void);  // Thread-local access
```

### 4.2 Thread Pool (Work-Stealing)

```c
typedef struct miku_threadpool_s miku_threadpool_t;
typedef void (*miku_task_fn)(void *arg);

typedef struct miku_task_s {
    miku_task_fn   fn;
    void          *arg;
    struct miku_task_s *next;
} miku_task_t;

// Per-thread state
typedef struct miku_worker_s {
    int               worker_id;
    pthread_t         thread;
    miku_task_t      *deque_head;    // Work-stealing deque (lock-free)
    miku_task_t      *deque_tail;
    atomic_size_t     deque_size;
    atomic_bool       sleeping;
    pthread_cond_t    wake_cond;
    pthread_mutex_t   wake_lock;
    miku_tls_pool_t  *tls_pool;
    miku_scheduler_t *scheduler;     // Coroutine scheduler for this thread
} miku_worker_t;

miku_threadpool_t *miku_threadpool_create(int num_workers);
void   miku_threadpool_submit(miku_threadpool_t *pool, miku_task_fn fn, void *arg);
void   miku_threadpool_destroy(miku_threadpool_t *pool);
```

Work-stealing: idle workers randomly pick another worker's deque and steal from the bottom.

### 4.3 Coroutine System (ucontext)

```c
typedef enum {
    CORO_READY,
    CORO_RUNNING,
    CORO_SUSPENDED,   // Waiting for I/O
    CORO_DEAD
} miku_coro_state_t;

typedef struct miku_coro_s {
    ucontext_t        ctx;
    ucontext_t       *caller_ctx;     // Caller to resume to
    miku_coro_state_t state;
    int64_t           coro_id;
    void            (*fn)(void *arg); // Entry function
    void             *arg;
    uint8_t          *stack;
    size_t            stack_size;
    int               events;         // epoll events we're waiting for
    int               fd;             // fd we're waiting on
    int64_t           timeout_ms;     // Deadline
    miku_arena_t     *arena;          // Request-scoped memory
    miku_tls_pool_t  *tls;            // Thread-local pool reference
} miku_coro_t;

// Core API
miku_coro_t *miku_coro_create(void (*fn)(void *), void *arg, size_t stack_size);
void         miku_coro_resume(miku_coro_t *coro);
void         miku_coro_yield(miku_coro_t *coro);
void         miku_coro_destroy(miku_coro_t *coro);

// I/O yielding (integration with IO layer)
int          miku_coro_read(miku_coro_t *coro, int fd, void *buf, size_t len);
int          miku_coro_write(miku_coro_t *coro, int fd, const void *buf, size_t len);
int          miku_coro_accept(miku_coro_t *coro, int listen_fd, struct sockaddr *addr);
void         miku_coro_sleep(miku_coro_t *coro, int64_t ms);
```

#### Coroutine Scheduler Integration with IO
```
Worker Thread Loop:
1. Check local coroutine run queue
2. If coroutine ready → resume it
3. Coroutine does non-blocking I/O:
   - If data available → process immediately
   - If not → register fd with epoll, yield coroutine
4. epoll_wait() on all registered fds
5. For each ready fd → mark corresponding coroutine as READY, put in run queue
6. Go to step 1
```

### 4.4 Networking Layer (Cross-Platform I/O)

```c
// Unified I/O interface
typedef struct miku_io_s miku_io_t;

typedef enum {
    MK_IO_READ   = 1,
    MK_IO_WRITE  = 2,
    MK_IO_ERROR  = 4,
} miku_io_event_t;

typedef void (*miku_io_cb)(int fd, int events, void *data);

miku_io_t *miku_io_create(void);
int        miku_io_add(miku_io_t *io, int fd, int events, miku_io_cb cb, void *data);
int        miku_io_mod(miku_io_t *io, int fd, int events, miku_io_cb cb, void *data);
int        miku_io_del(miku_io_t *io, int fd);
int        miku_io_poll(miku_io_t *io, int timeout_ms);
void       miku_io_destroy(miku_io_t *io);

// Platform backends:
// Linux:   miku_io_epoll.c  → epoll_create1/epoll_ctl/epoll_wait
// macOS:   miku_io_kqueue.c → kqueue/kevent
// Windows: miku_io_iocp.c   → CreateIoCompletionPort/GetQueuedCompletionStatus
```

### 4.5 HTTP Parser

Lightweight HTTP/1.1 request parser (state machine, zero-copy where possible).

```c
typedef struct miku_http_request_s {
    int         method;     // GET, POST, PUT, DELETE, etc.
    miku_str_t  path;       // Request path
    miku_str_t  query;      // Query string
    miku_str_t  body;       // Request body
    miku_str_t  content_type;
    miku_str_t  authorization;
    int         version;    // 11 = HTTP/1.1
    // Headers stored in a small hash map
    miku_hashmap_t headers;
} miku_http_request_t;

typedef struct miku_http_response_s {
    int         status_code;
    miku_str_t  body;
    miku_hashmap_t headers;
} miku_http_response_t;

// Router (similar to Gin)
typedef void (*miku_http_handler)(miku_http_request_t *req, miku_http_response_t *resp, void *ctx);

typedef struct miku_http_router_s miku_http_router_t;
miku_http_router_t *miku_http_router_create(void);
void miku_http_route(miku_http_router_t *r, const char *method, const char *path,
                     miku_http_handler handler, void *ctx);
```

### 4.6 WebSocket Server (RFC 6455)

```c
typedef struct miku_ws_conn_s {
    int              fd;
    miku_coro_t     *coro;
    uint8_t          opcode;
    uint8_t          mask[4];
    uint64_t         payload_len;
    uint8_t         *payload;
    char            *user_id;
    int              platform_id;
    bool             is_compressed;
    bool             closed;
    miku_mutex_t     write_lock;
} miku_ws_conn_t;

typedef void (*miku_ws_on_message)(miku_ws_conn_t *conn, uint8_t *data, size_t len);
typedef void (*miku_ws_on_open)(miku_ws_conn_t *conn);
typedef void (*miku_ws_on_close)(miku_ws_conn_t *conn);

typedef struct miku_ws_server_s {
    int                  listen_fd;
    miku_io_t           *io;
    miku_ws_on_message   on_message;
    miku_ws_on_open      on_open;
    miku_ws_on_close     on_close;
    miku_hashmap_t      *clients;     // user_id → ws_conn
    atomic_int64_t       online_count;
} miku_ws_server_t;
```

### 4.7 Custom Binary RPC Protocol

#### Frame Format
```
┌──────┬──────┬──────┬──────────┬──────────┬──────────┬─────────────┐
│ MAGIC│ VER  │ TYPE │   LEN    │  REQ_ID  │ SERVICE  │   PAYLOAD   │
│ 2B   │ 1B   │ 1B   │ 4B       │ 8B       │ 2B+      │ variable    │
└──────┴──────┴──────┴──────────┴──────────┴──────────┴─────────────┘

MAGIC:  0xMK (0x4D4B)
VER:    Protocol version (1)
TYPE:   0x01=Request, 0x02=Response, 0x03=Error, 0x04=Stream, 0x05=Ping, 0x06=Pong
LEN:    Total frame length (big-endian)
REQ_ID: Request ID for correlation (big-endian)
SERVICE: Service path length (1B) + method length (1B) + service_path + method_name
PAYLOAD: Protocol Buffers encoded data

Optional TRAILER:
┌──────────┬──────────┐
│  CRC32   │   META   │
│  4B      │ variable │
└──────────┴──────────┘
```

#### RPC Client/Server API
```c
typedef struct miku_rpc_server_s miku_rpc_server_t;
typedef struct miku_rpc_client_s miku_rpc_client_t;

typedef void (*miku_rpc_handler)(const char *method,
                                  const uint8_t *payload, size_t payload_len,
                                  uint8_t **resp, size_t *resp_len,
                                  void *ctx);

// Server
miku_rpc_server_t *miku_rpc_server_create(const char *host, int port);
void miku_rpc_register(miku_rpc_server_t *srv, const char *service,
                       const char *method, miku_rpc_handler handler, void *ctx);
int  miku_rpc_server_run(miku_rpc_server_t *srv);  // Blocking, uses coroutines

// Client
miku_rpc_client_t *miku_rpc_client_create(const char *target);
int  miku_rpc_call(miku_rpc_client_t *client, const char *service,
                   const char *method,
                   const uint8_t *req, size_t req_len,
                   uint8_t **resp, size_t *resp_len);
void miku_rpc_client_destroy(miku_rpc_client_t *client);
```

### 4.8 Protocol Buffers Encoder/Decoder (Lightweight)

Self-implemented lightweight PB encoder/decoder (like nanopb but simpler).

```c
// Wire types
#define PB_WIRE_VARINT   0
#define PB_WIRE_64BIT    1
#define PB_WIRE_LEN      2
#define PB_WIRE_32BIT    5

// Encode
int miku_pb_encode_varint(uint8_t *buf, size_t buf_len, uint64_t val);
int miku_pb_encode_string(uint8_t *buf, size_t buf_len, int field_num, const char *str);
int miku_pb_encode_bytes(uint8_t *buf, size_t buf_len, int field_num, const uint8_t *data, size_t len);
int miku_pb_encode_int32(uint8_t *buf, size_t buf_len, int field_num, int32_t val);
int miku_pb_encode_int64(uint8_t *buf, size_t buf_len, int field_num, int64_t val);

// Decode
typedef struct miku_pb_reader_s {
    const uint8_t *data;
    size_t         len;
    size_t         pos;
} miku_pb_reader_t;

int      miku_pb_read_tag(miku_pb_reader_t *r, int *field_num, int *wire_type);
uint64_t miku_pb_read_varint(miku_pb_reader_t *r);
int32_t  miku_pb_read_int32(miku_pb_reader_t *r);
int64_t  miku_pb_read_int64(miku_pb_reader_t *r);
int      miku_pb_read_string(miku_pb_reader_t *r, char **str, size_t *len);
int      miku_pb_read_bytes(miku_pb_reader_t *r, uint8_t **data, size_t *len);
```

### 4.9 Data Access Layer

#### MongoDB (mongoc driver + coroutine integration)
```c
typedef struct miku_mongo_pool_s miku_mongo_pool_t;

miku_mongo_pool_t *miku_mongo_pool_create(const char *uri, int pool_size);
// Coroutine-friendly: yields while waiting for response
int miku_mongo_insert(miku_mongo_pool_t *pool, const char *db, const char *collection,
                       const uint8_t *doc, size_t doc_len);
int miku_mongo_find_one(miku_mongo_pool_t *pool, const char *db, const char *collection,
                         const uint8_t *filter, size_t filter_len,
                         uint8_t **result, size_t *result_len);
int miku_mongo_update(miku_mongo_pool_t *pool, const char *db, const char *collection,
                       const uint8_t *filter, const uint8_t *update, size_t len);
```

#### Redis (hiredis async + coroutine)
```c
typedef struct miku_redis_s miku_redis_t;

miku_redis_t *miku_redis_create(const char *host, int port, const char *password, int pool_size);
// All operations yield coroutine until response received
int    miku_redis_set(miku_redis_t *r, const char *key, const char *val, int64_t ttl_ms);
char  *miku_redis_get(miku_redis_t *r, const char *key);
int    miku_redis_del(miku_redis_t *r, const char *key);
int    miku_redis_incr(miku_redis_t *r, const char *key, int64_t *result);
int    miku_redis_hset(miku_redis_t *r, const char *key, const char *field, const char *val);
char  *miku_redis_hget(miku_redis_t *r, const char *key, const char *field);
int64_t miku_redis_zadd(miku_redis_t *r, const char *key, double score, const char *member);
```

#### Kafka (librdkafka)
```c
typedef struct miku_kafka_producer_s miku_kafka_producer_t;
typedef struct miku_kafka_consumer_s miku_kafka_consumer_t;
typedef void (*miku_kafka_msg_cb)(const char *topic, const uint8_t *key, size_t key_len,
                                   const uint8_t *payload, size_t payload_len);

miku_kafka_producer_t *miku_kafka_producer_create(const char *brokers);
int miku_kafka_produce(miku_kafka_producer_t *p, const char *topic,
                        const uint8_t *key, size_t key_len,
                        const uint8_t *payload, size_t payload_len);

miku_kafka_consumer_t *miku_kafka_consumer_create(const char *brokers, const char *group_id);
int miku_kafka_subscribe(miku_kafka_consumer_t *c, const char **topics, int count,
                          miku_kafka_msg_cb callback);
```

### 4.10 Local Cache (LRU + TTL)

```c
typedef struct miku_cache_s miku_cache_t;
typedef void (*miku_cache_free_fn)(void *val);

miku_cache_t *miku_cache_create(size_t max_size, int64_t default_ttl_ms);
void         *miku_cache_get(miku_cache_t *cache, const char *key);
int           miku_cache_set(miku_cache_t *cache, const char *key, void *val,
                              size_t val_size, int64_t ttl_ms);
int           miku_cache_del(miku_cache_t *cache, const char *key);
void          miku_cache_destroy(miku_cache_t *cache);
```

### 4.11 Service Discovery (etcd)

```c
typedef struct miku_etcd_s miku_etcd_t;

miku_etcd_t *miku_etcd_create(const char *endpoints);
// Register service instance
int miku_etcd_register(miku_etcd_t *etcd, const char *service_name,
                        const char *instance_addr, int64_t lease_ttl);
// Discover service instances
int miku_etcd_discover(miku_etcd_t *etcd, const char *service_name,
                        char ***addrs, int *count);
// Watch for changes
int miku_etcd_watch(miku_etcd_t *etcd, const char *prefix,
                     void (*callback)(const char *key, const char *val));
```

---

## 5. Service Architecture

### 5.1 Service Entry Point Pattern
Each service is a separate executable with identical startup pattern:

```c
int main(int argc, char **argv) {
    // 1. Parse command line args
    // 2. Load configuration (YAML)
    // 3. Initialize logging
    // 4. Initialize memory pools
    // 5. Initialize thread pool + coroutine scheduler
    // 6. Connect to infrastructure (MongoDB, Redis, Kafka)
    // 7. Register with service discovery (etcd)
    // 8. Start RPC server / HTTP server / Kafka consumer
    // 9. Install signal handlers (SIGTERM, SIGINT for graceful shutdown)
    // 10. Block until shutdown signal
    // 11. Graceful shutdown: drain connections, flush data, deregister
}
```

### 5.2 Configuration Management
Each service loads its own YAML config + shared configs. Hot-reload via SIGHUP.

```c
typedef struct miku_config_s {
    miku_str_t  config_path;
    miku_hashmap_t values;  // Key-value config store
    
    // Pre-parsed sections
    struct {
        miku_str_t  listen_ip;
        int        *ports;
        int         port_count;
    } api;
    
    struct {
        miku_str_t  uri;
        miku_str_t  database;
        miku_str_t  username;
        miku_str_t  password;
        int         max_pool_size;
    } mongodb;
    
    struct {
        miku_str_t *addresses;
        int         addr_count;
        miku_str_t  password;
        int         pool_size;
    } redis;
    
    // ... more sections
} miku_config_t;
```

### 5.3 Graceful Shutdown
```
SIGTERM received:
1. Stop accepting new connections
2. Set drain flag
3. Wait for in-flight requests to complete (with timeout)
4. Flush Kafka producer buffers
5. Close MongoDB connections
6. Close Redis connections
7. Deregister from etcd
8. Destroy memory pools
9. Exit(0)
```

---

## 6. Build System (CMake)

```cmake
cmake_minimum_required(VERSION 3.16)
project(miku C)

# C99 baseline, with C23 feature detection
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Cross-platform detection
if(UNIX AND NOT APPLE)
    set(MIKU_LINUX 1)
elseif(APPLE)
    set(MIKU_MACOS 1)
elseif(WIN32)
    set(MIKU_WINDOWS 1)
endif()

# Dependencies: find_package or FetchContent
find_package(libmongoc-1.0 REQUIRED)
find_package(hiredis REQUIRED)
find_package(RdKafka REQUIRED)
find_package(libyaml REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)

# Subdirectories
add_subdirectory(src/foundation)
add_subdirectory(src/runtime)
add_subdirectory(src/protocol)
add_subdirectory(src/storage)
add_subdirectory(src/discovery)
add_subdirectory(src/models)
add_subdirectory(src/services)
add_subdirectory(src/gateway)
add_subdirectory(cmd)
add_subdirectory(tests)
```

---

## 7. Testing Strategy (TDD)

### Test Framework: Custom lightweight (cmocka-inspired)

```c
// test_memory.c
void test_arena_alloc(void **state) {
    miku_arena_t *arena = miku_arena_create(4096);
    assert_non_null(arena);
    
    void *p1 = miku_arena_alloc(arena, 100);
    assert_non_null(p1);
    
    void *p2 = miku_arena_alloc(arena, 200);
    assert_non_null(p2);
    assert_ptr_not_equal(p1, p2);
    
    miku_arena_reset(arena);
    // All allocations freed
    
    miku_arena_destroy(arena);
}
```

### Test Categories
1. **Unit tests**: Per-module (memory, coroutine, http, etc.)
2. **Integration tests**: Service-to-service RPC calls
3. **End-to-end tests**: Full API flow (register → login → send msg → receive)
4. **Stress tests**: 100K concurrent connections, message throughput benchmarks
5. **Memory leak tests**: Valgrind/ASAN integration

### Test Execution
```bash
./scripts/test.sh              # All tests
./scripts/test.sh --unit       # Unit tests only
./scripts/test.sh --integration # Integration tests
./scripts/test.sh --valgrind   # Memory leak detection
```

---

## 8. Performance Targets

| Metric | Target |
|--------|--------|
| Concurrent WebSocket connections | 100K+ per instance |
| Message throughput | 500K+ msg/sec |
| Message latency (p99) | < 1ms (intra-datacenter) |
| RPC call overhead | < 50μs |
| Memory per connection | < 8KB |
| Startup time | < 500ms |
| Binary size per service | < 5MB |

---

## 9. External Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| **mongoc** | 1.27+ | MongoDB C driver |
| **hiredis** | 1.2+ | Redis async client |
| **librdkafka** | 2.3+ | Kafka producer/consumer |
| **libyaml** | 0.2+ | YAML configuration parsing |
| **zlib** | 1.3+ | Compression (gzip, WebSocket) |
| **libcurl** | 8.0+ | HTTP client (etcd API, S3) |
| **OpenSSL** | 3.0+ | TLS, JWT, HMAC-SHA256 |

---

## 10. Implementation Phases

### Phase 1: Foundation Infrastructure (Weeks 1-3)
- Build system, project structure
- Memory pool (Arena + Slab + TLS)
- Logger, config parser, error framework
- Data structures (hashmap, list, rbtree, string buffer)
- Thread pool (work-stealing)
- Coroutine scheduler (ucontext)
- I/O multiplexing abstraction (epoll/kqueue/IOCP)
- HTTP parser + HTTP server
- WebSocket server
- Binary RPC framework
- Protocol Buffers codec

### Phase 2: Data Access Layer (Week 4)
- MongoDB driver wrapper
- Redis async client wrapper
- Kafka producer/consumer
- Local cache (LRU + TTL)
- Service discovery (etcd)

### Phase 3: Business Services (Weeks 5-8)
- RPC-Auth (JWT authentication)
- RPC-User (user management)
- RPC-Friend (relationship management)
- RPC-Group (group management)
- RPC-Conversation (conversation management)
- RPC-Msg (message core)
- RPC-Third (third-party services)

### Phase 4: Gateway Services (Weeks 9-11)
- HTTP API gateway (all 100+ endpoints)
- WebSocket gateway (MsgGateway)
- Message transfer (Kafka → MongoDB)
- Push notification service
- Cron task service

### Phase 5: Testing & Documentation (Week 12)
- Full test suite
- Documentation
- Deployment scripts
- Performance benchmarks
