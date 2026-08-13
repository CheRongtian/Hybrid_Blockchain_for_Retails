#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
SERVER_BINARY="$SCRIPT_DIR/Code/build/Server/control_server"

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

ensure_ipfs_service
export IPFS_API_URL="${IPFS_API_URL:-http://127.0.0.1:5002}"

echo "Starting the supply-chain control server on http://127.0.0.1:8081"
exec "$SERVER_BINARY"
