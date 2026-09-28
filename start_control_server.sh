#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
SERVER_BINARY="$SCRIPT_DIR/Code/build/Server/control_server"
CMAKE_CONFIG_MARKER="$SCRIPT_DIR/Code/build/CMakeFiles/cmake.check_cache"

ensure_ipfs_service() {
    local brew_binary=""
    if [[ -x "/opt/homebrew/bin/brew" ]]; then
        brew_binary="/opt/homebrew/bin/brew"
    elif command -v brew >/dev/null 2>&1; then
        brew_binary="$(command -v brew)"
    else
        echo "Homebrew was not found; install Kubo or start IPFS manually." >&2
        exit 1
    fi

    if ! "$brew_binary" services list 2>/dev/null |
        awk '$1 == "kubo" && $2 == "started" { found = 1 }
             END { exit found ? 0 : 1 }'; then
        echo "Starting the Kubo IPFS service..."
        "$brew_binary" services start kubo
    fi
}

if [[ ! -x "$SERVER_BINARY" ]]; then
    echo "Control server was not built: $SERVER_BINARY" >&2
    echo "Build it from Code with: cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

if [[ ! -f "$CMAKE_CONFIG_MARKER" ]]; then
    echo "The CMake configuration marker is missing: $CMAKE_CONFIG_MARKER" >&2
    echo "Configure and rebuild from Code with: cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

STALE_CMAKE_CONFIGURATION="$({
    find \
        "$SCRIPT_DIR/Code/CMakeLists.txt" \
        "$SCRIPT_DIR/Code/PrivateChain/Server" \
        "$SCRIPT_DIR/Code/Snapshot" \
        "$SCRIPT_DIR/Code/SnapshotStorage" \
        "$SCRIPT_DIR/Code/SnapshotScheduler" \
        -type f \
        -name 'CMakeLists.txt' \
        -newer "$CMAKE_CONFIG_MARKER" -print -quit
} 2>/dev/null)"

if [[ -n "$STALE_CMAKE_CONFIGURATION" ]]; then
    echo "The CMake build directory is older than its configuration: $STALE_CMAKE_CONFIGURATION" >&2
    echo "Reconfigure and rebuild from Code with: cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

STALE_SOURCE="$({
    find \
        "$SCRIPT_DIR/Code/PrivateChain/Server" \
        "$SCRIPT_DIR/Code/Snapshot" \
        "$SCRIPT_DIR/Code/SnapshotStorage" \
        "$SCRIPT_DIR/Code/SnapshotScheduler" \
        -type f \
        \( -name '*.cpp' -o -name '*.hpp' \) \
        -newer "$SERVER_BINARY" -print -quit
} 2>/dev/null)"

if [[ -n "$STALE_SOURCE" ]]; then
    echo "Control server is older than its source: $STALE_SOURCE" >&2
    echo "Rebuild it from Code with: cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

CONTROL_FRONTEND_SOURCE="$SCRIPT_DIR/Code/Frontend/dist/control"
if [[ -f "$CONTROL_FRONTEND_SOURCE/Home.html" ]]; then
    CONTROL_STATIC_SOURCE="$CONTROL_FRONTEND_SOURCE"
else
    echo "Vue control frontend was not built." >&2
    echo "Build it from Code/Frontend with: npm install && npm run build:control" >&2
    exit 1
fi

STALE_FRONTEND_SOURCE="$({
    find \
        "$SCRIPT_DIR/Code/Frontend/apps/control" \
        "$SCRIPT_DIR/Code/Frontend/shared" \
        "$SCRIPT_DIR/Code/PrivateChain/Server/control_static" \
        "$SCRIPT_DIR/Code/Frontend/package.json" \
        "$SCRIPT_DIR/Code/Frontend/vite.app.config.js" \
        "$SCRIPT_DIR/Code/Frontend/vite.control.config.js" \
        -type f \
        -newer "$CONTROL_FRONTEND_SOURCE/Home.html" \
        -print -quit
} 2>/dev/null)"

if [[ -n "$STALE_FRONTEND_SOURCE" ]]; then
    echo "Vue control frontend is older than its source: $STALE_FRONTEND_SOURCE" >&2
    echo "Rebuild it from Code/Frontend with: npm run build:control" >&2
    exit 1
fi

CONTROL_STATIC_BUILD="$SCRIPT_DIR/Code/build/Server/control_static"
if [[ ! -d "$CONTROL_STATIC_BUILD" ]] ||
   ! diff -qr "$CONTROL_STATIC_SOURCE" "$CONTROL_STATIC_BUILD" >/dev/null 2>&1; then
    echo "Control-page static files have not been synchronized to Code/build." >&2
    echo "Reconfigure and rebuild from Code after the Vue build: cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

ensure_ipfs_service
export IPFS_API_URL="${IPFS_API_URL:-http://127.0.0.1:5002}"

echo "Starting the supply-chain control server on http://127.0.0.1:8081"
exec "$SERVER_BINARY"
