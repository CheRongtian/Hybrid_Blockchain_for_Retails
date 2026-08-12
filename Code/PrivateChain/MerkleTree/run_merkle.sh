#!/bin/zsh
set -euo pipefail

MERKLE_SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MERKLE_BUILD_DIR="${MERKLE_SCRIPT_DIR}/build-cli"
MERKLE_INPUT_FILE="${1:-${MERKLE_SCRIPT_DIR}/inp.txt}"

cmake \
    -S "${MERKLE_SCRIPT_DIR}" \
    -B "${MERKLE_BUILD_DIR}" \
    -DMERKLE_TREE_BUILD_CLI=ON

cmake --build "${MERKLE_BUILD_DIR}" --target merkle_cli

exec "${MERKLE_BUILD_DIR}/merkle_cli" "${MERKLE_INPUT_FILE}"
