---
description: Show tuya-pocket-buddy daemon + device status at a glance.
allowed-tools: [Bash]
---

Prints whether the daemon is running, the currently paired device MAC,
and the last few lines of `daemon.log`.

!`py ${CLAUDE_PLUGIN_ROOT}/scripts/run.py status || python3 ${CLAUDE_PLUGIN_ROOT}/scripts/run.py status`
