---
description: First-time setup for tuya-pocket-buddy. Verifies Python ≥ 3.10, creates the daemon venv, installs the Python package, merges the hook block into ~/.claude/settings.json.
---

Run the full install. Safe to re-run; every step is idempotent.

The Windows path uses PowerShell (`scripts/install.ps1`); POSIX falls
back to bash (`scripts/install.sh`, untested in v1). Neither step
flashes firmware — use `tos.py` in the firmware project for that.

After install, pair and start the daemon with:

```
/buddy-pair
/buddy-start
```

!`python3 "$CLAUDE_PLUGIN_ROOT/scripts/run.py" install`
