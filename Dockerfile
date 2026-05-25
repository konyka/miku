FROM gcc:13-bookworm AS builder

WORKDIR /build
COPY . .

RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DMIKU_ENABLE_TESTS=OFF \
    -DMIKU_ENABLE_MONGO=OFF \
    -DMIKU_ENABLE_REDIS=OFF \
    -DMIKU_ENABLE_KAFKA=OFF \
    -DMIKU_ENABLE_S3=OFF \
    && cmake --build build -j$(nproc)

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libssl3 && rm -rf /var/lib/apt/lists/*

RUN groupadd -r miku && useradd -r -g miku miku

COPY --from=builder /build/build/bin/* /usr/local/bin/
COPY --from=builder /build/config /etc/miku

USER miku
WORKDIR /etc/miku

EXPOSE 10002 10003

ENTRYPOINT ["miku-api"]
CMD ["-c", "/etc/miku", "-h", "0.0.0.0", "-p", "10002"]
