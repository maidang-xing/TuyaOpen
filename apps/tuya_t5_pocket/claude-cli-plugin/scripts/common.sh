#!/usr/bin/env bash
# NOTE: Untested in v1; Windows is the primary target.
#
# Shared helpers for tuya-pocket-buddy POSIX scripts.
set -euo pipefail

STATE_DIR="${TUYA_POCKET_BUDDY_STATE_DIR:-$HOME/.tuya-pocket-buddy}"
VENV_DIR="$STATE_DIR/venv"
LOG_FILE="$STATE_DIR/daemon.log"
PID_FILE="$STATE_DIR/daemon.pid"
HOOK_BACKUP_PREFIX="settings.json.buddy-backup"

python_cmd() {
    local cmd
    for cmd in python3.12 python3.11 python3.10 python3; do
        if command -v "$cmd" >/dev/null 2>&1; then
            local version
            version="$("$cmd" -c 'import sys; print(f"{sys.version_info[0]}.{sys.version_info[1]}")')"
            if [[ "$(printf '%s\n3.10\n' "$version" | sort -V | head -n1)" == "3.10" ]]; then
                echo "$cmd"
                return 0
            fi
        fi
    done
    echo "tuya-pocket-buddy: Python >= 3.10 is required but was not found on PATH" >&2
    return 1
}

ensure_state_dir() {
    mkdir -p "$STATE_DIR"
}

daemon_is_running() {
    if [[ -f "$PID_FILE" ]]; then
        local pid
        pid="$(cat "$PID_FILE" 2>/dev/null || true)"
        if [[ -n "${pid:-}" ]] && kill -0 "$pid" 2>/dev/null; then
            echo "$pid"
            return 0
        fi
    fi
    return 1
}
