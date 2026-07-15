# Miku IM Server - Architecture Design Document

> High-performance, high-throughput, distributed IM server in pure C (C99-C23 compatible)
> Rewriting OpenIM Server (Go, 47K LOC, 12 microservices) with memory pool, thread pool, coroutines, and cross-platform support.
> **Status**: 203 API routes, 171 tests, 67 modules — inviteToGroup rejects missing/dismissed; sendBiz/history/roster gated; S3 stub.

## 1. Overview

### 1.1 Source Architecture (Go/OpenIM)
- **12 microservices** communicating via gRPC
- **Infrastructure**: MongoDB, Redis, Kafka, etcd, MinIO/S3, Prometheus
- **153 HTTP API endpoints** via Gin framework
- **WebSocket gateway** with 12 protocol opcodes
- **Kafka-based** message transfer pipeline (Redis → MongoDB → Push)
- **Offline push**: FCM, Getui, JPUSH
- **Cron tasks**: message cleanup, S3 file cleanup
- **Webhook/callback** system for event-driven extensions
- **Rate limiting**, **gzip compression**, **incremental sync**

### 1.2 Target Architecture (C/Miku)
- **Same 12 microservices** as separate executables
- **Custom binary RPC** instead of gRPC (lighter, faster)
- **C native libraries**: mongoc, hiredis, librdkafka, libyaml, zlib, libcurl
- **ucontext stackful coroutines** for async I/O
- **Work-stealing thread pool** for CPU-bound tasks
- **Cross-platform I/O**: epoll (Linux), kqueue (macOS), IOCP (Windows)
- **Full feature parity**: 203 routes (exceeds original), WS protocol, offline push, webhooks, rate limiting, gzip, incremental sync, seq management

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
├── Makefile                          # Convenience wrapper (debug/release/test/bench)
├── ARCHITECTURE.md                   # This file
├── README.md                         # Project documentation
├── notes.html                        # Development progress notes
│
├── build/                            # Build output (gitignored)
├── config/                           # Configuration files
│   ├── share.yml                     # Listen IP, API/WS/RPC ports
│   ├── mongodb.yml                   # MongoDB URI, database, pool size
│   ├── redis.yml                     # Redis address, DB, pool size
│   ├── kafka.yml                     # Kafka brokers, topic, group ID
│   └── log.yml                       # Log level, output, file rotation
│
├── src/
│   ├── foundation/                   # Foundation layer (15+ modules)
│   │   ├── CMakeLists.txt
│   │   ├── miku_common.h             # Platform detection, types, byte-order, time
│   │   ├── miku_arena.h/c            # Arena allocator
│   │   ├── miku_slab.h/c             # Slab allocator
│   │   ├── miku_memory.h/c           # Memory pool (Arena + Slab)
│   │   ├── miku_log.h/c              # Logging (6 levels, file+console)
│   │   ├── miku_config.h/c           # YAML config parser (dot-path nesting)
│   │   ├── miku_service_config.h/c   # Unified service config loader
│   │   ├── miku_error.h/c            # Error handling
│   │   ├── miku_string.h/c           # String buffer (sds-like)
│   │   ├── miku_hashmap.h/c          # Hash map (FNV-1a, open addressing)
│   │   ├── miku_list.h               # Intrusive doubly-linked list
│   │   ├── miku_rbtree.h/c           # Red-black tree
│   │   ├── miku_thread.h/c           # Thread, mutex, cond, rwlock
│   │   ├── miku_spinlock.h           # Spinlock (C11 atomics + GCC fallback)
│   │   ├── miku_atomic.h             # Atomic operations wrapper
│   │   ├── miku_uuid.h/c             # UUID v4 generation
│   │   ├── miku_crc32.h/c            # CRC32 checksum
│   │   ├── miku_base64.h/c           # Base64 encode/decode
│   │   ├── miku_sha1.h/c             # SHA-1 hash
│   │   ├── miku_token.h/c            # Signed token create/verify (FNV-1a, shared by auth + middleware)
│   │   ├── miku_graceful.h/c         # Graceful shutdown (SIGTERM/SIGINT + SIGHUP reload)
│   │   ├── miku_stats.h/c            # Atomic service metrics
│   │   └── miku_json_util.h          # Shared JSON helpers (miku_ji, miku_jss, miku_jerr)
│   │
│   ├── runtime/                      # Concurrency runtime
│   │   ├── CMakeLists.txt
│   │   ├── miku_coroutine.h/c        # ucontext stackful coroutines
│   │   ├── miku_scheduler.h/c        # Coroutine scheduler
│   │   ├── miku_threadpool.h/c       # Thread pool
│   │   ├── miku_io.h                 # I/O multiplexing abstraction
│   │   ├── miku_io_epoll.c           # Linux epoll backend
│   │   ├── miku_channel.h/c          # Coroutine channel
│   │   ├── miku_timer.h/c            # Timer (min-heap)
│   │   ├── miku_async.h/c            # Async stubs
│   │   └── miku_runtime.h/c          # Runtime singleton
│   │
│   ├── protocol/                     # Protocol layer
│   │   ├── CMakeLists.txt
│   │   ├── miku_http.h/c             # HTTP/1.1 parser
│   │   ├── miku_http_server.h/c      # HTTP server (routes, middleware, stats tracking)
│   │   ├── miku_json.h/c             # JSON parser + builder + stringify
│   │   ├── miku_websocket.h/c        # WebSocket (RFC 6455) frame codec
│   │   ├── miku_rpc.h/c              # Binary RPC framework + header codec
│   │   ├── miku_rpc_server.h/c       # Generic TCP RPC server (listen/poll/dispatch)
│   │   ├── miku_pb.h/c               # Protocol Buffers encoder/decoder
│   │   ├── miku_middleware.h/c        # HTTP middleware (CORS, rate limit, logging, stats, auth, request_id)
│   │   └── miku_rpc_cmd.h            # RPC command IDs
│   │
│   ├── storage/                      # Data access layer
│   │   ├── CMakeLists.txt
│   │   ├── miku_cache.h/c            # Local cache (LRU + TTL)
│   │   ├── miku_mongo.h/c            # MongoDB driver wrapper (conditional)
│   │   ├── miku_redis.h/c            # Redis client wrapper (conditional)
│   │   └── miku_kafka.h/c            # Kafka producer/consumer (conditional)
│   │
│   ├── discovery/                    # Service discovery
│   │   ├── miku_discovery.h/c        # Service discovery (stub)
│   │   └── CMakeLists.txt
│   │
│   ├── models/                       # Data models
│   │   ├── miku_models.h/c           # All models: user, friend, group, group_member, msg, conversation, token_info
│   │   └── CMakeLists.txt
│   │
│   ├── services/                     # Business services (7 RPC)
│   │   ├── auth/miku_auth.h/c        # Auth: token generation, parsing, force_logout
│   │   ├── user/miku_user.h/c        # User: register, find, update, get_users, count
│   │   ├── friend/miku_friend.h/c    # Friend: add, delete, get_list, is_friend
│   │   ├── group/miku_group.h/c      # Group: create, find, add_member, get_members
│   │   ├── conversation/miku_conversation.h/c  # Conv: create, get, get_all, update
│   │   ├── msg/miku_msg.h/c          # Msg: send, get_by_conv, revoke
│   │   ├── third/miku_third.h/c      # Third: getUploadToken, getDownloadURL
│   │   └── CMakeLists.txt
│   │
│   └── gateway/                      # Gateway services
│       ├── api/miku_api.h/c          # HTTP API gateway (203 routes, 17 service groups)
│       ├── msggateway/miku_msggateway.h/c  # WebSocket message gateway (4096 clients, 12 opcodes)
│       ├── msggateway/miku_im_message.h/c   # IM message protocol (JSON encode/decode)
│       ├── msggateway/miku_ws_subscription.h/c  # WS user status subscription
│       ├── msgtransfer/miku_msgtransfer.h/c  # Message transfer queue (SPSC ring buffer)
│       ├── msgtransfer/miku_mt_pipeline.h/c  # Batch pipeline (Redis/Mongo/Push callbacks)
│       ├── push/miku_push.h/c        # Online push notification service
│       ├── push/miku_offline_push.h/c  # Offline push (FCM/Getui/JPUSH/Dummy)
│       ├── crontask/miku_crontask.h/c  # Cron task scheduler
│       ├── crontask/miku_cron_tasks.h/c  # Cron task implementations (deleteMsg/clearS3)
│       └── CMakeLists.txt
│
├── cmd/                              # Service entry points (13 binaries)
│   ├── miku-api/main.c               # HTTP API gateway
│   ├── miku-msggateway/main.c        # WebSocket gateway
│   ├── miku-msgtransfer/main.c       # Message transfer
│   ├── miku-push/main.c              # Push notifications
│   ├── miku-crontask/main.c          # Cron tasks
│   ├── miku-rpc-auth/main.c          # Auth RPC service
│   ├── miku-rpc-user/main.c          # User RPC service
│   ├── miku-rpc-friend/main.c        # Friend RPC service
│   ├── miku-rpc-group/main.c         # Group RPC service
│   ├── miku-rpc-conversation/main.c  # Conversation RPC service
│   ├── miku-rpc-msg/main.c           # Message RPC service
│   ├── miku-rpc-third/main.c         # Third-party RPC service
│   ├── miku-dev/main.c               # All-in-one dev server
│   └── CMakeLists.txt
│
└── tests/                            # Test suite (156 tests + 5 benchmarks)
    ├── test_foundation.c             # Foundation tests (20 tests)
    ├── test_runtime.c                # Runtime tests (9 tests)
    ├── test_protocol.c               # Protocol + middleware + route tests (40 tests)
    ├── test_storage.c                # Storage tests (9 tests)
    ├── test_services.c               # Service + integration tests (22 tests)
    ├── test_new_modules.c            # New module tests (23 tests)
    ├── test_benchmark.c              # Benchmarks (5 benchmarks)
    └── CMakeLists.txt
