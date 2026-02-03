#!/bin/bash

# Unified benchmark runner for Allocator Test Suite
# Runs benchmarks across all available allocators (glibc, jemalloc, mimalloc, and custom)

set -e

BASE_DIR=$(pwd)
ALLOC_DIR="$BASE_DIR/allocators"
FILTER_ALLOC=""
BENCH_ARGS=""

if [ "$1" == "--help" ] || [ "$1" == "-h" ]; then
    echo "Usage: ./run_benchmarks.sh [allocator] [bench_args]"
    echo ""
    echo "Allocators: glibc, jemalloc, mimalloc, skeleton, custom"
    echo "Default: Runs all available allocators"
    echo ""
    echo "Example: ./run_benchmarks.sh jemalloc --realistic"
    exit 0
fi

# Check if the first argument is a known allocator
case "$1" in
    glibc|jemalloc|mimalloc|custom)
        FILTER_ALLOC="$1"
        shift
        ;;
esac

BENCH_ARGS="$@"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

run_benchmark() {
    local name=$1
    local allocator=$2
    local extra_cflags=$3
    local extra_ldflags=$4

    if [ ! -z "$FILTER_ALLOC" ] && [ "$FILTER_ALLOC" != "$name" ]; then
        return 0
    fi

    echo -e "${GREEN} Running benchmarks for: $name${NC}"

    make clean > /dev/null
    make ALLOCATOR="$allocator" EXTRA_CFLAGS="$extra_cflags" EXTRA_LDFLAGS="$extra_ldflags" bench > /dev/null
    
    ./bin/run_bench $BENCH_ARGS || echo -e "${RED}Benchmarks failed for $name${NC}"
    echo ""
}

# 1. Glibc (Baseline)
run_benchmark "glibc" "allocators/glibc/glibc_allocator.c" "" ""

# 2. Jemalloc (if built)
JEMALLOC_LIB="$ALLOC_DIR/jemalloc/jemalloc_src/lib/libjemalloc.a"
if [ -f "$JEMALLOC_LIB" ]; then
    run_benchmark "jemalloc" "allocators/jemalloc/jemalloc_wrapper.c" \
        "-I$ALLOC_DIR/jemalloc/jemalloc_src/include" \
        "$JEMALLOC_LIB -ldl"
else
    if [ "$FILTER_ALLOC" == "jemalloc" ]; then
        echo -e "${RED}Error: jemalloc is not built.${NC}"
        exit 1
    fi
    [ -z "$FILTER_ALLOC" ] && echo -e "${RED}Skipping jemalloc (not built)${NC}"
fi

# 3. Mimalloc (if built)
MIMALLOC_LIB="$BASE_DIR/build_secure/libmimalloc-secure.a"
if [ -f "$MIMALLOC_LIB" ]; then
    run_benchmark "mimalloc" "allocators/mimalloc/mimalloc_wrapper.c" \
        "-I$ALLOC_DIR/mimalloc/mimalloc_src/include" \
        "$MIMALLOC_LIB -lrt"
else
    if [ "$FILTER_ALLOC" == "mimalloc" ]; then
        echo -e "${RED}Error: mimalloc is not built.${NC}"
        exit 1
    fi
    [ -z "$FILTER_ALLOC" ] && echo -e "${RED}Skipping mimalloc (not built)${NC}"
fi

# 4. Custom Allocator (or Skeleton)
CURRENT_CUSTOM_ALLOC="$CUSTOM_ALLOCATOR"
CURRENT_CUSTOM_NAME="custom"

if [ -z "$CURRENT_CUSTOM_ALLOC" ]; then
    CURRENT_CUSTOM_ALLOC="allocators/skeleton/my_allocator.c"
    CURRENT_CUSTOM_NAME="skeleton"
fi

if [ -f "$CURRENT_CUSTOM_ALLOC" ]; then
    if [ ! -z "$FILTER_ALLOC" ] && [ "$FILTER_ALLOC" != "custom" ] && [ "$FILTER_ALLOC" != "skeleton" ] && [ "$FILTER_ALLOC" != "$CURRENT_CUSTOM_NAME" ]; then
        :
    else
        run_benchmark "$CURRENT_CUSTOM_NAME" "$CURRENT_CUSTOM_ALLOC" "$CUSTOM_CFLAGS" "$CUSTOM_LDFLAGS"
    fi
else
    if [ "$FILTER_ALLOC" == "custom" ] || [ "$FILTER_ALLOC" == "skeleton" ]; then
        echo -e "${RED}Error: Custom/Skeleton allocator not found: $CURRENT_CUSTOM_ALLOC${NC}"
        exit 1
    fi
    [ -z "$FILTER_ALLOC" ] && echo -e "${RED}Skipping custom/skeleton (not found)${NC}"
fi