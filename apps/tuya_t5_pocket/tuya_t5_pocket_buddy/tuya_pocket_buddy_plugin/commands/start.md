---
name: start
description: Start the Tuya Pocket Buddy daemon (Windows / Linux / macOS)
---

You MUST execute every step below using the Bash tool. Do NOT just show instructions — run the commands.

## Step 1: Check if already running

```bash
curl -s -X POST http://127.0.0.1:9878/hook -d '{}' -H 'Content-Type: application/json' --max-time 2 2>/dev/null && echo "ALREADY_RUNNING" || echo "NOT_RUNNING"
```

If `ALREADY_RUNNING`, tell the user the daemon is already running and stop.

## Step 2: Find the plugin directory and verify build

```bash
PLUGIN_DIR=""
[ -n "$CLAUDE_PLUGIN_ROOT" ] && PLUGIN_DIR="$CLAUDE_PLUGIN_ROOT"
if [ -z "$PLUGIN_DIR" ]; then
  PLUGIN_DIR="$(find . -maxdepth 7 -name 'package.json' -path '*/tuya_pocket_buddy_plugin/*' -exec dirname {} \; 2>/dev/null | head -1)"
fi
if [ -z "$PLUGIN_DIR" ]; then
  for c in \
    "apps/tuya_t5_pocket/tuya_t5_pocket_buddy/tuya_pocket_buddy_plugin" \
    "tuya_pocket_buddy_plugin" \
    "../tuya_pocket_buddy_plugin"; do
    [ -f "$c/package.json" ] && PLUGIN_DIR="$c" && break
  done
fi
[ -f "$PLUGIN_DIR/dist/index.js" ] && echo "DIST_OK=$PLUGIN_DIR" || echo "DIST_MISSING"
```

If `DIST_MISSING`, tell the user to run `/tuya-pocket-buddy:install` first and stop.

## Step 3: Check firewall (Windows only)

```bash
OS=$(uname -s 2>/dev/null || echo Windows)
if echo "$OS" | grep -qiE 'MINGW|CYGWIN|MSYS|Windows'; then
  powershell.exe -NoProfile -Command "if(Get-NetFirewallRule -DisplayName 'Tuya Pocket Buddy WS' -ErrorAction SilentlyContinue){Write-Output 'FIREWALL_OK'}else{Write-Output 'FIREWALL_MISSING'}" 2>/dev/null || echo "FIREWALL_CHECK_FAILED"
fi
```

If `FIREWALL_MISSING`, warn the user: "Windows Firewall has no rule for port 7681 — devices will NOT be able to connect. Run `/tuya-pocket-buddy:install` to add it (requires Administrator), or add it manually."

## Step 4: Start the daemon in the background

Use the Bash tool with `run_in_background: true`:

```bash
cd "$PLUGIN_DIR" && node dist/index.js run
```

## Step 5: Verify startup

```bash
sleep 2 && curl -s -X POST http://127.0.0.1:9878/hook -d '{}' -H 'Content-Type: application/json' --max-time 3 2>/dev/null && echo "DAEMON_UP" || echo "DAEMON_FAILED"
```

Report result:
- If `DAEMON_UP`: daemon started successfully
  - Hook server: http://127.0.0.1:9878
  - WebSocket server: ws://0.0.0.0:7681/buddy
  - Include firewall warning if FIREWALL_MISSING
  - T5AI Pocket devices can now connect
- If `DAEMON_FAILED`: report the failure and suggest checking Node.js is installed (`node --version`)