```

### Additional Project Files

```
miku/
├── Dockerfile                        # Multi-stage build (gcc:13 → debian:bookworm-slim)
├── Makefile                          # Convenience targets (build/test/dev/release/docker/count)
├── .github/workflows/ci.yml          # GitHub Actions CI (build + test + docker-build)
├── scripts/
│   └── bench.sh                      # Load test / benchmark runner
├── config/                           # YAML configuration files
│   ├── share.yml, mongodb.yml, redis.yml, kafka.yml, log.yml
└── notes.html                        # Development progress tracking
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

### 4.2 Thread Pool (Shared Global Queue)

Current implementation uses a single mutex-protected global task queue shared by all workers.
Per-worker work-stealing deques are planned but not yet implemented.

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

### 4.12 HTTP Server Features

The HTTP server (`miku_http_server`) supports production-ready features:

- **HTTP Keep-Alive**: Epoll event handles one request per wake-up, keeps fd registered for next request
- **Connection Idle Timeout**: Configurable (default 30s), tracked via `conn_last_active[]` array, expired connections cleaned every 100ms epoll cycle
- **Max Body Size Limit**: Default 1MB, returns `413 Payload Too Large` on oversized requests
- **TLS Support**: Optional OpenSSL integration via `-DMIKU_ENABLE_TLS=ON`, `miku_http_server_set_tls(cert, key)`. SSL read/write wrapped in `MIKU_READ`/`MIKU_WRITE` macros for transparent operation
- **Connection & Bytes Tracking**: Atomic counters for total connections, active connections, bytes in/out
- **Graceful Shutdown**: Stop accepting new connections, drain in-flight requests

