#!/bin/zsh
set -euo pipefail

NTREE_DIR="$(cd "$(dirname "$0")" && pwd)"
NTREE_BUILD_DIR="${NTREE_DIR}/build"

cmake -S "${NTREE_DIR}" -B "${NTREE_BUILD_DIR}"
cmake --build "${NTREE_BUILD_DIR}" --target merkle_ntree

exec "${NTREE_BUILD_DIR}/merkle_ntree" "$@"
