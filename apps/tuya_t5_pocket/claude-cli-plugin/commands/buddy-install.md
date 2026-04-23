---
description: First-time setup for tuya-pocket-buddy. Verifies Python ≥ 3.10, creates the daemon venv, installs the Python package, merges the hook block into ~/.claude/settings.json.
allowed-tools: [Bash]
---

Run the full install. Safe to re-run; every step is idempotent.

After install, pair and start the daemon with:

```
/buddy-pair
/buddy-start
```

!`py ${CLAUDE_PLUGIN_ROOT}/scripts/run.py install || python3 ${CLAUDE_PLUGIN_ROOT}/scripts/run.py install`