```c
miku_http_server_t *miku_http_server_create(const char *host, int port);
void miku_http_server_set_tls(miku_http_server_t *srv, const char *cert, const char *key);
void miku_http_server_set_max_body(miku_http_server_t *srv, size_t max_bytes);
void miku_http_server_set_idle_timeout(miku_http_server_t *srv, int seconds);
void miku_http_server_set_stats(miku_http_server_t *srv, miku_stats_t *stats);
```

### 4.13 Middleware Pipeline

Middleware executes in chain order before route handlers. Chain: **CORS → request_id → logging → auth → stats**.

| Middleware | Purpose |
|------------|---------|
| `miku_mw_cors` | Sets `Access-Control-Allow-*` headers for cross-origin requests |
| `miku_mw_request_id` | Generates unique `X-Request-ID` (UUID v4) per request, propagates to response |
| `miku_mw_logging` | Access log: method, path, status code, latency, request ID |
| `miku_mw_auth` | Cryptographic token validation (`miku\|uid\|platform\|ts\|nonce\|sig`, FNV-1a over secret), returns 401 on failure. Public: `/auth/user_token`, `/auth/admin_token`, `/auth/parse_token`, `/admin/health`, `/admin/metrics`, `/version`, `/prometheus*`. `force_logout` requires auth. |
| `miku_mw_stats` | Increments request/error counters in `miku_stats_t` |

```c
typedef enum { MK_MW_CONTINUE = 0, MK_MW_STOP = 1 } miku_mw_result_t;
typedef miku_mw_result_t (*miku_http_middleware_fn)(miku_http_request_t *req,
                                                    miku_http_response_t *resp,
                                                    void *ctx);
int miku_http_server_use(miku_http_server_t *srv, miku_http_middleware_fn mw, void *ctx);
```

### 4.14 Service Metrics (miku_stats)

Atomic counters for observability. Each service maintains its own `miku_stats_t`.

```c
typedef struct {
    const char   *service_name;
    int           instance_id;
    atomic_int64_t total_requests;
    atomic_int64_t total_errors;
    atomic_int64_t active_connections;
    atomic_int64_t bytes_in;
    atomic_int64_t bytes_out;
    atomic_int64_t ws_connections;
    atomic_int64_t uptime_start;
} miku_stats_t;

void miku_stats_init(miku_stats_t *s, const char *name, int id);
void miku_stats_format_prometheus(const miku_stats_t *s, char *buf, size_t len);
```

### 4.15 Prometheus Metrics Endpoint

`GET /admin/metrics` returns standard Prometheus text format:

```
# HELP miku_requests_total Total requests processed
# TYPE miku_requests_total counter
miku_requests_total{service="miku-api"} 12345
# HELP miku_errors_total Total errors
# TYPE miku_errors_total counter
miku_errors_total{service="miku-api"} 42
# HELP miku_active_connections Currently active connections
# TYPE miku_active_connections gauge
miku_active_connections{service="miku-api"} 7
...
```

### 4.16 Log Rotation

Size-based rotation via `miku_log_set_rotation()`. When log file exceeds threshold:

```
miku.log → miku.log.1
miku.log.1 → miku.log.2
...
miku.log.{N-1} → miku.log.N  (N = max_files)
```

### 4.17 Version Module

Compile-time version constants:

```c
// miku_version.h
#define MIKU_VERSION_MAJOR 0
#define MIKU_VERSION_MINOR 1
#define MIKU_VERSION_PATCH 0
#define MIKU_VERSION "0.1.0"
```

Exposed via `GET /version` endpoint.

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

### Build Commands

```bash
# Debug build with tests, no external services
cmake -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMIKU_ENABLE_TESTS=ON \
  -DMIKU_ENABLE_MONGO=OFF \
  -DMIKU_ENABLE_REDIS=OFF \
  -DMIKU_ENABLE_KAFKA=OFF \
  -DMIKU_ENABLE_S3=OFF \
  -DMIKU_ENABLE_TLS=OFF
cmake --build build -j$(nproc)

# Run tests
timeout 15 ./build/bin/miku_tests

# Production build with all features
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DMIKU_ENABLE_MONGO=ON -DMIKU_ENABLE_REDIS=ON \
  -DMIKU_ENABLE_KAFKA=ON -DMIKU_ENABLE_S3=ON \
  -DMIKU_ENABLE_TLS=ON
```

### Makefile Targets

```bash
make build      # Debug build (no external deps)
make test       # Build + run tests
make dev        # Start dev server (all-in-one)
make release    # Release build with all features
make docker     # Build Docker image
make count      # Count modules, tests, routes
```

### Conditional Compilation Flags

| Flag | Default | Description |
|------|---------|-------------|
| `MIKU_ENABLE_TESTS` | OFF | Build test suite |
| `MIKU_ENABLE_MONGO` | ON | MongoDB driver (mongoc) |
| `MIKU_ENABLE_REDIS` | ON | Redis client (hiredis) |
| `MIKU_ENABLE_KAFKA` | ON | Kafka producer/consumer (librdkafka) |
| `MIKU_ENABLE_S3` | ON | S3/MinIO storage (libcurl) |
| `MIKU_ENABLE_TLS` | OFF | OpenSSL TLS support |

When disabled, each module compiles to a stub that returns success/error codes.

### CMake Structure

