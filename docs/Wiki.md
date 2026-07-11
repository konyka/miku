# Miku IM Server — 项目 Wiki

> 高性能、高吞吐、分布式即时通讯服务器，纯 C 实现（C99-C23 兼容）
> 基于 OpenIM Server（Go, 47K LOC, 12 微服务）的 C 语言重写

---

## 目录

- [项目概览](#项目概览)
- [系统架构](#系统架构)
- [六层架构详解](#六层架构详解)
  - [1. Foundation 层 — 基础库](#1-foundation-层--基础库)
  - [2. Runtime 层 — 并发运行时](#2-runtime-层--并发运行时)
  - [3. Protocol 层 — 协议与通信](#3-protocol-层--协议与通信)
  - [4. Storage 层 — 数据访问](#4-storage-层--数据访问)
  - [5. Service 层 — 业务服务](#5-service-层--业务服务)
  - [6. Gateway 层 — 网关服务](#6-gateway-层--网关服务)
- [数据模型](#数据模型)
- [微服务架构](#微服务架构)
- [请求生命周期](#请求生命周期)
- [WebSocket 协议](#websocket-协议)
- [消息传输管道](#消息传输管道)
- [推送系统](#推送系统)
- [Webhook 事件系统](#webhook-事件系统)
- [API 路由表](#api-路由表)
- [配置管理](#配置管理)
- [构建与部署](#构建与部署)
- [测试体系](#测试体系)
- [性能基准](#性能基准)
- [关键设计决策](#关键设计决策)

---

## 项目概览

| 指标 | 数值 |
|------|------|
| HTTP 路由 | 203 |
| WS 协议操作码 | 12 |
| C 模块数 | 64 |
| C 头文件数 | 71 |
| 可执行文件 | 13 |
| 测试数 | 157 |
| C 代码行数 | ~9K |
| 构建警告 | 0 |

Miku IM Server 是对 OpenIM Server 的 C 语言重写，实现了 203 条路由、12 个 WS 操作码、7 个业务服务、5 个网关服务、完整的中间件管道、速率限制、Webhook、增量同步等。消息存储在无 Mongo 时使用 8k 内存环，cron `deleteMsg` 与写入同进程；离线推送可配置 `http://` 网关 POST；拆分部署时 `miku-api` 的 `force_logout` 通过 `ws_port+1` 的 `/internal/kick` 踢掉 WS。S3 清理仍待对象存储绑定。API 默认进程内嵌入业务服务；独立 RPC 二进制可用于拆分部署。WS 网关使用 epoll 且握手需 token；Webhook 通过原生 socket 出站 POST；`force_logout` 会吊销已签发 token。

---

## 系统架构

```
┌──────────────────────────────────────────────────────────────┐
│                     Application Layer                         │
│   API Gateway | WS Gateway | MsgTransfer | Push | CronTask   │
├──────────────────────────────────────────────────────────────┤
│                      Service Layer                            │
│   Auth | User | Friend | Group | Conversation | Msg | Third   │
├──────────────────────────────────────────────────────────────┤
│                     Protocol Layer                            │
│   HTTP Parser | WebSocket | Binary RPC | Protobuf | JSON      │
├──────────────────────────────────────────────────────────────┤
│                    Data Access Layer                          │
│   MongoDB | Redis | Kafka | LocalCache | SessionCache | S3    │
├──────────────────────────────────────────────────────────────┤
│                   Concurrency Runtime                         │
│   Coroutine Scheduler | Thread Pool | IO Multiplexer | Timer  │
├──────────────────────────────────────────────────────────────┤
│                    Foundation Layer                           │
│   Arena | Slab | Logger | Config | Error | Stats | HashMap   │
│   String | RBTree | List | Atomic | Spinlock | UUID | SHA1   │
└──────────────────────────────────────────────────────────────┘
```

### 数据流总览

```
客户端 ──HTTP──▶ miku-api (203 routes)
                  │
                  ├── 中间件管道: CORS → RequestID → Logging → Auth → Stats
                  │
                  └── RPC 调用 ──▶ 各业务微服务
                                    │
              客户端 ──WebSocket──▶ miku-msggateway (12 opcodes)
                                    │
                                    ├── 消息入队 ──▶ miku-msgtransfer
                                    │                    │
                                    │                    ├── SPSC Ring Buffer
                                    │                    ├── Batch Pipeline (Redis/Mongo/Push)
                                    │                    └── miku-push ──▶ 离线推送 (FCM/Getui/JPUSH)
                                    │
                                    └── miku-crontask (定时任务: deleteMsg, clearS3)
```

---

## 六层架构详解

### 1. Foundation 层 — 基础库

Foundation 层是整个系统的基石，提供内存管理、日志、配置、数据结构等基础设施，所有上层模块均依赖此层。

#### 1.1 内存管理（Arena + Slab）

Miku 采用两级内存分配策略：

- **Arena 分配器** (`miku_arena.h/c`)：请求作用域的线性分配器。一次请求内的所有分配共享一个 Arena，请求结束后一次性释放，避免频繁 malloc/free 和内存碎片。
  - 基于 block 链表实现，当前 block 空间不足时自动扩展新 block
  - 支持 `miku_arena_alloc_aligned` 对齐分配
  - `miku_arena_reset` 重用 Arena（不释放底层内存，仅重置偏移量）

- **Slab 分配器** (`miku_slab.h/c`)：固定大小对象池，用于频繁分配的连接、消息、缓冲区等对象。基于 free_stack 实现 O(1) 分配和释放。

- **内存池** (`miku_memory.h/c`)：Arena + Slab 的组合封装，提供统一的 `miku_pool_alloc`（变长分配走 Arena）和 `miku_pool_alloc_obj`（固定大小走 Slab）接口。

#### 1.2 日志系统（miku_log）

- 6 个级别：TRACE / DEBUG / INFO / WARN / ERROR / FATAL
- 支持文件+控制台双输出
- 基于大小的日志轮转：`miku.log → miku.log.1 → miku.log.2 → ...`
- `miku_log_set_rotation(max_bytes, max_files)` 配置轮转参数
- 编译器格式检查：`MIKU_FORMAT(4, 5)` 确保格式字符串安全

#### 1.3 配置系统（miku_config）

- 基于 libyaml 的 YAML 解析器
- 支持点分路径嵌套访问：`miku_config_get(cfg, "rpc.auth.port")`
- 文件和字符串两种加载方式
- 服务配置加载器 `miku_service_config` 统一管理多服务配置

#### 1.4 数据结构

| 模块 | 文件 | 说明 |
|------|------|------|
| HashMap | `miku_hashmap.h/c` | FNV-1a 哈希，开放寻址，tombstone 删除，cache-friendly |
| RBTree | `miku_rbtree.h/c` | 红黑树，用于定时器和有序集合 |
| List | `miku_list.h` | 侵入式双向链表 |
| String | `miku_string.h/c` | SDS-like 字符串缓冲区，支持零拷贝视图 `miku_str_t` |

#### 1.5 其他基础模块

| 模块 | 说明 |
|------|------|
| `miku_error.h/c` | 统一错误处理 |
| `miku_thread.h/c` | 线程、互斥锁、条件变量、读写锁封装 |
| `miku_spinlock.h` | 自旋锁（C11 atomic + GCC fallback） |
| `miku_atomic.h` | 原子操作封装 |
| `miku_uuid.h/c` | UUID v4 生成 |
| `miku_crc32.h/c` | CRC32 校验 |
| `miku_base64.h/c` | Base64 编解码 |
| `miku_sha1.h/c` | SHA-1 哈希 |
| `miku_hash.h/c` | 通用哈希函数 |
| `miku_graceful.h/c` | 优雅关闭（SIGTERM/SIGINT + SIGHUP 热重载） |
| `miku_stats.h/c` | 原子服务指标（Prometheus 格式） |
| `miku_version.h` | 编译时版本常量 `0.1.0` |
| `miku_json_util.h` | JSON 辅助宏（`miku_ji`, `miku_jss`, `miku_jerr`） |

#### 1.6 通用类型（miku_common.h）

核心公共头文件，定义了：

- **平台检测**：Linux / macOS / Windows / BSD
- **编译器检测**：GCC / Clang / MSVC
- **C 标准检测**：C99 / C11 / C17 / C23
- **编译器提示宏**：`MIKU_LIKELY/UNLIKELY`, `MIKU_UNUSED`, `MIKU_FORCEINLINE`, `MIKU_MALLOC`, `MIKU_FORMAT` 等
- **通用类型**：`miku_status_t`（错误码枚举）、`miku_str_t`（字符串视图）、`miku_buf_t`（字节缓冲区）
- **字节序工具**：`miku_bswap16/32/64`, `miku_read_be16/32/64`, `miku_write_be16/32/64`
- **时间工具**：`miku_timestamp_ms()`, `miku_timestamp_us()`
- **Defer 模式**：`MIKU_DEFER(fn)` — GCC/Clang cleanup 属性

---

### 2. Runtime 层 — 并发运行时

Runtime 层提供协程、线程池、I/O 多路复用、通道、定时器等并发原语。

#### 2.1 协程系统（ucontext）

基于 POSIX `ucontext` 的有栈协程，每个协程拥有独立栈空间。

```
协程状态机:
  READY ──▶ RUNNING ──▶ SUSPENDED (等待 I/O)
    ▲          │              │
    │          ▼              │
    └── resume │         yield│
               │              │
            DEAD ◀────────────┘
```

关键设计：
- 协程与 I/O 集成：非阻塞 I/O 就绪时注册 fd 到 epoll，yield 挂起；epoll_wait 返回后唤醒对应协程
- 每个协程维护 `wait_fd`、`wait_events`、`deadline_ms` 实现 I/O 等待和超时

#### 2.2 协程调度器（miku_scheduler）

调度器管理协程的生命周期和调度：

- `miku_scheduler_spawn` — 创建新协程
- `miku_scheduler_run` — 运行调度循环
- `miku_scheduler_wakeup` — 唤醒挂起的协程

与线程池集成：每个 Worker 线程运行自己的调度器实例。

#### 2.3 线程池（miku_threadpool）

共享全局队列线程池（Work-Stealing 规划中）：
- 每个 Worker 维护自己的任务队列
- 空闲 Worker 随机从其他 Worker 的队列底部偷取任务
- 支持普通任务提交和协程提交两种模式

```
Worker Thread Loop:
1. 检查本地协程运行队列
2. 如果有就绪协程 → resume
3. 协程执行非阻塞 I/O：
   - 数据就绪 → 立即处理
   - 数据未就绪 → 注册 fd 到 epoll，yield
4. epoll_wait() 等待所有已注册 fd
5. 对每个就绪 fd → 标记对应协程为 READY，放入运行队列
6. 回到步骤 1
```

#### 2.4 I/O 多路复用（miku_io）

统一 I/O 接口，跨平台实现：

| 平台 | 后端 | 文件 |
|------|------|------|
| Linux | epoll | `miku_io_epoll.c` |
| macOS | kqueue | 规划中 |
| Windows | IOCP | 规划中 |

API：`miku_io_add/mod/del/poll` — 事件注册、修改、删除、等待

#### 2.5 通道（miku_channel）

协程间通信的同步通道，类似 Go channel：
- 有界容量缓冲区
- `miku_channel_send/recv` — 发送/接收
- 支持关闭检测

#### 2.6 定时器（miku_timer）

基于最小堆的定时器：
- `miku_timer_add` — 添加定时器（返回 timer_id）
- `miku_timer_cancel` — 取消定时器
- `miku_timer_process` — 处理到期定时器

---

### 3. Protocol 层 — 协议与通信

Protocol 层实现所有通信协议：HTTP、WebSocket、二进制 RPC、Protobuf、JSON、中间件、速率限制、Webhook。

#### 3.1 HTTP 解析器（miku_http）

轻量级 HTTP/1.1 请求解析器：
- 状态机实现，尽可能零拷贝
- 支持 GET/POST/PUT/DELETE/PATCH/HEAD/OPTIONS 7 种方法
- 请求头存储在 HashMap 中
- 请求/响应序列化

#### 3.2 HTTP 服务器（miku_http_server）

生产级 HTTP 服务器特性：

| 特性 | 说明 |
|------|------|
| Keep-Alive | 每次 epoll 唤醒处理一个请求，fd 保持注册 |
| 连接空闲超时 | 可配置（默认 30s），100ms 清理周期 |
| 最大 Body 限制 | 默认 1MB，超限返回 413 |
| TLS 支持 | 可选 OpenSSL 集成 |
| 连接与字节跟踪 | 原子计数器统计 |
| 优雅关闭 | 停止接受新连接，排空进行中请求 |

#### 3.3 WebSocket 服务器（miku_websocket）

RFC 6455 完整实现：
- 帧编解码（文本/二进制/Close/Ping/Pong）
- 握手验证（Sec-WebSocket-Key → Accept 计算）
- 服务器和客户端连接管理

#### 3.4 二进制 RPC 协议（miku_rpc）

自定义二进制 RPC 协议，比 gRPC 更轻量、更快：

```
帧格式:
┌──────┬──────┬──────┬──────────┬──────────┬──────────┬─────────────┐
│MAGIC │ VER  │ TYPE │   LEN    │   SEQ    │ SERVICE  │   PAYLOAD   │
│ 2B   │ 1B   │ 1B   │ 4B       │ 4B       │ 4B+4B   │ variable    │
└──────┴──────┴──────┴──────────┴──────────┴──────────┴─────────────┘

MAGIC:  0x4D4B ("MK")
VER:    协议版本 (1)
TYPE:   1=Call, 2=Reply, 3=Push, 4=Kick
LEN:    帧总长度（大端序）
SEQ:    请求序列号（大端序）
SERVICE: 服务 ID + 方法 ID（各 4B）
PAYLOAD: JSON 编码的业务数据
```

#### 3.5 RPC 服务器（miku_rpc_server）

通用 TCP RPC 服务器：
- 监听 → accept → 读取请求 → dispatch → 写回响应
- 基于 `miku_io` 的 epoll 事件驱动
- 支持统计指标集成
- `MIKU_RPC_MAIN` 宏：统一 RPC 服务启动模板

#### 3.6 Protocol Buffers 编解码器（miku_pb）

自实现的轻量级 PB 编解码器：
- 支持 Wire Type：Varint / Fixed64 / Bytes / Fixed32
- 编码：varint/svarint/fixed32/fixed64/bool/bytes/string
- 解码：`miku_pb_reader_t` 逐字段读取

#### 3.7 JSON 解析器（miku_json）

完整的 JSON 解析和构建器：
- 支持 null/bool/int/double/string/array/object 7 种类型
- 递归下降解析器
- `miku_json_stringify` 序列化
- 性能：1.36M ops/sec（解析），1.47M ops/sec（序列化）

#### 3.8 中间件管道

中间件按链式顺序在路由处理器之前执行：

```
CORS → RequestID → Logging → Auth → Stats → Route Handler
```

| 中间件 | 功能 |
|--------|------|
| `miku_mw_cors` | 设置 `Access-Control-Allow-*` 头 |
| `miku_mw_request_id` | 生成 UUID v4 请求 ID，传播到响应 |
| `miku_mw_logging` | 访问日志：方法、路径、状态码、延迟、请求 ID |
| `miku_mw_auth` | Token 密码学验证（`miku\|uid\|platform\|ts\|nonce\|sig`，FNV-1a 签名），失败返回 401。公开：`/auth/user_token`、`/auth/admin_token`、`/auth/parse_token`、`/admin/health`、`/admin/metrics`、`/version`、`/prometheus*`。`force_logout` 需鉴权 |
| `miku_mw_stats` | 递增请求/错误计数器 |

#### 3.9 速率限制（miku_ratelimit）

滑动窗口速率限制：
- 默认 100 请求/分钟/用户
- Key 提取：从请求 body 中取 `userID` 或 `ownerUserID`
- 超限返回 `HTTP 429 + {"errCode":429,"errMsg":"rate limit exceeded"}`

#### 3.10 Webhook 系统（miku_webhook）

事件驱动的扩展系统，11 种事件类型：

| 事件 | 说明 |
|------|------|
| `BEFORE_SEND_MSG` / `AFTER_SEND_MSG` | 消息发送前后 |
| `BEFORE_ADD_FRIEND` / `AFTER_ADD_FRIEND` | 添加好友前后 |
| `BEFORE_CREATE_GROUP` / `AFTER_CREATE_GROUP` | 创建群组前后 |
| `BEFORE_JOIN_GROUP` / `AFTER_JOIN_GROUP` | 加入群组前后 |
| `USER_ONLINE` / `USER_OFFLINE` | 用户上下线 |
| `MSG_REVOKE` | 消息撤回 |

支持同步和异步触发。`fire()` 将 URL POST 投递到内部线程池（不阻塞 API）；`fire_sync()` 同步等待。失败计入 `total_failed`。

#### 3.11 Gzip 压缩（miku_gzip）

基于 zlib 的压缩/解压缩：
- 4 个压缩级别：NoCompression / Default / BestCompression / BestSpeed
- `miku_gzip_accepts_encoding` 检测客户端是否支持 gzip

---

### 4. Storage 层 — 数据访问

Storage 层封装所有外部存储和缓存，支持条件编译。

#### 4.1 MongoDB（miku_mongo）

基于 mongoc 驱动的 MongoDB 封装：
- 连接池管理
- CRUD 操作：insert / find_one / update / delete
- JSON 文档格式交互
- 条件编译：`MIKU_ENABLE_MONGO`

#### 4.2 Redis（miku_redis）

基于 hiredis 的 Redis 客户端：
- String 操作：set/get/del/exists/incr/expire
- Hash 操作：hset/hget/hdel
- Pub/Sub：publish/subscribe
- 条件编译：`MIKU_ENABLE_REDIS`

#### 4.3 Kafka（miku_kafka）

基于 librdkafka 的消息队列：
- Producer：创建 → 发送消息
- Consumer：创建 → 订阅 → 轮询消费
- 条件编译：`MIKU_ENABLE_KAFKA`

#### 4.4 本地缓存（miku_cache）

LRU + TTL 本地缓存：
- HashMap + 双向链表实现 LRU 淘汰
- TTL 过期检测，`miku_cache_evict_expired` 清理过期条目
- 支持永久缓存和限时缓存
- 无外部依赖，始终可用

#### 4.5 消息存储（miku_msg_store）

消息持久化封装：Mongo 可选；无 Mongo 时使用容量 8192 的内存槽位（空闲栈分配 + `msg_id` 开放寻址哈希，满则按 `send_time` 淘汰最旧）。提供 `purge_older_than` / `clear_user` / `count`，供 cron 与 msgtransfer 使用。

#### 4.6 会话缓存（miku_session_cache）

Redis 会话缓存封装。

#### 4.7 序列号管理（miku_seq）

消息序列号（seq）管理器，用于增量同步。

#### 4.8 增量同步（miku_incr_sync）

增量数据同步模块，支持好友/黑名单/群组/成员/会话的增量拉取。

---

### 5. Service 层 — 业务服务

7 个独立 RPC 业务服务，每个服务作为一个独立进程运行。

#### 5.1 Auth 服务（miku-rpc-auth, 端口 10100）

认证与令牌管理：
- `userToken` — 生成用户 Token（格式 `miku|uid|platform|ts|nonce|sig`，FNV-1a 签名）
- `parseToken` — 解析验证 Token，提取 userID
- `forceLogout` / `forceLogoutAll` — 强制下线
- Token 密钥：`"openIM123"`（兼容 OpenIM）

#### 5.2 User 服务（miku-rpc-user, 端口 10110）

用户管理（32 个 RPC 方法）：
- 注册、查找、更新、批量查询、搜索
- 在线状态管理、全局消息接收设置
- 用户命令处理、通知账号管理
- 客户端配置管理

#### 5.3 Friend 服务（miku-rpc-friend, 端口 10120）

好友关系管理（26 个 RPC 方法）：
- 添加/删除好友、好友列表、判断好友关系
- 好友申请管理（申请/接受/拒绝）
- 黑名单管理
- 好友导入、增量同步

#### 5.4 Group 服务（miku-rpc-group, 端口 10150）

群组管理（35 个 RPC 方法）：
- 创建/设置/获取群信息
- 加入/退出/邀请/踢出/转让/解散群组
- 禁言管理（群级/成员级）
- 群成员管理、群申请管理
- 增量同步

#### 5.5 Message 服务（miku-rpc-msg, 端口 10130）

消息核心（30 个 RPC 方法）：
- 发送消息（单条/批量/简单/业务通知）
- 拉取消息（按 seq / 按会话）
- 消息撤回、标记已读
- 消息删除（逻辑/物理）
- 会话已读 seq 管理
- 消息反应扩展（Reaction）
- 消息搜索

#### 5.6 Conversation 服务（miku-rpc-conversation, 端口 10180）

会话管理（21 个 RPC 方法）：
- 获取全部/排序/指定会话
- 设置/更新/删除会话
- 置顶会话、免打扰
- 已读管理、未读计数
- 增量同步

#### 5.7 Third 服务（miku-rpc-third, 端口 10200）

第三方服务（15 个 RPC 方法）：
- 文件上传/下载 Token
- FCM 推送 Token 更新
- 应用角标设置
- 日志管理
- S3 分片上传
- 信令邀请信息

---

### 6. Gateway 层 — 网关服务

5 个网关服务，处理外部请求接入和消息流转。

#### 6.1 API 网关（miku-api, 端口 10002）

HTTP API 入口，203 条路由：
- 17 个服务组的路由分发
- 中间件管道：CORS → RequestID → Logging → Auth → Stats
- 速率限制：100 请求/分钟/用户
- 请求验证：`require_fields()` 变参辅助
- Webhook 触发：业务完成后异步通知
- 统一错误响应：`{"errCode":N,"errMsg":"...","errDmg":"..."}`

上下文结构 `miku_api_ctx_t` 聚合了所有业务服务实例、统计、速率限制器和 Webhook。

#### 6.2 消息网关（miku-msggateway, 端口 10001）

WebSocket 消息网关，支持 4096 并发客户端：
- 12 个操作码处理
- 用户认证与连接管理
- 消息广播与定向推送
- 用户踢出
- 后台/前台状态切换

#### 6.3 消息传输（miku-msgtransfer）

消息传输队列：
- SPSC Ring Buffer（16384 容量）
- Batch Pipeline：Redis 写入 → MongoDB 持久化 → Push 推送回调
- 吞吐量：38.4M ops/sec（入队）

#### 6.4 推送服务（miku-push）

在线/离线推送通知：
- 在线推送：通过 WS Gateway 定向推送
- 离线推送：4 种 Provider
  - **FCM** — Firebase Cloud Messaging
  - **Getui** — 个推
  - **JPUSH** — 极光推送
  - **Dummy** — 空实现（测试用）
- `miku_offline_push_set_endpoint(url)`：配置 `http://` 网关后，非 DUMMY 会 POST JSON（token/title/content）；未配置时 dry-run
- 订阅/取消订阅管理

#### 6.5 定时任务（miku-crontask）

定时任务调度器（最大 256 个任务）：
- `deleteMsg` — 按保留天数调用 `miku_msg_store_purge_older_than`；**内存 store 与写入同进程**（`miku-msgtransfer` / `miku-dev`），避免跨进程空跑
- `clearUserMsg` — 调用 `miku_msg_store_clear_user` 清理指定用户消息
- `clearS3` — 定期清理 S3 过期文件（仍待对象存储绑定）
- `miku_cron_tasks_set_msg_store` 绑定存储；`miku-crontask` 独立进程不持有私有内存环
- 可扩展的任务注册机制

WS 入站 opcode 帧会解包 `data` 再交给 handler（与出站 `{"reqIdentifier","data"}` 对称）；`LOGOUT` 会断开该连接。

---

## 数据模型

核心数据模型定义在 `src/models/miku_models.h`：

### 用户（miku_user_t）
| 字段 | 大小 | 说明 |
|------|------|------|
| user_id | 64B | 用户唯一 ID |
| nickname | 64B | 昵称 |
| face_url | 256B | 头像 URL |
| gender | 4B | 性别 |
| phone_number | 20B | 手机号 |
| email | 64B | 邮箱 |
| birth | 8B | 生日时间戳 |
| ex | 1024B | 扩展字段 |
| app_mgr_level | 4B | 管理员级别 |
| global_recv_msg_opt | 4B | 全局消息接收选项 |

### 好友（miku_friend_t）
| 字段 | 大小 | 说明 |
|------|------|------|
| owner_user_id | 64B | 所属用户 ID |
| friend_user_id | 64B | 好友用户 ID |
| remark | 64B | 好友备注 |
| add_source | 4B | 添加来源 |
| ex | 1024B | 扩展字段 |

### 群组（miku_group_t / miku_group_member_t）
- 群组：group_id, group_name, face_url, owner_user_id, group_type, member_count, status, ex
- 群成员：group_id, user_id, role_level, join_source, operator_id, join_time, ex

### 消息（miku_msg_t）
| 字段 | 大小 | 说明 |
|------|------|------|
| server_msg_id | 64B | 服务端消息 ID |
| client_msg_id | 64B | 客户端消息 ID |
| send_id | 64B | 发送者 ID |
| recv_id | 64B | 接收者 ID |
| session_type | 4B | 会话类型 |
| msg_type | 4B | 消息类型（101-200） |
| content | 1024B | 消息内容 |
| seq | 8B | 序列号 |
| send_time | 8B | 发送时间 |

消息类型枚举：
- 101=文本, 102=图片, 103=语音, 104=视频, 105=文件, 106=@, 107=位置, 108=自定义, 109=撤回, 114=好友通知, 200=系统消息

### 会话（miku_conversation_t）
- conversation_id, owner_user_id, conversation_type, user_id, group_id
- recv_msg_opt, unread_count, latest_msg_send_time, latest_msg_content
- draft_text, is_pinned, is_private_chat, burn_duration, ex

### 令牌（miku_token_info_t）
- token (512B), user_id, platform, expire_at

---

## 微服务架构

13 个可执行文件，每个服务遵循统一的启动模式：

```
main() {
  1. 解析命令行参数
  2. 加载 YAML 配置
  3. 初始化日志
  4. 初始化内存池
  5. 初始化线程池 + 协程调度器
  6. 连接基础设施（MongoDB, Redis, Kafka）
  7. 注册服务发现（etcd）
  8. 启动 RPC/HTTP/Kafka 服务
  9. 安装信号处理（SIGTERM/SIGINT/SIGHUP）
  10. 阻塞等待关闭信号
  11. 优雅关闭：排空连接 → 刷写数据 → 注销服务
}
```

| 服务 | 端口 | 类型 | 说明 |
|------|------|------|------|
| miku-api | 10002 | HTTP | API 网关 (203 路由) |
| miku-msggateway | 10001 | WebSocket | 消息网关 (12 操作码) |
| miku-msgtransfer | — | 消息队列 | 消息传输管道 |
| miku-push | — | 推送 | 在线/离线推送 |
| miku-crontask | — | 定时 | 定时任务 |
| miku-rpc-auth | 10100 | RPC | 认证服务 |
| miku-rpc-user | 10110 | RPC | 用户管理 |
| miku-rpc-friend | 10120 | RPC | 好友服务 |
| miku-rpc-msg | 10130 | RPC | 消息核心 |
| miku-rpc-group | 10150 | RPC | 群组管理 |
| miku-rpc-conversation | 10180 | RPC | 会话管理 |
| miku-rpc-third | 10200 | RPC | 第三方服务 |
| miku-dev | 10002+10001 | 全集成 | 开发服务器 |

---

## 请求生命周期

每个 HTTP 请求经过以下管道：

```
客户端 POST
  │
  ▼
HTTP 解析 ──▶ 路由分发 ──▶ Handler
                          │
                          ├─ 1. check_ratelimit()  → 429 超限
                          ├─ 2. parse_body()       → JSON 对象
                          ├─ 3. require_fields()   → 400 缺字段
                          ├─ 4. method dispatch    → strstr 链
                          ├─ 5. RPC handler        → 业务逻辑
                          ├─ 6. webhook fire       → 成功后触发
                          └─ 7. json_resp()        → 序列化响应
```

### 请求验证
- `require_fields(j, resp, "field1", "field2", ..., NULL)` — 变参辅助函数
- 检查每个字段是否存在且字符串值非空
- 返回 `HTTP 400 + {"errCode":400,"errMsg":"missing required fields: ..."}`

### Webhook 触发
业务成功（errCode == 0）后触发：
- `registerUser` → `MK_WH_USER_ONLINE`
- `addFriend` → `MK_WH_AFTER_ADD_FRIEND`
- `createGroup` → `MK_WH_AFTER_CREATE_GROUP`
- `joinGroup` → `MK_WH_AFTER_JOIN_GROUP`
- `sendMsg` → `MK_WH_AFTER_SEND_MSG`
- `revokeMsg` → `MK_WH_MSG_REVOKE`

---

## WebSocket 协议

12 个操作码分为 3 类：

### 客户端 → 服务端（1xxx）
| 操作码 | 名称 | 说明 |
|--------|------|------|
| 1001 | GET_NEWEST_SEQ | 获取最新序列号 |
| 1002 | PULL_MSG_BY_SEQ | 按序列号拉取消息 |
| 1003 | SEND_MSG | 发送消息 |
| 1004 | SEND_SIGNAL_MSG | 发送信令消息 |
| 1005 | PULL_MSG | 拉取消息 |
| 1006 | GET_CONV_MAX_READ_SEQ | 获取会话最大已读 seq |
| 1007 | PULL_CONV_LAST_MSG | 拉取会话最新消息 |

### 服务端 → 客户端（2xxx）
| 操作码 | 名称 | 说明 |
|--------|------|------|
| 2001 | PUSH_MSG | 推送消息 |
| 2002 | KICK_ONLINE | 踢下线 |
| 2003 | LOGOUT | 注销 |
| 2004 | SET_BACKGROUND | 后台/前台切换 |
| 2005 | SUB_USER_STATUS | 订阅用户状态 |

### 错误（3xxx）
| 操作码 | 名称 | 说明 |
|--------|------|------|
| 3001 | DATA_ERROR | 数据错误 |

---

## 消息传输管道

```
客户端消息 ──▶ WS Gateway
                  │
                  ▼
            MsgTransfer (SPSC Ring Buffer, 16384)
                  │
                  ▼
            Batch Pipeline
            ┌─────┼─────┐
            ▼     ▼     ▼
         Redis  Mongo  Push
         (缓存) (持久化) (推送)
```

- **SPSC Ring Buffer**：单生产者单消费者环形缓冲，38.4M ops/sec 吞吐
- **Batch Pipeline**：批量刷写，Redis 写入 → MongoDB 持久化 → Push 推送，三个回调顺序执行
- 消费者数量可配置（默认 2）

---

## 推送系统

```
消息到达 ──▶ miku-push
               │
               ├─ 用户在线? ──▶ WS Gateway 在线推送
               │
               └─ 用户离线? ──▶ 离线推送 Provider
                                  ├─ FCM (Firebase)
                                  ├─ Getui (个推)
                                  ├─ JPUSH (极光)
                                  └─ Dummy (测试)
```

---

## Webhook 事件系统

```
业务操作 ──▶ API Gateway
               │
               ├─ 业务处理成功 (errCode == 0)
               │     │
               │     └─▶ miku_webhook_fire(event, payload)
               │              │
               │              ├─ 异步: miku_webhook_fire()
               │              └─ 同步: miku_webhook_fire_sync() → 等待响应
               │
               └─ 业务处理失败 → 不触发
```

---

## API 路由表

203 条路由，17 个服务组：

| 服务组 | 路由数 | 说明 |
|--------|--------|------|
| Auth | 5 | 令牌生成/解析/强制下线 |
| User | 32 | 用户注册/查询/更新/搜索/状态 |
| Friend | 26 | 好友/黑名单/申请管理 |
| Group | 35 | 群组/成员/申请/禁言管理 |
| Message | 30 | 消息发送/拉取/撤回/搜索/反应 |
| Conversation | 21 | 会话管理/置顶/已读 |
| Third | 15 | 文件上传/推送/信令 |
| Object/S3 | 8 | S3 对象存储操作 |
| Batch | 2 | 批量查询/删除 |
| Statistics | 4 | 用户/群组/消息统计 |
| JSSDK | 2 | JS SDK 下载/上传 |
| Prometheus Discovery | 11 | 服务发现端点 |
| Config Manager | 6 | 配置管理 |
| Restart | 1 | 服务重启 |
| Admin | 4 | 健康检查/统计/指标/关闭 |
| Version | 1 | 版本信息 |

---

## 配置管理

YAML 配置文件位于 `config/` 目录：

### share.yml — 服务端口与通用配置
```yaml
listenIP: 0.0.0.0
api:
  port: 10002
  prometheus:
    enable: true
    port: 20100
msggateway:
  port: 10001
rpc:
  auth:
    port: 10100
  user:
    port: 10110
  # ... 各 RPC 服务端口
```

### mongodb.yml — MongoDB 连接配置
```yaml
uri: "mongodb://localhost:27017"
database: "miku"
maxPoolSize: 64
collections:
  user: "user"
  friend: "friend"
  # ...
```

### redis.yml — Redis 连接配置
```yaml
address: "localhost:6379"
password: ""
db: 0
poolSize: 32
```

### kafka.yml — Kafka 配置
```yaml
brokers: "localhost:9092"
topic: "miku_msg"
groupId: "miku-msgtransfer"
```

### log.yml — 日志配置
```yaml
level: "info"
output: "stdout"
file:
  path: "/var/log/miku"
  maxSize: 100
  maxBackups: 5
  compress: true
```

**热重载**：发送 `SIGHUP` 信号触发配置重新加载。
**CLI 覆盖**：`-c <dir>` 配置目录, `-p <port>` API 端口, `-w <port>` WS 端口。

---

## 构建与部署

### 构建

```bash
# Debug 构建（无需外部依赖）
make build

# 运行测试
make test

# 开发服务器（全集成）
make dev

# Release 构建
make release

# Docker 构建
make docker

# ASAN 构建
make asan

# 基准测试
make bench
```

### CMake 直接构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DMIKU_ENABLE_TESTS=ON \
  -DMIKU_ENABLE_MONGO=OFF \
  -DMIKU_ENABLE_REDIS=OFF \
  -DMIKU_ENABLE_KAFKA=OFF \
  -DMIKU_ENABLE_S3=OFF
cmake --build build -j$(nproc)
```

### 条件编译标志

| 标志 | 默认 | 说明 |
|------|------|------|
| `MIKU_ENABLE_TESTS` | OFF | 构建测试套件 |
| `MIKU_ENABLE_MONGO` | ON | MongoDB 驱动 |
| `MIKU_ENABLE_REDIS` | ON | Redis 客户端 |
| `MIKU_ENABLE_KAFKA` | ON | Kafka 生产/消费 |
| `MIKU_ENABLE_S3` | ON | S3/MinIO 存储 |
| `MIKU_ENABLE_TLS` | OFF | OpenSSL TLS |
| `MIKU_USE_ASAN` | OFF | AddressSanitizer |

禁用时模块编译为返回成功/错误码的 stub。

### 部署方式

#### Docker
```bash
docker build -t miku:latest .
docker run -p 10002:10002 miku:latest
```

#### Docker Compose
```bash
make docker-compose-up   # 启动全部 12 个服务
make docker-compose-down # 停止
```

#### Kubernetes
```bash
kubectl apply -f deploy/k8s/
```

#### Helm
```bash
helm install miku deploy/helm/miku/
```

#### Systemd
```bash
sudo bash deploy/systemd/install.sh
```

### CI/CD

GitHub Actions (`.github/workflows/ci.yml`)：
- **build-test**：CMake Debug 构建 + 测试 + 统计
- **docker-build**：验证 Docker 镜像构建

---

## 测试体系

### 测试框架
自研轻量测试框架（cmocka 风格）。

### 测试分类

| 分类 | 测试数 | 说明 |
|------|--------|------|
| Foundation | 20 | 内存池、Arena、Slab、日志、配置、HashMap、字符串、UUID 等 |
| Runtime | 9 | 协程、线程池、调度器、通道、定时器 |
| Protocol | 40 | HTTP 解析、JSON、SHA1、WebSocket、RPC、PB、中间件、路由 |
| Storage | 9 | LRU 缓存、服务发现 |
| Services | 22 | 模型、7 个 RPC 服务、集成测试、认证中间件 |
| New Modules | 23 | IM 消息、消息管道、消息存储、会话缓存等 |
| Benchmarks | 5 | JSON/HashMap/Cache/Queue 性能基准 |
| **总计** | **128+** | + 5 基准测试 |

### 运行测试
```bash
make test           # 或
timeout 60 ./build/bin/miku_tests
```

---

## 性能基准

| 基准 | 吞吐量 |
|------|--------|
| JSON 解析 | 1.36M ops/sec |
| JSON 序列化 | 1.47M ops/sec |
| HashMap Put | 7.09M ops/sec |
| Cache Set+Get | 3.97M ops/sec |
| MsgTransfer 入队 | 38.4M ops/sec |

### 性能目标

| 指标 | 目标 |
|------|------|
| 并发 WebSocket 连接 | 100K+ / 实例 |
| 消息吞吐量 | 500K+ msg/sec |
| 消息延迟 (p99) | < 1ms (同数据中心) |
| RPC 调用开销 | < 50μs |
| 每连接内存 | < 8KB |
| 启动时间 | < 500ms |
| 每服务二进制大小 | < 5MB |

---

## 关键设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| HashMap 删除 | 开放寻址 + 墓碑 | 避免指针链，cache-friendly |
| Token 格式 | `miku\|uid\|platform\|ts_ms\|nonce\|sig` | FNV-1a 签名，24h 过期；`force_logout` 吊销后立即失效 |
| Token 密钥 | `"openIM123"` | 兼容 OpenIM 默认配置 |
| 错误响应 | `{"errCode":N,"errMsg":"...","errDmg":"..."}` | 兼容 OpenIM 错误格式 |
| HTTP 服务器模型 | 同步/阻塞（主线程）+ epoll I/O | 简单正确 |
| Keep-Alive | 每次 epoll 唤醒处理一个请求 | 无管线化复杂性 |
| 连接跟踪 | `conn_fds[]`/`conn_last_active[]` 数组 | 按 fd 索引，O(1) 查找 |
| 中间件顺序 | CORS → RequestID → Logging → Auth → Stats | CORS 先处理预检，Auth 在业务前 |
| 条件编译 | `MIKU_ENABLE_*` CMake 标志 | 禁用时编译为 stub |
| TLS 集成 | `MIKU_READ`/`MIKU_WRITE` 宏 | 透明 SSL/非 SSL 操作 |
| 日志轮转 | 基于大小，顺序重命名链 | 简单，无外部依赖 |
| 指标格式 | Prometheus 文本格式 `/admin/metrics` | 标准兼容 |
| 速率限制 | 每用户滑动窗口 | 100 req/min，超限 429 |
| 请求验证 | `require_fields()` 变参辅助 | 400 返回缺失字段名 |
| Webhook | API 网关层，成功后触发 | 非侵入式扩展 |
| RPC 协议 | 自定义二进制 | 比 gRPC 更轻量更快 |
| 协程模型 | ucontext 有栈协程 | 比 C20 协程兼容性更好 |

---

## 监控端点

| 端点 | 格式 | 说明 |
|------|------|------|
| `GET /health` | JSON | 存活检查 `{"status":"ok"}` |
| `GET /admin/stats` | JSON | 服务统计 |
| `GET /admin/metrics` | Prometheus | 指标数据 |
| `GET /version` | JSON | 构建版本 |

---

## 外部依赖

| 库 | 版本 | 用途 | 必需 |
|----|------|------|------|
| mongoc | 1.27+ | MongoDB C 驱动 | 可选 |
| hiredis | 1.2+ | Redis 异步客户端 | 可选 |
| librdkafka | 2.3+ | Kafka 生产/消费 | 可选 |
| libyaml | 0.2+ | YAML 配置解析 | 是 |
| zlib | 1.3+ | 压缩（gzip, WebSocket） | 是 |
| libcurl | 8.0+ | HTTP 客户端（etcd, S3） | 可选 |
| OpenSSL | 3.0+ | TLS | 可选 |

---

## 目录结构速查

```
miku/
├── src/
│   ├── foundation/     # 基础库（15+ 模块）
│   ├── runtime/        # 并发运行时（7 模块）
│   ├── protocol/       # 协议层（11 模块）
│   ├── storage/        # 数据访问层（8 模块）
│   ├── discovery/      # 服务发现
│   ├── models/         # 数据模型
│   ├── services/       # 业务服务（7 RPC）
│   └── gateway/        # 网关服务（5 服务）
├── cmd/                # 13 个可执行入口
├── config/             # YAML 配置文件
├── tests/              # 测试套件（128+ 测试）
├── deploy/             # 部署配置
│   ├── docker/         # Docker Compose
│   ├── k8s/            # Kubernetes 清单
│   ├── helm/           # Helm Chart
│   └── systemd/        # Systemd 单元
├── docs/               # 文档 + OpenAPI
├── scripts/            # 脚本
└── .github/workflows/  # CI/CD
```
