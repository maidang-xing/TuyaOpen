---
name: uninstall
description: Stop the Tuya Pocket Buddy daemon and remove hooks from Claude Code settings
---

You MUST execute every step below using the Bash tool. Do NOT just show instructions — run the commands.

## Step 1: Stop the daemon (cross-platform)

```bash
OS=$(uname -s 2>/dev/null || echo Windows)
if echo "$OS" | grep -qiE 'MINGW|CYGWIN|MSYS|Windows'; then
  PIDS=$(netstat -ano 2>/dev/null | awk '/TCP.*127\.0\.0\.1:9878.*LISTENING/{print $NF}' | sort -u)
  for PID in $PIDS; do
    taskkill //F //PID "$PID" 2>/dev/null && echo "Killed PID $PID"
  done
else
  pkill -f "node.*dist/index.js" 2>/dev/null || pkill -f "node.*index.js.*run" 2>/dev/null || true
fi
curl -s -X POST http://127.0.0.1:9878/hook -d '{}' -H 'Content-Type: application/json' --max-time 2 2>/dev/null && echo "STILL_RUNNING" || echo "DAEMON_STOPPED"
```

## Step 2: Find the plugin directory and remove hooks

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

if [ -n "$PLUGIN_DIR" ] && [ -f "$PLUGIN_DIR/scripts/remove-hooks.js" ]; then
  node "$PLUGIN_DIR/scripts/remove-hooks.js"
else
  echo "Plugin directory not found — hooks not removed automatically."
  echo "To remove manually: edit ~/.claude/settings.json and delete entries containing ':9878'"
fi
```

## Step 3: Report

Print summary:
- Daemon: stopped / was not running
- Hooks: removed from `~/.claude/settings.json` / not found
- **Next step:** restart Claude Code (or open the `/hooks` menu) to unload the hooks
- To reinstall later: run `/tuya-pocket-buddy:install`
