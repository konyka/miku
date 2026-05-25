# Convenience Makefile for Miku IM Server
#
# Usage:
#   make              - Build all (Release)
#   make debug        - Build with Debug + ASAN
#   make test         - Build and run tests
#   make clean        - Clean build artifacts
#   make install      - Install to /usr/local
#   make <service>    - Build a specific service binary

.PHONY: all debug release test clean install \
        miku-api miku-msggateway miku-msgtransfer miku-push miku-crontask \
        miku-rpc-auth miku-rpc-user miku-rpc-friend miku-rpc-group \
        miku-rpc-conversation miku-rpc-msg miku-rpc-third

BUILD_DIR := build
JOBS      := $(shell nproc 2>/dev/null || echo 4)

# ── Build Targets ────────────────────────────────────────────

all: release

release:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DMIKU_ENABLE_TESTS=ON .
	cmake --build $(BUILD_DIR) -j$(JOBS)

debug:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DMIKU_ENABLE_TESTS=ON -DMIKU_USE_ASAN=ON .
	cmake --build $(BUILD_DIR) -j$(JOBS)

# ── Individual Services ──────────────────────────────────────

miku-api: release
miku-msggateway: release
miku-msgtransfer: release
miku-push: release
miku-crontask: release
miku-rpc-auth: release
miku-rpc-user: release
miku-rpc-friend: release
miku-rpc-group: release
miku-rpc-conversation: release
miku-rpc-msg: release
miku-rpc-third: release

# ── Testing ──────────────────────────────────────────────────

test: debug
	cd $(BUILD_DIR) && ctest --output-on-failure -j$(JOBS)

bench: debug
	./$(BUILD_DIR)/bin/miku_tests 2>&1 | grep -E "ops/sec|Total|FAIL"

run: debug
	./$(BUILD_DIR)/bin/miku-dev -p 10002 -w 10001

valgrind: debug
	cd $(BUILD_DIR) && valgrind --leak-check=full --error-exitcode=1 ./bin/miku_tests

# ── Clean / Install ─────────────────────────────────────────

clean:
	rm -rf $(BUILD_DIR)

install: release
	cmake --install $(BUILD_DIR)

# ── Format ───────────────────────────────────────────────────

format:
	find src cmd tests -name '*.c' -o -name '*.h' | xargs clang-format -i -style=file

.PHONY: format valgrind
