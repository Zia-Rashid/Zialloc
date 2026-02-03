#!/bin/bash
set -e

# Setup script for Allocator Test Suite dependencies
# Usage: ./setup_allocators.sh

BASE_DIR=$(pwd)
ALLOC_DIR="$BASE_DIR/allocators"
LIBS_DIR="$BASE_DIR/libs"

echo "Checking for build dependencies..."
for cmd in git cmake autoconf automake make cc; do
    if ! command -v $cmd &> /dev/null; then
        echo "Error: $cmd is not installed."
        exit 1
    fi
done

echo "Setting up dependencies in $ALLOC_DIR..."
mkdir -p "$ALLOC_DIR"
mkdir -p "$LIBS_DIR"

# --- Mimalloc Setup ---
MIMALLOC_DIR="$ALLOC_DIR/mimalloc/mimalloc_src"
if [ ! -f "$MIMALLOC_DIR/CMakeLists.txt" ]; then
    echo "Cloning mimalloc..."
    rm -rf "$MIMALLOC_DIR"
    git clone https://github.com/microsoft/mimalloc "$MIMALLOC_DIR"
else
    echo "mimalloc source found."
fi

# Build Mimalloc Release
echo "Building mimalloc (release)..."
mkdir -p "$BASE_DIR/build/mimalloc-release"
cd "$BASE_DIR/build/mimalloc-release"
cmake "$MIMALLOC_DIR" -DMI_SECURE=OFF -DMI_BUILD_SHARED=OFF -DMI_BUILD_TESTS=OFF
make -j$(nproc)
cp libmimalloc.a "$LIBS_DIR/libmimalloc.a"
cd "$BASE_DIR"

# Build Mimalloc Secure
echo "Building mimalloc (secure)..."
mkdir -p "$BASE_DIR/build/mimalloc-secure"
cd "$BASE_DIR/build/mimalloc-secure"
cmake "$MIMALLOC_DIR" -DMI_SECURE=ON -DMI_BUILD_SHARED=OFF -DMI_BUILD_TESTS=OFF
make -j$(nproc)
cp libmimalloc-secure.a "$LIBS_DIR/libmimalloc-secure.a"
cd "$BASE_DIR"


# --- Jemalloc Setup ---
JEMALLOC_DIR="$ALLOC_DIR/jemalloc/jemalloc_src"
if [ ! -f "$JEMALLOC_DIR/autogen.sh" ]; then
    echo "Cloning jemalloc..."
    rm -rf "$JEMALLOC_DIR"
    git clone https://github.com/jemalloc/jemalloc "$JEMALLOC_DIR"
else
    echo "jemalloc source found."
fi

echo "Building jemalloc..."
cd "$JEMALLOC_DIR"
if [ ! -f "configure" ]; then
    ./autogen.sh
fi

# Build Jemalloc Release
echo "Configuring jemalloc (release)..."
# We abuse the source tree a bit here, so clean first
if [ -f "Makefile" ]; then make distclean || true; fi

./configure --with-jemalloc-prefix=je_ --disable-shared --enable-static --disable-stats --disable-debug
make -j$(nproc)
cp lib/libjemalloc.a "$LIBS_DIR/libjemalloc.a"

# Build Jemalloc Debug/Secure-ish
echo "Configuring jemalloc (debug)..."
make distclean || true
./configure --with-jemalloc-prefix=je_ --disable-shared --enable-static --enable-debug --enable-fill --enable-prof
make -j$(nproc)
cp lib/libjemalloc.a "$LIBS_DIR/libjemalloc-debug.a"

cd "$BASE_DIR"

echo "Build complete. Artifacts in libs/:"
ls -l "$LIBS_DIR"
