# Miku IM Server

High-performance, distributed instant messaging server written in pure C (C99-C23).

A complete rewrite of [OpenIM Server](https://github.com/openimsdk/open-im-server) (Go, 12 microservices, 100+ API endpoints) with memory pools, thread pools, ucontext coroutines, and cross-platform I/O.

## Architecture

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
│  MongoDB | Redis | Kafka | LocalCache | Discovery    │
├─────────────────────────────────────────────────────┤
│               Concurrency Runtime                     │
│  Coroutine Scheduler | Thread Pool | IO Multiplexer  │
├─────────────────────────────────────────────────────┤
│                 Foundation Layer                      │
│  Memory Pool | Logger | Config | Stats | Error       │
│  String Buffer | Hash Map | List | Atomic | Spinlock │
└─────────────────────────────────────────────────────┘
```

## Microservices (13 Binaries)

| Service | Port | Description |
|---------|------|-------------|
| `miku-api` | 10002 | HTTP API gateway (103 routes) |
| `miku-msggateway` | 10001 | WebSocket message gateway |
| `miku-msgtransfer` | — | Message transfer queue |
| `miku-push` | — | Push notification service |
| `miku-crontask` | — | Cron task scheduler |
| `miku-rpc-auth` | 10100 | Authentication service |
| `miku-rpc-user` | 10110 | User management |
| `miku-rpc-friend` | 10120 | Friend relationships |
| `miku-rpc-group` | 10150 | Group management |
| `miku-rpc-conversation` | 10180 | Conversation management |
| `miku-rpc-msg` | 10130 | Message core |
| `miku-rpc-third` | 10200 | Third-party services |
| `miku-dev` | 10002+10001 | All-in-one dev server |

## Build

```bash
# Prerequisites (optional, auto-detected):
#   libmongoc, hiredis, librdkafka, libyaml, zlib, libcurl, openssl

# Debug build with tests
make build

# Run tests
make test

# Run all-in-one dev server
make dev

# Release build
make release

# Docker build
make docker

# Clean
make distclean
```

### CMake Directly

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DMIKU_ENABLE_TESTS=ON \
  -DMIKU_ENABLE_MONGO=OFF \
  -DMIKU_ENABLE_REDIS=OFF \
  -DMIKU_ENABLE_KAFKA=OFF \
  -DMIKU_ENABLE_S3=OFF
cmake --build build -j$(nproc)
```

## Run

```bash
# All-in-one dev server
./build/bin/miku-dev -c config/

# Individual services
./build/bin/miku-api -c config/ -p 10002
./build/bin/miku-msggateway -c config/ -p 10001
./build/bin/miku-rpc-auth -c config/
```

Send `SIGTERM` or `Ctrl+C` for graceful shutdown. Send `SIGHUP` to trigger config reload.

## Configuration

YAML config files in `config/`:

| File | Contents |
|------|----------|
| `share.yml` | Listen IP, API/WS/RPC ports, prometheus |
| `mongodb.yml` | URI, database, pool size, collections |
| `redis.yml` | Address, password, DB, pool size |
| `kafka.yml` | Brokers, topic, group ID |
| `log.yml` | Level, output, file rotation |

CLI flags override config: `-c <dir>` config dir, `-p <port>` API/WS port, `-w <port>` WS port.

## Project Stats

- **54 modules** across 6 layers
- **13 binaries** (12 microservices + dev server)
- **103 API routes** (Auth 5, User 16, Friend 15, Group 22, Msg 16, Conv 13, Third 9, Batch 2, Admin 4)
- **100 tests + 5 benchmarks**, all passing
- **Benchmarks**: JSON 1.36M/s, HashMap 7.09M/s, Cache 3.97M/s, Queue 38.4M/s

## Features

- HTTP/1.1 server with keep-alive, TLS (OpenSSL), idle timeout, body size limit
- Middleware pipeline: CORS, request ID, access logging, auth, stats
- WebSocket gateway (RFC 6455) with frame codec
- Custom binary RPC with Protobuf codec
- Prometheus metrics at `/admin/metrics`
- Log rotation (size-based)
- Graceful shutdown (SIGTERM/SIGINT) + config reload (SIGHUP)
- Conditional compilation for all external dependencies
- Docker, docker-compose, Kubernetes manifests, systemd units

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) - Full architecture design document (13 sections)
- [docs/openapi.yaml](docs/openapi.yaml) - OpenAPI 3.0 spec (103 routes)
- [config/miku-example.yml](config/miku-example.yml) - Config example with all options
- [notes.html](notes.html) - Development progress log

## Deployment

```bash
# Docker
make docker
docker run --rm -p 10002:10002 miku:latest

# Docker Compose (all 12 services)
make docker-compose-up

# Kubernetes
kubectl apply -f deploy/k8s/

# Systemd
sudo bash deploy/systemd/install.sh
```

## Tech Stack

- **Language**: C99-C23 (no C++)
- **Memory**: Arena + Slab pools, per-request allocation
- **Concurrency**: ucontext stackful coroutines, work-stealing thread pool
- **I/O**: epoll (Linux), kqueue (macOS planned), IOCP (Windows planned)
- **RPC**: Custom binary protocol with Protobuf codec
- **External**: mongoc, hiredis, librdkafka, libyaml, zlib, libcurl (all optional/conditional)
- **Build**: CMake 3.16+
