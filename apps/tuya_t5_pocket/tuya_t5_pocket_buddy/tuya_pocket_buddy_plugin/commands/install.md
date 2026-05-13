---
name: install
description: Install the Tuya Pocket Buddy daemon — npm install and build
---

You MUST execute every step below using the Bash tool. Do NOT just show instructions — run the commands.

## Step 1: Find the plugin directory

The plugin lives at `tuya_pocket_buddy_plugin/` relative to the project that contains this command. Find it:

```bash
PLUGIN_DIR="$(find . -maxdepth 5 -type f -name 'package.json' -path '*/tuya_pocket_buddy_plugin/*' -exec dirname {} \; 2>/dev/null | head -1)"
```

If `PLUGIN_DIR` is empty, tell the user the plugin directory was not found and stop.

## Step 2: Install dependencies and build

```bash
cd "$PLUGIN_DIR" && npm install && npm run build
```

If either command fails, report the error and stop.

## Step 3: Report

Print a summary:
- Plugin directory: `$PLUGIN_DIR`
- npm install: OK
- Build: OK
- Hooks: automatically configured by the plugin system (no manual setup needed)
- Tell the user to run `/tuya-pocket-buddy:start` to start the daemon
