#!/usr/bin/env bash
# NOTE: Untested in v1; Windows is the primary target.
#
# Dump daemon pid + selected device MAC + tail of the log.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

if pid="$(daemon_is_running)"; then
    echo "daemon.pid       $pid"
else
    echo "daemon.pid       (not running)"
fi

if [[ -f "$STATE_DIR/state.json" ]]; then
    echo "state.json       $STATE_DIR/state.json"
    cat "$STATE_DIR/state.json"
    echo
else
    echo "state.json       (none; run /buddy-pair)"
fi

if [[ -f "$LOG_FILE" ]]; then
    echo "--- tail $LOG_FILE ---"
    tail -n 40 "$LOG_FILE"
else
    echo "daemon.log       (none yet)"
fi
