.PHONY: all build test clean dev dev-api dev-ws distclean

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

docker-run:
	docker run --rm -p 10002:10002 -p 10003:10003 miku:latest

count:
	@echo "=== Lines of Code ==="
	@find src -name '*.c' -o -name '*.h' | xargs wc -l | tail -1
	@echo "=== Modules ==="
	@find src -name '*.c' | wc -l
	@echo "=== Tests ==="
	@grep -c "void test_" tests/*.c 2>/dev/null || true
	@echo "=== Routes ==="
	@grep -c 'miku_http_server_route' src/gateway/api/miku_api.c
