---
name: status
description: Check the Tuya Pocket Buddy daemon status
---

You MUST execute every step below using the Bash tool. Do NOT just show instructions — run the commands.

## Step 1: Check daemon process

```bash
curl -s -X POST http://127.0.0.1:9878/hook -d '{}' -H 'Content-Type: application/json' --max-time 2 2>/dev/null && echo "DAEMON_UP" || echo "DAEMON_DOWN"
```

## Step 2: Report

Print a status summary table:

| Item | Status |
|------|--------|
| Daemon | Running / Stopped |
| Hook server | http://127.0.0.1:9878 |
| WebSocket server | ws://0.0.0.0:7681/buddy |
| Hooks | Managed by plugin system (auto-loaded) |

If daemon is stopped, suggest `/tuya-pocket-buddy:start`.
