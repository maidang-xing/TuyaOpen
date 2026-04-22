#!/usr/bin/env bash
# NOTE: Untested in v1; Windows is the primary target.
#
# Merge the plugin's hook block into ~/.claude/settings.json. Backs up
# the original to `settings.json.buddy-backup-<ts>` first.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

PY="$(python_cmd)"
exec "$PY" "$SCRIPT_DIR/install-hooks.py" --merge \
    --plugin-root "$PLUGIN_ROOT" "$@"
