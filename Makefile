.PHONY: all build test clean dev dev-api dev-ws distclean asan bench helm

BUILD_DIR  := build
BIN_DIR    := $(BUILD_DIR)/bin
NPROC      := $(shell nproc)

CMAKE_DEFS := \
	-DCMAKE_BUILD_TYPE=Debug \
	-DMIKU_ENABLE_TESTS=ON \
	-DMIKU_ENABLE_MONGO=OFF \
	-DMIKU_ENABLE_REDIS=OFF \
	-DMIKU_ENABLE_KAFKA=OFF \
	-DMIKU_ENABLE_S3=OFF

all: build

build:
	cmake -B $(BUILD_DIR) $(CMAKE_DEFS)
	cmake --build $(BUILD_DIR) -j$(NPROC)

release:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DMIKU_ENABLE_TESTS=OFF
	cmake --build $(BUILD_DIR) -j$(NPROC)

test: build
	timeout 60 $(BIN_DIR)/miku_tests

clean:
	cmake --build $(BUILD_DIR) --target clean 2>/dev/null || true

distclean:
	rm -rf $(BUILD_DIR)

dev: build
	$(BIN_DIR)/miku-dev -c config/

dev-api: build
	$(BIN_DIR)/miku-api -c config/

dev-ws: build
	$(BIN_DIR)/miku-msggateway -c config/

docker:
	docker build -t miku:latest .

docker-tls:
	docker build --build-arg ENABLE_TLS=ON -t miku:latest .

docker-run:
	docker run --rm -p 10002:10002 miku:latest

docker-compose-up:
	docker-compose -f deploy/docker/docker-compose.yml up -d

docker-compose-down:
	docker-compose -f deploy/docker/docker-compose.yml down

asan:
	@echo "Note: requires libasan and libubsan packages installed"
	cmake -B $(BUILD_DIR)-asan \
		-DCMAKE_BUILD_TYPE=Debug \
		-DMIKU_ENABLE_TESTS=ON \
		-DMIKU_ENABLE_MONGO=OFF \
		-DMIKU_ENABLE_REDIS=OFF \
		-DMIKU_ENABLE_KAFKA=OFF \
		-DMIKU_ENABLE_S3=OFF \
		-DCMAKE_C_FLAGS="-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -g" \
		-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address -fsanitize=undefined"
	cmake --build $(BUILD_DIR)-asan -j$(NPROC)
	@echo "=== Running tests under ASAN ==="
	timeout 60 $(BUILD_DIR)-asan/bin/miku_tests

bench: build
	bash scripts/bench.sh

helm:
	helm lint deploy/helm/miku
	helm template miku deploy/helm/miku

count:
	@echo "=== Lines of Code ==="
	@find src -name '*.c' -o -name '*.h' | xargs wc -l | tail -1
	@echo "=== Modules ==="
	@find src -name '*.c' | wc -l
	@echo "=== Tests ==="
	@grep -h 'mk_run_test(' tests/*.c | wc -l
	@echo "=== Routes ==="
	@grep -c 'miku_http_server_route(' src/gateway/api/miku_api.c