```cmake
cmake_minimum_required(VERSION 3.16)
project(miku C)
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

| Category | Tests | Description |
|----------|-------|-------------|
| Foundation | 20 | Memory, arena, slab, log, config, hashmap, string, UUID, etc. |
| Runtime | 9 | Coroutine, thread pool, scheduler, channel, timer |
| Protocol | 40 | HTTP parser, JSON, SHA1, WebSocket, RPC, PB, middleware, routes |
| Storage | 9 | LRU cache, service discovery |
| Services | 22 | Models, 7 RPC services, integration tests, auth middleware |
| **Total** | **100** | + 5 benchmarks |
| Benchmarks | 5 | JSON parse/stringify, HashMap put, Cache set+get, Queue enqueue |

### Test Execution
```bash
# Quick build + test
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DMIKU_ENABLE_TESTS=ON \
  -DMIKU_ENABLE_MONGO=OFF -DMIKU_ENABLE_REDIS=OFF \
  -DMIKU_ENABLE_KAFKA=OFF -DMIKU_ENABLE_S3=OFF && \
cmake --build build -j$(nproc) && \
timeout 15 ./build/bin/miku_tests

# Or via Makefile
make test
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

| Library | Version | Purpose | Required |
|---------|---------|---------|----------|
| **mongoc** | 1.27+ | MongoDB C driver | Optional (`MIKU_ENABLE_MONGO`) |
| **hiredis** | 1.2+ | Redis async client | Optional (`MIKU_ENABLE_REDIS`) |
| **librdkafka** | 2.3+ | Kafka producer/consumer | Optional (`MIKU_ENABLE_KAFKA`) |
| **libyaml** | 0.2+ | YAML configuration parsing | Yes |
| **zlib** | 1.3+ | Compression (gzip, WebSocket) | Yes |
| **libcurl** | 8.0+ | HTTP client (etcd API, S3) | Optional (`MIKU_ENABLE_S3`) |
| **OpenSSL** | 3.0+ | TLS | Optional (`MIKU_ENABLE_TLS`) |

---

## 10. Implementation Phases (Actual)

All phases complete for the HTTP/WS API surface. **171 tests + 5 benchmarks** passing. **67 modules** across 6 layers. **13 binaries**. **203 routes**. Auth uses signed `miku|...` tokens (FNV-1a, ms timestamps, in-memory revoke). WS gateway uses epoll with slot reuse, O(1) fd map and user-id hash chains for push/kick, and requires handshake token. Rate limit and in-memory user lookup use FNV open-addressing (same pattern as `miku_seq`). Inbound opcode frames unwrap `data` before `on_op`; `SEND_MSG` persists and fans out `PUSH_MSG` to online `recvID`; `SUB_USER_STATUS` subscriptions receive online/offline presence via `miku_ws_sub_user_online/offline` hooked to connection lifecycle. Seq is per-conversation via `miku_seq`. Split deploy kick via localhost `/internal/kick`. In-mem `deleteMsg` co-located with writers. Offline push POSTs JSON to an optional `http://` endpoint when configured.

| Phase | Description | Status |
|-------|-------------|--------|
| 0 | Architecture Design | DONE |
| 1A | Build System + Project Skeleton | DONE |
| 1B | Memory Pool (Arena + Slab) | DONE |
| 1C | Logger, Config, Error | DONE |
| 1D | Thread Pool + Coroutine | DONE |
| 1E | I/O Abstraction (epoll) | DONE |
| 1F | HTTP + JSON | DONE |
| 1G | WebSocket + Binary RPC + Protobuf | DONE |
| 2 | Data Access Layer (cache, mongo, redis, kafka, discovery) | DONE |
| 3 | Business Services (7 RPC) | DONE |
| 4 | Gateway Services (API routes, WS gateway, transfer, push, cron) | DONE |
| 5 | Service Wiring + Config YAML + Integration Tests | DONE |
| 6 | Middleware Framework + Dev Server + Benchmarks | DONE |
| 7 | WebSocket Gateway I/O + Cross-Service Integration | DONE |
| 8 | Config Integration (dot-path YAML, service config loader, graceful shutdown) | DONE |
| 9 | Service Metrics + Health Endpoints + SIGHUP Reload + README | DONE |
| 10 | Stats Middleware + JSON Utilities + Error Standardization | DONE |
| 11 | HTTP Server Connection + Bytes Tracking | DONE |
| 12 | Version Module + RPC Server Stats + JSON 404 Responses | DONE |
| 13 | /version Endpoint + Access Log Middleware + 1MB Body Limit (413) | DONE |
| 14 | 103 Registered Routes (7 service groups) - Full OpenIM API Surface | DONE |
| 15 | Auth Middleware (miku_mw_auth) + 10 Integration Tests (99→100 total) | DONE |
| 16 | HTTP Keep-Alive + Request ID Tracking + Response Header val_free Bugfix | DONE |
| 17 | Connection Idle Timeout (30s) + Full RPC Dispatch for All Routes | DONE |
| 18 | Dockerfile (Multi-stage) + Makefile + Route Validation Test | DONE |
| 19 | GitHub Actions CI (build + test + docker-build) | DONE |
| 20 | Optional TLS via OpenSSL + Conditional Compilation | DONE |
| 21 | Log Rotation (size-based) + Prometheus /admin/metrics Endpoint | DONE |
| 22 | ARCHITECTURE.md Rewrite (13 sections) + Benchmark Script | DONE |
| 23 | Production Deployment (systemd, docker-compose, k8s, config example) | DONE |
| 24 | OpenAPI 3.0 Specification (3037 lines, 103 routes) | DONE |
| 25 | README Update + 108→0 Source Warnings | DONE |
| 26 | Test Warning Cleanup + .gitignore (73→0 test warnings, 0 total) | DONE |
| 27 | Optional Enhancements (IM message protocol, Swagger UI, ASAN, Helm, MongoDB msg store, Redis session cache) | DONE |
| 28 | Full API Parity — 103→203 routes (User+18, Friend+11, Group+15, Msg+14, Third+8, Conv+8, Statistics+4, JSSDK+2, PrometheusDiscovery+11, Config+6, Restart+1, Object+8) | DONE |
| 29 | Non-HTTP Feature Parity — WS protocol (12 opcodes), WS subscription, MsgTransfer pipeline, offline push (FCM/Getui/JPUSH/Dummy), CronTask tasks, Webhook/callback (11 events), rate limiting, seq management, incremental sync, gzip compression | DONE |
| 30 | New Module Tests (14 tests) + README/notes.html Update | DONE |
| 31-32 | OpenAPI spec updated to 203 routes, ARCHITECTURE.md updated | DONE |
| 33 | WS Gateway Binary Wiring (12 opcodes, IM message, subscriptions) | DONE |
| 34 | MsgTransfer Pipeline Wiring (SPSC→pipeline, batch flush, callbacks) | DONE |
| 35 | Push Binary Offline Push Wiring (DUMMY provider, device tokens) | DONE |
| 36 | CronTask Binary Task Wiring (scheduler + impls, deleteMsg/clearS3) | DONE |
| 37 | API Binary Rate Limit + Webhook (100/min, startup event) | DONE |
| 38 | New Module Tests (im_message, mt_pipeline, msg_store, session_cache — 9 tests) | DONE |
| 39 | miku-dev Full Integration (all modules in single process) | DONE |
| 40 | README + Docs Update (test count 34→123) | DONE |
| 41-42 | ARCHITECTURE.md + README documentation full update | DONE |
| 43 | Fix 6 API path→method strstr() chain ordering bugs | DONE |
| 44 | Add 79 missing RPC method stubs — all 166 methods dispatched | DONE |
| 45-46 | Flesh out RPC stubs with real logic + 7 dispatch coverage tests (150 methods) | DONE |
| 47 | 6 functional integration tests (msg CRUD+search, friend remark+designated, group setInfoEx+members, conv update+get, msg seq range pull+delete) | DONE |
| 48 | 4 HTTP-level e2e tests (start real server, POST requests, parse JSON responses — user register+get, auth token, friend add+list, msg send+search) | DONE |
| 49-50 | Webhook triggers wired into 5 API handlers (registerUser, addFriend, createGroup, joinGroup, sendMsg, revokeMsg) + 3 e2e webhook tests | DONE |

