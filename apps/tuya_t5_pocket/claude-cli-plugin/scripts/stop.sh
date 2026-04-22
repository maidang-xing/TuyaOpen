#!/usr/bin/env bash
# NOTE: Untested in v1; Windows is the primary target.
#
# Stop the tuya-pocket-buddy daemon.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

if pid="$(daemon_is_running)"; then
    kill "$pid"
    for _ in 1 2 3 4 5; do
        sleep 0.5
        if ! kill -0 "$pid" 2>/dev/null; then
            break
        fi
    done
    if kill -0 "$pid" 2>/dev/null; then
        kill -9 "$pid" || true
    fi
    rm -f "$PID_FILE"
    echo "tuya-pocket-buddy: stopped"
else
    echo "tuya-pocket-buddy: not running"
    rm -f "$PID_FILE"
fi
