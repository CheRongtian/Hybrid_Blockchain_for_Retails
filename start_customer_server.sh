#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PUBLIC_CHAIN_DIR="$SCRIPT_DIR/Code/PublicChain"
CHAIN_ID="${PUBLIC_CHAIN_ID:-31337}"
DEPLOYMENT_FILE="$PUBLIC_CHAIN_DIR/deployments/$CHAIN_ID.json"

if [[ ! -d "$PUBLIC_CHAIN_DIR/node_modules" ]]; then
    echo "PublicChain dependencies are missing." >&2
    echo "Run npm install in Code/PublicChain first." >&2
    exit 1
fi

if ! command -v lsof >/dev/null 2>&1; then
    echo "lsof is required to check the local Hardhat port." >&2
    exit 1
fi

hardhat_is_running() {
    lsof -nP -iTCP:8545 -sTCP:LISTEN >/dev/null 2>&1
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

echo "Starting the customer trace service on http://127.0.0.1:8082"
exec npm run consumer