### Performance Benchmarks
- JSON parse: **1.36M ops/sec**
- JSON stringify: **1.47M ops/sec**
- HashMap put: **7.09M ops/sec**
- Cache set+get: **3.97M ops/sec**
- MsgTransfer enqueue: **38.4M ops/sec**

---

## 11. Production Deployment

### Docker

Multi-stage build: `gcc:13` (build) to `debian:bookworm-slim` (runtime).

```bash
# Build image
docker build -t miku:latest .

# Run API gateway
docker run -p 10002:10002 miku:latest ./bin/miku-api

# Run with TLS
docker run -p 443:10002 \
  -v /etc/ssl/cert.pem:/tls/cert.pem \
  -v /etc/ssl/key.pem:/tls/key.pem \
  miku:latest ./bin/miku-api
```

### CI/CD

GitHub Actions pipeline (`.github/workflows/ci.yml`):
- **build**: CMake debug build with tests disabled
- **test**: Full build with tests, runs `miku_tests`
- **docker-build**: Validates Docker image builds successfully

### Monitoring

- `GET /health` - Liveness check (returns `{"status":"ok"}`)
- `GET /admin/stats` - JSON service stats
- `GET /admin/metrics` - Prometheus text format metrics
- `GET /version` - Build version info

---

## 12. Request Lifecycle (API Gateway)

Every HTTP request passes through this pipeline in `miku_api.c`:

```
Client POST → HTTP parse → route dispatch → handler → response
                                         │
                                         ├─ 1. check_ratelimit()  → 429 if exceeded
                                         ├─ 2. parse_body()       → JSON object
                                         ├─ 3. require_fields()   → 400 if missing
                                         ├─ 4. method dispatch    → exact path hash O(1)
                                         ├─ 5. RPC handler        → business logic
                                         ├─ 6. webhook fire       → post-success
                                         └─ 7. json_resp()        → serialize
```

### Rate Limiting
- `miku_ratelimit_create(60000, 100)` — 100 requests/minute per user key
- Key extraction: `userID` or `ownerUserID` from request body, fallback `"global"`
- Key copied to stack buffer to avoid use-after-free (JSON parser returns views)
- Lookup via FNV-1a open addressing (`MK_RL_HASH=8192`) — O(1) average per allow/remaining
- Returns `HTTP 429 + {"errCode":429,"errMsg":"rate limit exceeded"}`

### Request Validation
- `require_fields(j, resp, "field1", "field2", ..., NULL)` — variadic helper
- Checks each field exists in JSON and string values are non-empty
- Returns `HTTP 400 + {"errCode":400,"errMsg":"missing required fields: field1,field2"}`
- Applied to write endpoints: auth (userID,secret), user (userID),
  friend (ownerUserID,friendUserID), group (ownerUserID,groupName),
  conv (userID,conversationID), msg (sendID,recvID,content)

### Webhook Triggers
Fired after successful RPC completion (errCode == 0):
- `registerUser` → `MK_WH_USER_ONLINE`
- `addFriend` → `MK_WH_AFTER_ADD_FRIEND`
- `createGroup` → `MK_WH_AFTER_CREATE_GROUP`
- `joinGroup` → `MK_WH_AFTER_JOIN_GROUP`
- `sendMsg` → `MK_WH_AFTER_SEND_MSG`
- `revokeMsg` → `MK_WH_MSG_REVOKE`

