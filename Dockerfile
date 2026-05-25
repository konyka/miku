FROM gcc:13-bookworm AS builder

WORKDIR /build
COPY . .

ARG ENABLE_MONGO=OFF
ARG ENABLE_REDIS=OFF
ARG ENABLE_KAFKA=OFF
ARG ENABLE_S3=OFF
ARG ENABLE_TLS=OFF

RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DMIKU_ENABLE_TESTS=OFF \
    -DMIKU_ENABLE_MONGO=${ENABLE_MONGO} \
    -DMIKU_ENABLE_REDIS=${ENABLE_REDIS} \
    -DMIKU_ENABLE_KAFKA=${ENABLE_KAFKA} \
    -DMIKU_ENABLE_S3=${ENABLE_S3} \
    -DMIKU_ENABLE_TLS=${ENABLE_TLS} \
    && cmake --build build -j$(nproc)

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libssl3 && rm -rf /var/lib/apt/lists/*

RUN groupadd -r miku && useradd -r -g miku miku

COPY --from=builder /build/build/bin/* /usr/local/bin/
COPY --from=builder /build/config /etc/miku

USER miku
WORKDIR /etc/miku

EXPOSE 10001 10002 10100 10110 10120 10130 10150 10180 10200

ENTRYPOINT ["miku-api"]
CMD ["-c", "/etc/miku", "-h", "0.0.0.0", "-p", "10002"]
