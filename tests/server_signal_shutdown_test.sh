#!/usr/bin/env bash
set -euo pipefail

server_path="$1"
log_file="$(mktemp)"
pid=""

cleanup() {
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
        kill -KILL "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
    fi
    rm -f "${log_file}"
}
trap cleanup EXIT

"${server_path}" >"${log_file}" 2>&1 &
pid="$!"

for _ in $(seq 1 50); do
    if grep -q "LiteIM server is listening" "${log_file}"; then
        break
    fi
    if ! kill -0 "${pid}" 2>/dev/null; then
        cat "${log_file}"
        if grep -q "Failed to start MySQL pool\\|Failed to start Redis pool" "${log_file}"; then
            exit 77
        fi
        wait "${pid}"
        exit 1
    fi
    sleep 0.05
done

if ! grep -q "LiteIM server is listening" "${log_file}"; then
    cat "${log_file}"
    echo "liteim_server did not start listening before timeout" >&2
    exit 1
fi

kill -TERM "${pid}"

(sleep 2; kill -KILL "${pid}" 2>/dev/null || true) &
killer="$!"

set +e
wait "${pid}"
status="$?"
set -e
pid=""
kill "${killer}" 2>/dev/null || true
wait "${killer}" 2>/dev/null || true

if [[ "${status}" -ne 0 ]]; then
    cat "${log_file}"
    echo "liteim_server exited with status ${status} after SIGTERM" >&2
    exit 1
fi
