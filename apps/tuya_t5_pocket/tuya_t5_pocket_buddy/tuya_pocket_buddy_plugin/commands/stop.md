---
name: stop
description: Stop the running Tuya Pocket Buddy daemon (Windows / Linux / macOS)
---

You MUST execute every step below using the Bash tool. Do NOT just show instructions — run the commands.

## Step 1: Check if running

```bash
curl -s -X POST http://127.0.0.1:9878/hook -d '{}' -H 'Content-Type: application/json' --max-time 2 2>/dev/null && echo "RUNNING" || echo "NOT_RUNNING"
```

If `NOT_RUNNING`, tell the user the daemon is not running and stop.

## Step 2: Kill the daemon (cross-platform)

```bash
OS=$(uname -s 2>/dev/null || echo Windows)
if echo "$OS" | grep -qiE 'MINGW|CYGWIN|MSYS|Windows'; then
  # Windows: find PID listening on 9878, kill it
  PIDS=$(netstat -ano 2>/dev/null | awk '/TCP.*127\.0\.0\.1:9878.*LISTENING/{print $NF}' | sort -u)
  if [ -n "$PIDS" ]; then
    for PID in $PIDS; do
      taskkill //F //PID "$PID" 2>/dev/null && echo "Killed PID $PID" || echo "Failed to kill PID $PID"
    done
  else
    echo "No process found listening on port 9878"
  fi
else
  # Linux / macOS
  pkill -f "node.*dist/index.js" 2>/dev/null || pkill -f "node.*index.js.*run" 2>/dev/null || echo "pkill found nothing"
fi
```

## Step 3: Verify stopped

```bash
sleep 1
curl -s -X POST http://127.0.0.1:9878/hook -d '{}' -H 'Content-Type: application/json' --max-time 2 2>/dev/null && echo "STILL_RUNNING" || echo "STOPPED"
```

- If `STOPPED`: report success
- If `STILL_RUNNING`: warn the user the process may still be running and suggest repeating Step 2 or killing it manually
