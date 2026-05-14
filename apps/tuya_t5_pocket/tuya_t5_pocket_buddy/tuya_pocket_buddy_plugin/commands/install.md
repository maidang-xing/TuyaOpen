---
name: install
description: Install Tuya Pocket Buddy — build from source and register hooks in Claude Code settings (Windows / Linux / macOS)
---

You MUST execute every step below using the Bash tool. Do NOT just show instructions — run the commands.

## Step 1: Find the plugin directory

Try three strategies in order, stop at the first that succeeds:

```bash
PLUGIN_DIR=""

# Strategy 1: CLAUDE_PLUGIN_ROOT env var (set by --plugin-dir in some contexts)
[ -n "$CLAUDE_PLUGIN_ROOT" ] && PLUGIN_DIR="$CLAUDE_PLUGIN_ROOT"

# Strategy 2: search from current working directory
if [ -z "$PLUGIN_DIR" ]; then
  PLUGIN_DIR="$(find . -maxdepth 7 -name 'package.json' -path '*/tuya_pocket_buddy_plugin/*' -exec dirname {} \; 2>/dev/null | head -1)"
fi

# Strategy 3: known relative paths from common repo roots
if [ -z "$PLUGIN_DIR" ]; then
  for c in \
    "apps/tuya_t5_pocket/tuya_t5_pocket_buddy/tuya_pocket_buddy_plugin" \
    "tuya_pocket_buddy_plugin" \
    "../tuya_pocket_buddy_plugin"; do
    [ -f "$c/package.json" ] && PLUGIN_DIR="$c" && break
  done
fi

echo "PLUGIN_DIR=$PLUGIN_DIR"
```

If `PLUGIN_DIR` is still empty, ask the user for the absolute path to the plugin directory, assign it, and continue.

## Step 2: Install dependencies and build

```bash
cd "$PLUGIN_DIR" && npm install && npm run build && echo "BUILD_OK"
```

If it fails, report the error and stop.

## Step 3: Register hooks in Claude Code settings

```bash
node "$PLUGIN_DIR/scripts/setup-hooks.js"
```

This writes all required hooks into `~/.claude/settings.json` (idempotent — safe to run multiple times).
If it fails, report the error and stop.

**Important:** If you also load this plugin via `--plugin-dir`, do NOT use both simultaneously — the plugin's `hooks/hooks.json` would register a duplicate set of hooks that conflict. Choose one: either `--plugin-dir` (hooks auto-loaded, no install needed) or global hooks registered by this command (no `--plugin-dir` needed).

## Step 4: Configure firewall for WebSocket port 7681 (Windows only)

```bash
OS=$(uname -s 2>/dev/null || echo Windows)
if echo "$OS" | grep -qiE 'MINGW|CYGWIN|MSYS|Windows'; then
  powershell.exe -NoProfile -Command "\$r=Get-NetFirewallRule -DisplayName 'Tuya Pocket Buddy WS' -ErrorAction SilentlyContinue; if(\$r){Write-Output 'FIREWALL_EXISTS'}else{New-NetFirewallRule -DisplayName 'Tuya Pocket Buddy WS' -Direction Inbound -Protocol TCP -LocalPort 7681 -Action Allow|Out-Null; if(\$?){Write-Output 'FIREWALL_ADDED'}else{Write-Output 'FIREWALL_FAILED'}}" 2>/dev/null || echo "FIREWALL_SKIPPED"
else
  echo "NOT_WINDOWS"
fi
```

- `FIREWALL_EXISTS` or `FIREWALL_ADDED`: OK
- `FIREWALL_FAILED`: warn the user to manually allow port 7681 inbound in Windows Firewall (requires Administrator terminal)
- `NOT_WINDOWS`: no action needed; remind user to check `ufw`/`iptables` if device cannot connect

## Step 5: Report summary

Print:
- Plugin directory: `$PLUGIN_DIR`
- npm install + build: OK
- Hooks: registered in `~/.claude/settings.json`
- Firewall: added / exists / needs manual setup / not Windows
- **Next step:** restart Claude Code (or open the `/hooks` menu) to load the new hooks, then run `/tuya-pocket-buddy:start`