## 13. API Route Table

203 routes across 17 service groups:

### Auth (5 routes)
| Method | Path | RPC Method |
|--------|------|------------|
| POST | `/auth/user_token` | userToken |
| POST | `/auth/parse_token` | parseToken |
| POST | `/auth/admin_token` | adminToken |
| POST | `/auth/force_logout` | forceLogout |
| POST | `/auth/force_logout_all` | forceLogoutAll |

### User (32 routes)
| Method | Path | RPC Method |
|--------|------|------------|
| POST | `/user/register` | registerUser |
| POST | `/user/update_user_info` | updateUserInfo |
| POST | `/user/update_user_info_ex` | updateUserInfoEx |
| POST | `/user/get_users_info` | getUsersInfo |
| POST | `/user/get_all_users` | getAllUsers |
| POST | `/user/get_all_users_uid` | getAllUsersUID |
| POST | `/user/get_users` | getUsers |
| POST | `/user/account_check` | accountCheck |
| POST | `/user/count` | getUserCount |
| POST | `/user/search` | searchUser |
| POST | `/user/get_users_online_status` | getUsersOnlineStatus |
| POST | `/user/get_users_online_token_detail` | getUsersOnlineTokenDetail |
| POST | `/user/set_global_recv_opt` | setGlobalRecvMessageOpt |
| POST | `/user/get_global_recv_opt` | getGlobalRecvMessageOpt |
| POST | `/user/process_user_command` | processUserCommand |
| POST | `/user/process_user_command_add` | processUserCommandAdd |
| POST | `/user/process_user_command_delete` | processUserCommandDelete |
| POST | `/user/process_user_command_get` | processUserCommandGet |
| POST | `/user/process_user_command_get_all` | processUserCommandGetAll |
| POST | `/user/process_user_command_update` | processUserCommandUpdate |
| POST | `/user/get_user_status` | getUserStatus |
| POST | `/user/update_user_status` | updateUserStatus |
| POST | `/user/set_user_status` | setUserStatus |
| POST | `/user/get_subscribe_users_status` | getSubscribeUsersStatus |
| POST | `/user/subscribe_or_cancel_user_status` | subscribeOrCancelUserStatus |
| POST | `/user/add_notification_account` | addNotificationAccount |
| POST | `/user/update_notification_account` | updateNotificationAccount |
| POST | `/user/search_notification_account` | searchNotificationAccount |
| POST | `/user/set_user_client_config` | setUserClientConfig |
| POST | `/user/get_user_client_config` | getUserClientConfig |
| POST | `/user/del_user_client_config` | delUserClientConfig |
| POST | `/user/page_user_client_config` | pageUserClientConfig |
| GET | `/user/get_users_info` | getUsersInfo |

### Friend (26 routes)
| Method | Path | RPC Method |
|--------|------|------------|
| POST | `/friend/add` | addFriend |
| POST | `/friend/delete` | deleteFriend |
| POST | `/friend/get_friend_list` | getFriendList |
| POST | `/friend/set_friend_remark` | setFriendRemark |
| POST | `/friend/is_friend` | isFriend |
| POST | `/friend/get_friend_apply_list` | getFriendApplyList |
| POST | `/friend/get_self_apply_list` | getSelfApplyList |
| POST | `/friend/get_designated_apply` | getDesignatedFriendsApply |
| POST | `/friend/add_black` | addBlack |
| POST | `/friend/remove_black` | removeBlack |
| POST | `/friend/get_black_list` | getBlackList |
| POST | `/friend/import_friend` | importFriend |
| POST | `/friend/get_specified_friends_info` | getSpecifiedFriendsInfo |
| POST | `/friend/get_designated_friends` | getDesignatedFriends |
| POST | `/friend/get_friend_id` | getFriendID |
| POST | `/friend/get_full_friend_user_ids` | getFullFriendUserIDs |
| POST | `/friend/get_self_unhandled_apply_count` | getSelfUnhandledApplyCount |
| POST | `/friend/get_incremental_friends` | getIncrementalFriends |
| POST | `/friend/get_incremental_blacks` | getIncrementalBlacks |
| POST | `/friend/get_specified_blacks` | getSpecifiedBlacks |
| POST | `/friend/sync_friend` | syncFriend |
| POST | `/friend/update_friends` | updateFriends |
| POST | `/friend/add_friend_response` | addFriendResponse |
| POST | `/friend/accept_apply` | acceptFriendApply |
| POST | `/friend/refuse_apply` | refuseFriendApply |
| POST | `/friend/delete_friend` | deleteFriend |

