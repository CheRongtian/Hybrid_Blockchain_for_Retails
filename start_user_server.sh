#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
SERVER_BINARY="$SCRIPT_DIR/Code/build/Server/user_server"

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
    echo "User server was not built: $SERVER_BINARY" >&2
    echo "Build it from Code with: cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

PARTICIPANT_FRONTEND_SOURCE="$SCRIPT_DIR/Code/Frontend/dist/participant"
if [[ ! -f "$PARTICIPANT_FRONTEND_SOURCE/Home.html" ]]; then
    echo "Vue participant frontend was not built." >&2
    echo "Build it from Code/Frontend with: npm install && npm run build:participant" >&2
    exit 1
fi

STALE_FRONTEND_SOURCE="$({
    find \
        "$SCRIPT_DIR/Code/Frontend/apps/participant" \
        "$SCRIPT_DIR/Code/Frontend/shared" \
        "$SCRIPT_DIR/Code/Frontend/package.json" \
        "$SCRIPT_DIR/Code/Frontend/vite.app.config.js" \
        "$SCRIPT_DIR/Code/Frontend/vite.participant.config.js" \
        -type f \
        -newer "$PARTICIPANT_FRONTEND_SOURCE/Home.html" \
        -print -quit
} 2>/dev/null)"

if [[ -n "$STALE_FRONTEND_SOURCE" ]]; then
    echo "Vue participant frontend is older than its source: $STALE_FRONTEND_SOURCE" >&2
    echo "Rebuild it from Code/Frontend with: npm run build:participant" >&2
    exit 1
fi

PARTICIPANT_STATIC_BUILD="$SCRIPT_DIR/Code/build/Server/user_static"
if [[ ! -d "$PARTICIPANT_STATIC_BUILD" ]] ||
   ! diff -qr "$PARTICIPANT_FRONTEND_SOURCE" "$PARTICIPANT_STATIC_BUILD" >/dev/null 2>&1; then
    echo "Participant-page assets have not been synchronized to Code/build." >&2
    echo "Reconfigure and rebuild from Code after the Vue build: cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

ensure_ipfs_service
export IPFS_API_URL="${IPFS_API_URL:-http://127.0.0.1:5002}"

echo "Starting the supply-chain user server on http://127.0.0.1:8080"
exec "$SERVER_BINARY"
