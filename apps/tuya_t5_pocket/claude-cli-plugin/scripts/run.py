"""Cross-platform dispatcher for tuya-pocket-buddy slash commands.

Slash command docs invoke this module as:

    !`python3 ${CLAUDE_PLUGIN_ROOT}/scripts/run.py <action>`

All actions are implemented directly in Python — no .ps1 / .sh scripts needed.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
from pathlib import Path


# ---------------------------------------------------------------------------
# State / path helpers
# ---------------------------------------------------------------------------

def _state_dir() -> Path:
    override = os.environ.get("TUYA_POCKET_BUDDY_STATE_DIR")
    if override:
        p = Path(override)
        p.mkdir(parents=True, exist_ok=True)
        return p
    if os.name == "nt":
        root = Path(os.environ.get("LOCALAPPDATA") or Path.home() / "AppData" / "Local")
    else:
        root = Path.home()
    d = root / "tuya-pocket-buddy"
    d.mkdir(parents=True, exist_ok=True)
    return d


def _venv_python() -> Path:
    venv = _state_dir() / "venv"
    if os.name == "nt":
        return venv / "Scripts" / "python.exe"
    return venv / "bin" / "python"


def _log_file() -> Path:
    return _state_dir() / "daemon.log"


def _pid_file() -> Path:
    return _state_dir() / "daemon.pid"


def _get_daemon_pid() -> int | None:
    pid_file = _pid_file()
    if not pid_file.is_file():
        return None
    try:
        pid = int(pid_file.read_text().strip())
    except (ValueError, OSError):
        return None
    # Check whether the process is still alive.
    if os.name == "nt":
        import ctypes
        SYNCHRONIZE = 0x00100000
        handle = ctypes.windll.kernel32.OpenProcess(SYNCHRONIZE, False, pid)
        if handle:
            ctypes.windll.kernel32.CloseHandle(handle)
            return pid
        return None
    else:
        try:
            os.kill(pid, 0)
            return pid
        except OSError:
            return None


# ---------------------------------------------------------------------------
# Python interpreter discovery
# ---------------------------------------------------------------------------

def _find_python() -> tuple[str, list[str]]:
    """Return (executable_path, extra_args) for Python >= 3.10.

    Skips Windows App Store stubs (WindowsApps) — they hang in
    non-interactive sessions waiting for the Store to open.
    """
    import shutil
    candidates: list[tuple[str, list[str]]] = [
        ("py",         ["-3"]),
        ("python",     []),
        ("python3",    []),
        ("python3.13", []),
        ("python3.12", []),
        ("python3.11", []),
        ("python3.10", []),
    ]
    for name, extra in candidates:
        path = shutil.which(name)
        if not path:
            continue
        if "WindowsApps" in path:
            continue
        try:
            r = subprocess.run(
                [path] + extra + [
                    "-c",
                    "import sys; v=sys.version_info; print(f'{v[0]}.{v[1]}')",
                ],
                capture_output=True, text=True, timeout=5,
            )
            if r.returncode != 0:
                continue
            major, minor = r.stdout.strip().split(".")
            if int(major) > 3 or (int(major) == 3 and int(minor) >= 10):
                return path, extra
        except Exception:
            continue
    raise SystemExit(
        "tuya-pocket-buddy: Python >= 3.10 not found on PATH "
        "(WindowsApps stubs are skipped — install Python from python.org)."
    )


# ---------------------------------------------------------------------------
# Actions
# ---------------------------------------------------------------------------

def _action_install(plugin_root: Path) -> int:
    print("tuya-pocket-buddy: looking for Python >= 3.10 ...")
    py_exe, py_extra = _find_python()
    print(f"tuya-pocket-buddy: using {py_exe}")

    venv_py = _venv_python()
    venv_dir = venv_py.parent.parent

    if not venv_py.is_file():
        print(f"tuya-pocket-buddy: creating venv at {venv_dir}")
        r = subprocess.run([py_exe] + py_extra + ["-m", "venv", str(venv_dir)])
        if r.returncode != 0:
            print("tuya-pocket-buddy: venv creation failed", file=sys.stderr)
            return r.returncode

    print("tuya-pocket-buddy: upgrading pip ...")
    subprocess.run(
        [str(venv_py), "-m", "pip", "install", "--upgrade", "--quiet", "pip"]
    )

    daemon_dir = plugin_root / "daemon"
    print(f"tuya-pocket-buddy: installing daemon package from {daemon_dir} ...")
    r = subprocess.run(
        [str(venv_py), "-m", "pip", "install", "--quiet", str(daemon_dir)]
    )
    if r.returncode != 0:
        print("tuya-pocket-buddy: pip install failed", file=sys.stderr)
        return r.returncode

    print("tuya-pocket-buddy: merging hooks into ~/.claude/settings.json ...")
    hooks_script = plugin_root / "scripts" / "install-hooks.py"
    r = subprocess.run(
        [py_exe] + py_extra
        + [str(hooks_script), "--merge", "--plugin-root", str(plugin_root)]
    )
    if r.returncode != 0:
        print("tuya-pocket-buddy: hook merge failed", file=sys.stderr)
        return r.returncode

    print("tuya-pocket-buddy: install complete.")
    print("  Next steps:  /buddy-pair  →  /buddy-start")
    return 0


def _action_start() -> int:
    venv_py = _venv_python()
    if not venv_py.is_file():
        print("tuya-pocket-buddy: venv not found — run /buddy-install first.",
              file=sys.stderr)
        return 1

    existing = _get_daemon_pid()
    if existing is not None:
        print(f"tuya-pocket-buddy: already running (pid {existing})")
        return 0

    log = _log_file()
    log_fh = open(log, "a")

    if os.name == "nt":
        CREATE_NO_WINDOW = 0x08000000
        DETACHED_PROCESS = 0x00000008
        proc = subprocess.Popen(
            [str(venv_py), "-m", "tuya_pocket_buddy", "run"],
            stdout=log_fh, stderr=log_fh,
            creationflags=CREATE_NO_WINDOW | DETACHED_PROCESS,
            close_fds=True,
        )
    else:
        proc = subprocess.Popen(
            [str(venv_py), "-m", "tuya_pocket_buddy", "run"],
            stdout=log_fh, stderr=log_fh,
            start_new_session=True,
        )

    _pid_file().write_text(str(proc.pid))
    print(f"tuya-pocket-buddy: started (pid {proc.pid})")
    return 0


def _action_stop() -> int:
    pid_value = _get_daemon_pid()
    pid_file = _pid_file()

    if pid_value is None:
        print("tuya-pocket-buddy: not running")
        if pid_file.is_file():
            pid_file.unlink()
        return 0

    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/F", "/PID", str(pid_value)],
            capture_output=True,
        )
    else:
        import signal
        try:
            os.kill(pid_value, signal.SIGTERM)
        except ProcessLookupError:
            pass

    for _ in range(10):
        time.sleep(0.3)
        if _get_daemon_pid() is None:
            break

    if pid_file.is_file():
        pid_file.unlink()
    print(f"tuya-pocket-buddy: stopped (pid {pid_value})")
    return 0


def _action_status() -> int:
    state_dir = _state_dir()
    log = _log_file()
    state_json = state_dir / "state.json"

    daemon_pid = _get_daemon_pid()
    print("daemon:    " + (f"running (pid {daemon_pid})" if daemon_pid else "not running"))

    if state_json.is_file():
        try:
            data = json.loads(state_json.read_text())
            addr = data.get("device_address", "(none)")
            owner = data.get("owner_name", "")
        except Exception:
            addr, owner = "(parse error)", ""
        print(f"device:    {addr}" + (f"  owner={owner}" if owner else ""))
    else:
        print("device:    (none — run /buddy-pair)")

    if log.is_file():
        lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
        print(f"\n--- last 40 lines of {log} ---")
        for line in lines[-40:]:
            print(line)
    else:
        print("log:       (none yet)")

    return 0


def _action_install_hooks(plugin_root: Path) -> int:
    py_exe, py_extra = _find_python()
    hooks_script = plugin_root / "scripts" / "install-hooks.py"
    r = subprocess.run(
        [py_exe] + py_extra
        + [str(hooks_script), "--merge", "--plugin-root", str(plugin_root)]
    )
    return r.returncode


def _action_pair() -> int:
    venv_py = _venv_python()
    if not venv_py.is_file():
        print("tuya-pocket-buddy: venv not found — run /buddy-install first.",
              file=sys.stderr)
        return 1
    r = subprocess.run([str(venv_py), "-m", "tuya_pocket_buddy", "pair"])
    return r.returncode


def _action_unpair() -> int:
    venv_py = _venv_python()
    if not venv_py.is_file():
        print("tuya-pocket-buddy: venv not found — run /buddy-install first.",
              file=sys.stderr)
        return 1
    r = subprocess.run([str(venv_py), "-m", "tuya_pocket_buddy", "unpair"])
    return r.returncode


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

_ACTIONS = frozenset({"install", "install-hooks", "start", "stop", "status", "pair", "unpair"})


def main(argv: list[str] | None = None) -> int:
    args = argv if argv is not None else sys.argv[1:]
    if not args or args[0] not in _ACTIONS:
        valid = ", ".join(sorted(_ACTIONS))
        print(f"run.py: usage: run.py <action>  (one of: {valid})", file=sys.stderr)
        return 2

    plugin_root = Path(__file__).resolve().parent.parent

    action = args[0]
    if action == "install":
        return _action_install(plugin_root)
    if action == "install-hooks":
        return _action_install_hooks(plugin_root)
    if action == "start":
        return _action_start()
    if action == "stop":
        return _action_stop()
    if action == "status":
        return _action_status()
    if action == "pair":
        return _action_pair()
    if action == "unpair":
        return _action_unpair()
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
