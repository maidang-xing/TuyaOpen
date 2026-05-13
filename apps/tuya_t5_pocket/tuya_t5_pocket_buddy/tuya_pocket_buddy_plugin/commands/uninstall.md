---
name: uninstall
description: Stop the Tuya Pocket Buddy daemon
---

You MUST execute every step below using the Bash tool. Do NOT just show instructions — run the commands.

## Step 1: Stop the daemon if running

```bash
pkill -f "node dist/index.js run" 2>/dev/null || true
```

## Step 2: Report

Print a summary:
- Daemon stopped: yes/no
- Hooks: automatically removed when the plugin is unloaded (no manual cleanup needed)
- Note: to fully remove the plugin, stop loading it with `--plugin-dir` or run `claude plugins uninstall tuya-pocket-buddy`
