#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
LOG_DIR="$SCRIPT_DIR/logs"
STARTUP_TIMEOUT_SECONDS=120

SERVICE_NAMES=()
SERVICE_PIDS=()
SERVICE_LOGS=()
TAIL_PID=""

cleanup() {
    local exit_code=$?
    local index

    trap - EXIT INT TERM HUP

    if [[ -n "$TAIL_PID" ]]; then
        kill "$TAIL_PID" 2>/dev/null || true
        wait "$TAIL_PID" 2>/dev/null || true
    fi

    if [[ "${#SERVICE_PIDS[@]}" -gt 0 ]]; then
        echo
        echo "Stopping services started by this script..."
    fi

    for ((index = 0; index < ${#SERVICE_PIDS[@]}; index++)); do
        kill -TERM -- "-${SERVICE_PIDS[$index]}" 2>/dev/null || true
    done

    sleep 1

    for ((index = 0; index < ${#SERVICE_PIDS[@]}; index++)); do
        kill -KILL -- "-${SERVICE_PIDS[$index]}" 2>/dev/null || true
        wait "${SERVICE_PIDS[$index]}" 2>/dev/null || true
    done

    if [[ "${#SERVICE_PIDS[@]}" -gt 0 ]]; then
        echo "All managed services have stopped."
    fi

    exit "$exit_code"
}

trap cleanup EXIT
trap 'exit 0' INT TERM HUP

if ! command -v lsof >/dev/null 2>&1; then
    echo "lsof is required to coordinate the local service ports." >&2
    exit 1
fi

mkdir -p "$LOG_DIR"

require_free_port() {
    local port="$1"
    local service_name="$2"

    if lsof -nP -iTCP:"$port" -sTCP:LISTEN >/dev/null 2>&1; then
        echo "$service_name cannot start because port $port is already in use." >&2
        echo "Stop the existing service and run ./start_all.sh again." >&2
        exit 1
    fi
}

wait_for_port() {
    local service_name="$1"
    local port="$2"
    local pid="$3"
    local log_file="$4"
    local attempt

    for ((attempt = 1; attempt <= STARTUP_TIMEOUT_SECONDS; attempt++)); do
        if lsof -nP -iTCP:"$port" -sTCP:LISTEN >/dev/null 2>&1; then
            echo "$service_name is ready on port $port."
            return 0
        fi

        if ! kill -0 "$pid" 2>/dev/null; then
            echo "$service_name exited during startup. Recent log output:" >&2
            tail -n 40 "$log_file" >&2 || true
            return 1
        fi

        sleep 1
    done

    echo "$service_name did not listen on port $port within ${STARTUP_TIMEOUT_SECONDS} seconds." >&2
    echo "Recent log output:" >&2
    tail -n 40 "$log_file" >&2 || true
    return 1
}

start_service() {
    local service_name="$1"
    local script_name="$2"
    local port="$3"
    local log_file="$LOG_DIR/$4"
    local pid

    : > "$log_file"
    echo "Starting $service_name..."
    "$SCRIPT_DIR/$script_name" > "$log_file" 2>&1 &
    pid=$!

    SERVICE_NAMES+=("$service_name")
    SERVICE_PIDS+=("$pid")
    SERVICE_LOGS+=("$log_file")

    wait_for_port "$service_name" "$port" "$pid" "$log_file"
}

require_free_port 8080 "Participant service"
require_free_port 8081 "Administrator service"
require_free_port 8082 "Customer service"

# Monitor mode places each background launcher in its own process group. This
# lets cleanup include the Hardhat and QR display children started by the
# customer launcher without touching services that were already running.
set -m
start_service "Administrator service" "start_control_server.sh" 8081 "control_server.log"
start_service "Participant service" "start_user_server.sh" 8080 "user_server.log"
start_service "Customer service" "start_customer_server.sh" 8082 "customer_server.log"
set +m

USER_URL="http://127.0.0.1:8080/"
CONTROL_URL="http://127.0.0.1:8081/"
CUSTOMER_URL="http://127.0.0.1:8082/"
QR_URL="http://127.0.0.1:8084/"

echo
echo "All services are ready."
echo "Participant:   $USER_URL"
echo "Administrator: $CONTROL_URL"
echo "Customer:      $CUSTOMER_URL"
echo "QR display:    $QR_URL"

if command -v open >/dev/null 2>&1; then
    if ! open "$USER_URL" "$CONTROL_URL" "$CUSTOMER_URL"; then
        echo "The browser could not be opened automatically; use the URLs above." >&2
    fi
else
    echo "The macOS open command is unavailable; use the URLs above." >&2
fi

echo
echo "Combined logs follow. Press Ctrl+C to stop all managed services."
tail -n 12 -F "${SERVICE_LOGS[@]}" &
TAIL_PID=$!

while true; do
    for ((index = 0; index < ${#SERVICE_PIDS[@]}; index++)); do
        if ! kill -0 "${SERVICE_PIDS[$index]}" 2>/dev/null; then
            echo "${SERVICE_NAMES[$index]} stopped unexpectedly. See ${SERVICE_LOGS[$index]}." >&2
            exit 1
        fi
    done
    sleep 2
done
