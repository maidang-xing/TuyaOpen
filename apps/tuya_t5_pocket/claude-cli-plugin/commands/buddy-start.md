---
description: Start the tuya-pocket-buddy daemon (background). Binds 127.0.0.1:9878 and connects to the paired T5AI-Pocket over BLE.
---

Starts the daemon detached. PID file lives at
`%LOCALAPPDATA%\tuya-pocket-buddy\daemon.pid` (Windows) or
`~/.tuya-pocket-buddy/daemon.pid` (POSIX); rolling log is `daemon.log`
in the same directory.

Idempotent: re-running while the daemon is already up is a no-op.

!`python3 "$CLAUDE_PLUGIN_ROOT/scripts/run.py" start`