### Group (35 routes)
| Method | Path | RPC Method |
|--------|------|------------|
| POST | `/group/create` | createGroup |
| POST | `/group/set_group_info` | setGroupInfo |
| POST | `/group/set_group_info_ex` | setGroupInfoEx |
| POST | `/group/get_group_info` | getGroupInfo |
| POST | `/group/get_groups_info` | getGroupsInfo |
| POST | `/group/get_groups` | getGroups |
| POST | `/group/join` | joinGroup |
| POST | `/group/quit` | quitGroup |
| POST | `/group/invite` | inviteToGroup |
| POST | `/group/kick` | kickGroup |
| POST | `/group/transfer` | transferGroup |
| POST | `/group/dismiss` | dismissGroup |
| POST | `/group/mute` | muteGroup |
| POST | `/group/cancel_mute` | cancelMuteGroup |
| POST | `/group/mute_member` | muteGroupMember |
| POST | `/group/cancel_mute_member` | cancelMuteMember |
| POST | `/group/set_group_member_info` | setGroupMemberInfo |
| POST | `/group/get_group_member_list` | getGroupMemberList |
| POST | `/group/get_group_member_user_id` | getGroupMemberUserID |
| POST | `/group/get_groups_info` | getGroupsInfo |
| POST | `/group/get_joined_group_list` | getJoinedGroupList |
| POST | `/group/get_full_join_group_ids` | getFullJoinGroupIDs |
| POST | `/group/get_full_group_member_user_ids` | getFullGroupMemberUserIDs |
| POST | `/group/get_group_abstract_info` | getGroupAbstractInfo |
| POST | `/group/get_incremental_join_groups` | getIncrementalJoinGroups |
| POST | `/group/get_incremental_group_members` | getIncrementalGroupMembers |
| POST | `/group/get_incremental_group_members_batch` | getIncrementalGroupMembersBatch |
| POST | `/group/get_group_application_list` | getGroupApplicationList |
| POST | `/group/get_user_req_group_applicationList` | getUserReqGroupApplicationList |
| POST | `/group/get_group_users_req_application_list` | getGroupUsersReqApplicationList |
| POST | `/group/get_recv_group_applicationList` | getRecvGroupApplicationList |
| POST | `/group/get_specified_user_group_request_info` | getSpecifiedUserGroupRequestInfo |
| POST | `/group/get_group_application_unhandled_count` | getGroupApplicationUnhandledCount |
| POST | `/group/accept_group_application` | acceptGroupApplication |
| POST | `/group/refuse_group_application` | refuseGroupApplication |

### Message (30 routes)
| Method | Path | RPC Method |
|--------|------|------------|
| POST | `/msg/send_msg` | sendMsg |
| POST | `/msg/send` | send |
| POST | `/msg/send_simple_msg` | sendSimpleMsg |
| POST | `/msg/send_business_notification` | sendBusinessNotification |
| POST | `/msg/batch_send` | batchSend |
| POST | `/msg/get_msg` | getMsg |
| POST | `/msg/get` | get |
| POST | `/msg/get_by_seq` | getBySeq |
| POST | `/msg/pull_msg_by_seq` | pullMsgBySeq |
| POST | `/msg/newest_seq` | newestSeq |
| POST | `/msg/get_server_time` | getServerTime |
| POST | `/msg/get_send_status` | getSendStatus |
| POST | `/msg/check_msg_is_send_success` | checkMsgIsSendSuccess |
| POST | `/msg/delete_msg` | deleteMsg |
| POST | `/msg/delete_msg_physical` | deleteMsgPhysical |
| POST | `/msg/delete_msg_phsical_by_seq` | deleteMsgPhysicalBySeq |
| POST | `/msg/clean_up` | cleanUp |
| POST | `/msg/clear_conversation_msg` | clearConversationMsg |
| POST | `/msg/user_clear_all_msg` | userClearAllMsg |
| POST | `/msg/revoke` | revokeMsg |
| POST | `/msg/mark_as_read` | markAsRead |
| POST | `/msg/mark_conversation_as_read` | markConversationAsRead |
| POST | `/msg/mark_msgs_as_read` | markMsgsAsRead |
| POST | `/msg/set_conversation_has_read_seq` | setConversationHasReadSeq |
| POST | `/msg/get_conversations_has_read_and_max_seq` | getConversationsHasReadAndMaxSeq |
| POST | `/msg/set_message_reaction_extensions` | setReactionExtensions |
| POST | `/msg/get_message_list_reaction_extensions` | getReactionExtensions |
| POST | `/msg/add_message_reaction_extensions` | addReactionExtensions |
| POST | `/msg/delete_message_reaction_extensions` | deleteReactionExtensions |
| POST | `/msg/search_msg` | searchMsg |

### Conversation (21 routes)
| Method | Path | RPC Method |
|--------|------|------------|
| POST | `/conversation/get_all` | getAllConversations |
| POST | `/conversation/get_all_conversations` | getAllConversations |
| POST | `/conversation/get_conversation_list` | getConversationList |
| POST | `/conversation/get_conversations` | getConversations |
| POST | `/conversation/get_conv` | getConv |
| POST | `/conversation/get_full_conversation_ids` | getFullConversationIDs |
| POST | `/conversation/get_incremental_conversations` | getIncrementalConversations |
| POST | `/conversation/get_sorted_conversation_list` | getSortedConversationList |
| POST | `/conversation/get_owner_conversation` | getOwnerConversation |
| POST | `/conversation/get_not_notify_conversation_ids` | getNotNotifyConversationIDs |
| POST | `/conversation/get_pinned_conversation_ids` | getPinnedConversationIDs |
| POST | `/conversation/get_total_unread` | getTotalUnread |
| POST | `/conversation/set` | setConversation |
| POST | `/conversation/set_conversations` | setConversations |
| POST | `/conversation/set_conversation_min_seq` | setConversationMinSeq |
| POST | `/conversation/update_conversations_by_user` | updateConversationsByUser |
| POST | `/conversation/delete_conversation` | deleteConversation |
| POST | `/conversation/delete_conversations` | deleteConversations |
| POST | `/conversation/clear_conv_msg` | clearConvMsg |
| POST | `/conversation/mark_as_read` | markAsRead |
| POST | `/conversation/pin_conversation` | pinConversation |

