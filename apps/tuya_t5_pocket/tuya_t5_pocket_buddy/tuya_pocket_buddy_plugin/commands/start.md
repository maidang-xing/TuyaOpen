---
name: start
description: Start the Tuya Pocket Buddy daemon process in the background
---

You MUST execute every step below using the Bash tool. Do NOT just show instructions — run the commands.

## Step 1: Check if already running

```bash
curl -s -X POST http://127.0.0.1:9878/hook -d '{}' -H 'Content-Type: application/json' --max-time 2 2>/dev/null
```

If curl succeeds (exit code 0), the daemon is already running. Tell the user and stop.

## Step 2: Find the plugin directory

```bash
PLUGIN_DIR="$(find . -maxdepth 5 -type f -name 'package.json' -path '*/tuya_pocket_buddy_plugin/*' -exec dirname {} \; 2>/dev/null | head -1)"
```

If empty, tell the user to run `/tuya-pocket-buddy:install` first.

Check that `$PLUGIN_DIR/dist/index.js` exists. If not, tell the user to run `/tuya-pocket-buddy:install` first.

## Step 3: Start the daemon

Run the daemon in the background using the Bash tool with `run_in_background`:

```bash
cd "$PLUGIN_DIR" && node dist/index.js run
```

## Step 4: Verify

Wait 2 seconds, then check:

```bash
sleep 2 && curl -s -X POST http://127.0.0.1:9878/hook -d '{}' -H 'Content-Type: application/json' --max-time 2
```

Report the result:
- Daemon started successfully
- Hook server: http://127.0.0.1:9878
- WebSocket server: ws://0.0.0.0:7681/buddy
- Tell the user their T5AI Pocket device can now connect
