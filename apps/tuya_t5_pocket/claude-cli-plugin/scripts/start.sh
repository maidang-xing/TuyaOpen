#!/usr/bin/env bash
# NOTE: Untested in v1; Windows is the primary target.
#
# Start the tuya-pocket-buddy daemon detached. Idempotent.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

ensure_state_dir

if pid="$(daemon_is_running)"; then
    echo "tuya-pocket-buddy: already running (pid $pid)"
    exit 0
fi

if [[ ! -x "$VENV_DIR/bin/python" ]]; then
    echo "tuya-pocket-buddy: venv missing. Run /buddy-install first." >&2
    exit 1
fi

nohup "$VENV_DIR/bin/python" -m tuya_pocket_buddy run \
    >>"$LOG_FILE" 2>&1 &
echo $! > "$PID_FILE"
echo "tuya-pocket-buddy: started (pid $(cat "$PID_FILE"))"