### Third-Party (15 routes)
| Method | Path | RPC Method |
|--------|------|------------|
| POST | `/third/get_upload_token` | getUploadToken |
| POST | `/third/get_download_url` | getDownloadUrl |
| POST | `/third/fcm_update_token` | fcmUpdateToken |
| POST | `/third/set_app_badge` | setAppBadge |
| POST | `/third/log` | log |
| POST | `/third/upload_log` | uploadLog |
| POST | `/third/delete_log` | deleteLog |
| POST | `/third/search_server` | searchServer |
| POST | `/third/get_upload_token_v2` | getUploadTokenV2 |
| POST | `/third/access_url` | accessURL |
| POST | `/third/initiate_multipart_upload` | initiateMultipartUpload |
| POST | `/third/complete_multipart_upload` | completeMultipartUpload |
| POST | `/third/get_signal_invitation_info` | getSignalInvitationInfo |
| POST | `/third/get_signal_invitation_info_start_app` | getSignalInvitationInfoStartApp |
| POST | `/third/fcm_update_token_v2` | fcmUpdateTokenV2 |

### Object/S3 (8 routes)
| Method | Path | Handler |
|--------|------|---------|
| POST | `/object/put` | objectPut |
| POST | `/object/get` | objectGet |
| POST | `/object/delete` | objectDelete |
| POST | `/object/list` | objectList |
| POST | `/object/initiate_multipart` | initiateMultipart |
| POST | `/object/complete_multipart` | completeMultipart |
| POST | `/object/upload_part` | uploadPart |
| POST | `/object/abort_multipart` | abortMultipart |

### Batch (2 routes)
| Method | Path | Handler |
|--------|------|---------|
| POST | `/batch/get_users_info` | batchGetUsersInfo |
| POST | `/batch/delete_friend` | batchDeleteFriend |

### Statistics (4 routes)
| Method | Path | Handler |
|--------|------|---------|
| POST | `/statistics/user/active` | activeUserCount |
| POST | `/statistics/user/register` | registerUserCount |
| POST | `/statistics/group/active` | activeGroupCount |
| POST | `/statistics/msg/send` | msgSendCount |

### JSSDK (2 routes)
| Method | Path | Handler |
|--------|------|---------|
| POST | `/jssdk/get_download_url` | jssdkGetDownloadUrl |
| POST | `/jssdk/get_upload_token` | jssdkGetUploadToken |

### Prometheus Discovery (11 routes)
| Method | Path | Handler |
|--------|------|---------|
| GET | `/prometheus_discovery/api` | API service |
| GET | `/prometheus_discovery/user` | User RPC |
| GET | `/prometheus_discovery/friend` | Friend RPC |
| GET | `/prometheus_discovery/group` | Group RPC |
| GET | `/prometheus_discovery/msg` | Message RPC |
| GET | `/prometheus_discovery/conversation` | Conversation RPC |
| GET | `/prometheus_discovery/third` | Third-party RPC |
| GET | `/prometheus_discovery/auth` | Auth RPC |
| GET | `/prometheus_discovery/push` | Push service |
| GET | `/prometheus_discovery/msg_gateway` | WS Gateway |
| GET | `/prometheus_discovery/msg_transfer` | MsgTransfer |

### Config Manager (6 routes)
| Method | Path | Handler |
|--------|------|---------|
| POST | `/config/get_config` | getConfig |
| POST | `/config/set_config` | setConfig |
| POST | `/config/reset_config` | resetConfig |
| POST | `/config/get_config_list` | getConfigList |
| POST | `/config/get_enable_config_manager` | getEnableConfigManager |
| POST | `/config/set_enable_config_manager` | setEnableConfigManager |

### Restart (1 route)
| Method | Path | Handler |
|--------|------|---------|
| POST | `/restart` | restartServer |

### Admin (4 routes)
| Method | Path | Handler |
|--------|------|--------|
| GET | `/admin/health` | Health check |
| GET | `/admin/stats` | Service stats (JSON) |
| GET | `/admin/metrics` | Prometheus metrics |
| POST | `/admin/shutdown` | Trigger graceful shutdown |

### Version (1 route)
| Method | Path | Handler |
|--------|------|--------|
| GET | `/version` | Build version |

---

## 14. Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Hash map deletion | Open addressing with tombstone (`HM_DELETED`) | Avoids pointer chaining, cache-friendly |
| Token format | `miku_{userID}_{uuid}_{platform}` | Stateless, verifiable without DB lookup |
| Token secret | `"openIM123"` | Matches OpenIM default for compatibility |
| Error response | `{"errCode":N,"errMsg":"...","errDmg":"..."}` | Matches OpenIM error format |
| HTTP server model | Synchronous/blocking (main thread) | Simple, correct; epoll-based I/O multiplexing |
| Keep-alive | One request per epoll wake-up, fd stays registered | No pipelining complexity, simple state machine |
| Connection tracking | `conn_fds[]`/`conn_last_active[]` arrays | Direct index by fd, O(1) lookup |
| Middleware order | CORS -> request_id -> logging -> auth -> stats | CORS first (preflight), auth before business logic |
| Conditional compilation | `MIKU_ENABLE_*` CMake flags | Stub fallbacks when external services unavailable |
| TLS integration | `MIKU_READ`/`MIKU_WRITE` macros | Transparent SSL/non-SSL operation |
| Log rotation | Size-based, sequential rename chain | Simple, no external dependencies |
| Metrics format | Prometheus text format at `/admin/metrics` | Standard, compatible with any monitoring stack |
| UUID generation | `miku_uuid_generate(char out[37])` | Single buffer, no allocation |
| Rate limiting | Per-user sliding window (userID/ownerUserID from body) | 100 req/min default, returns 429 when exceeded |
| Request validation | `require_fields()` variadic helper | Returns 400 with missing field names for write endpoints |
| Webhook triggers | API gateway layer, post-success | Fires AFTER_ADD_FRIEND, AFTER_CREATE_GROUP, AFTER_SEND_MSG etc. |
