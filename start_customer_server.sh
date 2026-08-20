#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PUBLIC_CHAIN_DIR="$SCRIPT_DIR/Code/PublicChain"
SNAPSHOT_QR_DIR="$SCRIPT_DIR/Code/SnapshotQRCode"
SNAPSHOT_QR_BUILD_DIR="$SNAPSHOT_QR_DIR/build"
SNAPSHOT_QR_BINARY="$SNAPSHOT_QR_BUILD_DIR/snapshot_qr"
QR_DISPLAY_PORT="${QR_DISPLAY_PORT:-8084}"
QR_DISPLAY_HOST="${QR_DISPLAY_HOST:-0.0.0.0}"
CHAIN_ID="${PUBLIC_CHAIN_ID:-31337}"
DEPLOYMENT_FILE="$PUBLIC_CHAIN_DIR/deployments/$CHAIN_ID.json"
CUSTOMER_DISPLAY_URL="${CONSUMER_PUBLIC_URL:-http://127.0.0.1:${CONSUMER_PORT:-8082}}"

if [[ ! -d "$PUBLIC_CHAIN_DIR/node_modules" ]]; then
    echo "PublicChain dependencies are missing." >&2
    echo "Run npm install in Code/PublicChain first." >&2
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "CMake is required to build the QR Code generator." >&2
    exit 1
fi

echo "Preparing the Snapshot QR Code generator..."
cmake -S "$SNAPSHOT_QR_DIR" -B "$SNAPSHOT_QR_BUILD_DIR"
cmake --build "$SNAPSHOT_QR_BUILD_DIR" --target snapshot_qr
export QR_GENERATOR_BINARY="${QR_GENERATOR_BINARY:-$SNAPSHOT_QR_BINARY}"

if ! command -v lsof >/dev/null 2>&1; then
    echo "lsof is required to check the local Hardhat port." >&2
    exit 1
fi

hardhat_is_running() {
    lsof -nP -iTCP:8545 -sTCP:LISTEN >/dev/null 2>&1
}

qr_display_is_running() {
    lsof -nP -iTCP:"$QR_DISPLAY_PORT" -sTCP:LISTEN >/dev/null 2>&1
}

cd "$PUBLIC_CHAIN_DIR"
hardhat_started=0

if ! hardhat_is_running; then
    echo "Starting the local Hardhat node on http://127.0.0.1:8545"
    npm run node > "$PUBLIC_CHAIN_DIR/hardhat-node.log" 2>&1 &
    hardhat_pid=$!
    hardhat_started=1

    for _ in {1..30}; do
        if hardhat_is_running; then
            break
        fi
        sleep 1
    done

    if ! hardhat_is_running; then
        echo "Hardhat did not start. See $PUBLIC_CHAIN_DIR/hardhat-node.log" >&2
        kill "$hardhat_pid" 2>/dev/null || true
        exit 1
    fi
else
    echo "Using the existing Hardhat node on http://127.0.0.1:8545"
fi

if [[ "$hardhat_started" -eq 1 || ! -s "$DEPLOYMENT_FILE" ]]; then
    echo "Deploying SnapshotGateway to the local chain..."
    npm run deploy:local
else
    echo "Using the existing SnapshotGateway deployment: $DEPLOYMENT_FILE"
fi

if ! qr_display_is_running; then
    echo "Starting the QR display page on port $QR_DISPLAY_PORT"
    QR_DISPLAY_HOST="$QR_DISPLAY_HOST" \
    QR_DISPLAY_PORT="$QR_DISPLAY_PORT" \
    npm run qr-display > "$PUBLIC_CHAIN_DIR/qr-display.log" 2>&1 &
    qr_display_pid=$!
    for _ in {1..15}; do
        if qr_display_is_running; then
            break
        fi
        sleep 1
    done
    if ! qr_display_is_running; then
        echo "QR display page did not start. See $PUBLIC_CHAIN_DIR/qr-display.log" >&2
        kill "$qr_display_pid" 2>/dev/null || true
        exit 1
    fi
else
    echo "Using the existing QR display page on port $QR_DISPLAY_PORT"
fi

echo "Starting the customer trace service on $CUSTOMER_DISPLAY_URL"
exec npm run consumer
