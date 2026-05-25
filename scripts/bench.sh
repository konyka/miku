#!/usr/bin/env bash
# Miku IM Server - Load Test / Benchmark Runner
# Usage: ./scripts/bench.sh [--api URL] [--duration SECS] [--connections N] [--help]
#
# Prerequisites:
#   - miku-api or miku-dev running on target
#   - curl, ab (Apache Bench) or wrk installed
#
# Examples:
#   ./scripts/bench.sh                          # bench against localhost:10002
#   ./scripts/bench.sh --api http://host:8080   # bench against custom host
#   ./scripts/bench.sh --duration 30            # run for 30 seconds
#   ./scripts/bench.sh --unit                   # run unit benchmarks only

set -euo pipefail

# Defaults
API_URL="http://127.0.0.1:10002"
DURATION=10
CONNECTIONS=50
UNIT_ONLY=0

usage() {
    echo "Miku IM Server - Benchmark Runner"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --api URL          Target API URL (default: http://127.0.0.1:10002)"
    echo "  --duration SECS    Test duration in seconds (default: 10)"
    echo "  --connections N    Concurrent connections (default: 50)"
    echo "  --unit             Run unit benchmarks only (no HTTP)"
    echo "  --help             Show this help"
    echo ""
    echo "Examples:"
    echo "  $0                           # Full bench against localhost"
    echo "  $0 --unit                    # Unit benchmarks (JSON, HashMap, Cache)"
    echo "  $0 --api http://host:8080    # Bench remote server"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --api)        API_URL="$2"; shift 2 ;;
        --duration)   DURATION="$2"; shift 2 ;;
        --connections) CONNECTIONS="$2"; shift 2 ;;
        --unit)       UNIT_ONLY=1; shift ;;
        --help|-h)    usage ;;
        *)            echo "Unknown option: $1"; usage ;;
    esac
done

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

pass() { echo -e "  ${GREEN}PASS${NC} $1"; }
fail() { echo -e "  ${RED}FAIL${NC} $1"; }
info() { echo -e "  ${CYAN}$1${NC}"; }
warn() { echo -e "  ${YELLOW}$1${NC}"; }

header() {
    echo ""
    echo -e "${YELLOW}========================================${NC}"
    echo -e "${YELLOW}  $1${NC}"
    echo -e "${YELLOW}========================================${NC}"
}

###############################################################################
# Unit Benchmarks (built-in miku_tests benchmarks)
###############################################################################
run_unit_benchmarks() {
    header "Unit Benchmarks"

    if [[ ! -f ./build/bin/miku_tests ]]; then
        warn "Building miku_tests..."
        cmake -B build -DCMAKE_BUILD_TYPE=Release \
            -DMIKU_ENABLE_TESTS=ON \
            -DMIKU_ENABLE_MONGO=OFF \
            -DMIKU_ENABLE_REDIS=OFF \
            -DMIKU_ENABLE_KAFKA=OFF \
            -DMIKU_ENABLE_S3=OFF >/dev/null 2>&1
        cmake --build build -j"$(nproc)" >/dev/null 2>&1
    fi

    if [[ -f ./build/bin/miku_tests ]]; then
        info "Running miku_tests (benchmarks included)..."
        timeout 30 ./build/bin/miku_tests 2>&1 | tail -20
        pass "Unit benchmarks complete"
    else
        fail "Could not build miku_tests"
        return 1
    fi
}

