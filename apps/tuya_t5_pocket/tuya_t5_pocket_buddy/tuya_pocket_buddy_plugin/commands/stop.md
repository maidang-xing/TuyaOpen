---
name: stop
description: Stop the running Tuya Pocket Buddy daemon
---

You MUST execute every step below using the Bash tool. Do NOT just show instructions — run the commands.

## Step 1: Check if daemon is running

```bash
curl -s -X POST http://127.0.0.1:9878/hook -d '{}' -H 'Content-Type: application/json' --max-time 2 2>/dev/null
```

If curl fails, the daemon is not running. Tell the user and stop.

## Step 2: Kill the daemon

```bash
pkill -f "node dist/index.js run" 2>/dev/null || true
```

## Step 3: Verify

```bash
sleep 1 && curl -s -X POST http://127.0.0.1:9878/hook -d '{}' -H 'Content-Type: application/json' --max-time 2 2>/dev/null && echo "STILL_RUNNING" || echo "STOPPED"
```

If output contains "STOPPED", report success. Otherwise warn the user that the process may still be running and suggest `kill -9`.
