#!/usr/bin/env bash
# NOTE: Untested in v1; Windows is the primary target.
#
# Install the tuya-pocket-buddy daemon: create a venv, install the
# package, merge the hook block into ~/.claude/settings.json.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

PY="$(python_cmd)"

ensure_state_dir

if [[ ! -d "$VENV_DIR" ]]; then
    echo "tuya-pocket-buddy: creating venv at $VENV_DIR"
    "$PY" -m venv "$VENV_DIR"
fi

"$VENV_DIR/bin/pip" install --upgrade --quiet pip
"$VENV_DIR/bin/pip" install --quiet "$PLUGIN_ROOT/daemon"

echo "tuya-pocket-buddy: merging hooks into ~/.claude/settings.json"
"$PY" "$SCRIPT_DIR/install-hooks.py" --merge \
    --plugin-root "$PLUGIN_ROOT"

echo "tuya-pocket-buddy: install complete. Next: /buddy-pair then /buddy-start"