###############################################################################
# HTTP Latency Benchmarks
###############################################################################
run_http_benchmarks() {
    header "HTTP Latency Benchmarks (target: $API_URL)"

    # Check server is up
    info "Checking server availability..."
    if ! curl -sf -o /dev/null -m 3 "${API_URL}/health" 2>/dev/null; then
        fail "Server not responding at ${API_URL}/health"
        echo "  Start the server first: ./build/bin/miku-dev"
        return 1
    fi
    pass "Server is up"

    echo ""

    # --- Single-request latency ---
    info "--- Single Request Latency ---"
    for endpoint in "/health" "/version" "/admin/stats"; do
        latency=$(curl -sf -o /dev/null -w "%{time_total}" "${API_URL}${endpoint}" 2>/dev/null || echo "N/A")
        if [[ "$latency" != "N/A" ]]; then
            ms=$(echo "$latency" | awk '{printf "%.2f", $1 * 1000}')
            printf "  %-25s %6s ms\n" "$endpoint" "$ms"
        else
            printf "  %-25s   FAIL\n" "$endpoint"
        fi
    done

    echo ""

    # --- API endpoint latency ---
    info "--- API Endpoint Latency (POST) ---"
    # Auth token request
    latency=$(curl -sf -o /dev/null -w "%{time_total}" \
        -X POST -H "Content-Type: application/json" \
        -d '{"userID":"bench_user","secret":"openIM123","platformID":1}' \
        "${API_URL}/auth/user_token" 2>/dev/null || echo "N/A")
    if [[ "$latency" != "N/A" ]]; then
        ms=$(echo "$latency" | awk '{printf "%.2f", $1 * 1000}')
        printf "  %-25s %6s ms\n" "/auth/user_token" "$ms"
    else
        printf "  %-25s   FAIL\n" "/auth/user_token"
    fi

    # User register
    latency=$(curl -sf -o /dev/null -w "%{time_total}" \
        -X POST -H "Content-Type: application/json" \
        -d '{"userID":"bench_user","nickname":"bench","faceURL":""}' \
        "${API_URL}/user/register" 2>/dev/null || echo "N/A")
    if [[ "$latency" != "N/A" ]]; then
        ms=$(echo "$latency" | awk '{printf "%.2f", $1 * 1000}')
        printf "  %-25s %6s ms\n" "/user/register" "$ms"
    else
        printf "  %-25s   FAIL\n" "/user/register"
    fi

    echo ""

    # --- Throughput with ab (Apache Bench) ---
    info "--- Throughput (ab) ---"
    if command -v ab >/dev/null 2>&1; then
        for endpoint in "/health" "/version"; do
            result=$(ab -n 1000 -c "${CONNECTIONS}" -s 5 "${API_URL}${endpoint}" 2>&1 || true)
            rps=$(echo "$result" | grep "Requests per second" | awk '{print $4}')
            p50=$(echo "$result" | grep "50%" | awk '{print $2}')
            p99=$(echo "$result" | grep "99%" | awk '{print $2}')
            if [[ -n "$rps" ]]; then
                printf "  %-20s  %8s req/s  p50=%-6s  p99=%-6s\n" "$endpoint" "$rps" "${p50}ms" "${p99}ms"
            else
                printf "  %-20s  FAILED\n" "$endpoint"
            fi
        done
    else
        warn "ab (Apache Bench) not found. Install: apt install apache2-utils"
        # Fallback: use curl in a loop
        info "--- Throughput (curl fallback, 100 requests) ---"
        for endpoint in "/health" "/version"; do
            start_ns=$(date +%s%N)
            ok=0
            for i in $(seq 1 100); do
                if curl -sf -o /dev/null -m 2 "${API_URL}${endpoint}" 2>/dev/null; then
                    ((ok++))
                fi
            done
            end_ns=$(date +%s%N)
            elapsed=$(( (end_ns - start_ns) / 1000000 ))
            rps=$(echo "$ok $elapsed" | awk '{if($2>0) printf "%.0f", $1/($2/1000); else print "N/A"}')
            printf "  %-20s  %5s req/s  (%d/%d ok in %dms)\n" "$endpoint" "$rps" "$ok" "100" "$elapsed"
        done
    fi

    echo ""

    # --- Metrics check ---
    info "--- Service Metrics ---"
    metrics=$(curl -sf "${API_URL}/admin/metrics" 2>/dev/null || echo "")
    if [[ -n "$metrics" ]]; then
        echo "$metrics" | grep -E "miku_(requests|errors|active|bytes)" | while read -r line; do
            echo "  $line"
        done
    else
        warn "Could not fetch /admin/metrics"
    fi
}

###############################################################################
# Main
###############################################################################
echo ""
echo "====================================="
echo " Miku IM Server Benchmark Suite"
echo "====================================="
echo " API:         $API_URL"
echo " Duration:    ${DURATION}s"
echo " Connections: $CONNECTIONS"
echo " Unit only:   $UNIT_ONLY"
echo "====================================="

cd "$(dirname "$0")/.."

FAILED=0

run_unit_benchmarks || FAILED=1

if [[ "$UNIT_ONLY" -eq 0 ]]; then
    run_http_benchmarks || FAILED=1
fi

echo ""
if [[ "$FAILED" -eq 0 ]]; then
    pass "All benchmarks completed"
else
    fail "Some benchmarks failed"
    exit 1
fi
