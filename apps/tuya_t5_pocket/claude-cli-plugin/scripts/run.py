"""Cross-platform dispatcher for slash-command scripts.

Slash command docs invoke this module as:

    !`python3 "$CLAUDE_PLUGIN_ROOT/scripts/run.py" <action> [args...]`

On Windows the dispatcher shells out to ``pwsh`` against the matching
``.ps1`` script. On POSIX it runs ``bash`` against the matching ``.sh``
script. No shell interpolation is performed; :func:`subprocess.run` is
invoked with a list argv and ``shell=False`` (see the workspace
security rules).
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

# Whitelist of dispatchable actions. Anything else is rejected.
_ACTIONS = frozenset({
    "install",
    "install-hooks",
    "start",
    "stop",
    "status",
})


def _scripts_dir() -> Path:
    return Path(__file__).resolve().parent


def _dispatch(action: str, extra: list[str]) -> int:
    if action not in _ACTIONS:
        print(f"run.py: unknown action {action!r}", file=sys.stderr)
        return 2

    scripts = _scripts_dir()
    if os.name == "nt":
        script = scripts / f"{action}.ps1"
        pwsh = shutil.which("pwsh") or shutil.which("powershell")
        if pwsh is None:
            print("run.py: neither `pwsh` nor `powershell` is on PATH",
                  file=sys.stderr)
            return 3
        argv = [pwsh, "-NoProfile", "-File", str(script), *extra]
    else:
        script = scripts / f"{action}.sh"
        bash = shutil.which("bash")
        if bash is None:
            print("run.py: `bash` is not on PATH", file=sys.stderr)
            return 3
        argv = [bash, str(script), *extra]

    if not script.is_file():
        print(f"run.py: script not found: {script}", file=sys.stderr)
        return 4
    return subprocess.run(argv, check=False).returncode


def main(argv: list[str] | None = None) -> int:
    args = argv if argv is not None else sys.argv[1:]
    if not args:
        print("run.py: missing action", file=sys.stderr)
        return 2
    return _dispatch(args[0], args[1:])


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
