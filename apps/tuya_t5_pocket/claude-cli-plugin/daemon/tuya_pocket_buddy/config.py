"""Daemon configuration: state directory, log paths, persisted device MAC.

Respects Windows / POSIX conventions per spec §3.6 ("Security"). All path
joins go through :mod:`pathlib` to prevent ``..\\`` traversal through
misconfigured values.

State layout::

    Windows: %LOCALAPPDATA%\\tuya-pocket-buddy\\
    POSIX:   ~/.tuya-pocket-buddy/
        ├── daemon.log          # rotated daily, 7-day retention
        ├── daemon.pid
        └── state.json          # {"device_address": "...", "owner_name": "..."}
"""

from __future__ import annotations

import json
import logging
import logging.handlers
import os
import platform
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

log = logging.getLogger(__name__)

_LOG_RETENTION_DAYS = 7
_LOG_FILENAME = "daemon.log"
_PID_FILENAME = "daemon.pid"
_STATE_FILENAME = "state.json"


def state_dir() -> Path:
    """Return the per-user state directory, creating it if necessary."""
    if platform.system() == "Windows":
        root = os.environ.get("LOCALAPPDATA")
        if not root:
            root = str(Path.home() / "AppData" / "Local")
        path = Path(root) / "tuya-pocket-buddy"
    else:
        path = Path.home() / ".tuya-pocket-buddy"
    path.mkdir(parents=True, exist_ok=True)
    return path


def log_path() -> Path:
    """Return ``<state_dir>/daemon.log``."""
    return state_dir() / _LOG_FILENAME


def pid_path() -> Path:
    """Return ``<state_dir>/daemon.pid``."""
    return state_dir() / _PID_FILENAME


def state_path() -> Path:
    """Return ``<state_dir>/state.json``."""
    return state_dir() / _STATE_FILENAME


@dataclass
class DaemonConfig:
    """Resolved runtime config."""

    state_dir: Path
    log_path: Path
    pid_path: Path
    state_path: Path
    port: int
    owner_name: str
    device_address: str | None


def default_owner_name() -> str:
    """Best-effort friendly owner name for the device UI."""
    env = os.environ.get("TUYA_POCKET_BUDDY_OWNER")
    if env:
        return env[:30]
    user = os.environ.get("USER") or os.environ.get("USERNAME") or "friend"
    return user[:30]


def load_state(path: Path) -> dict[str, Any]:
    """Load the persisted state (device MAC, owner name). Returns ``{}`` if absent."""
    if not path.is_file():
        return {}
    try:
        with path.open("r", encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, json.JSONDecodeError):
        log.warning("config: unreadable state file at %s", path)
        return {}
    return data if isinstance(data, dict) else {}


def save_state(path: Path, data: dict[str, Any]) -> None:
    """Persist *data* atomically to *path*."""
    tmp = path.with_suffix(".tmp")
    with tmp.open("w", encoding="utf-8") as fh:
        json.dump(data, fh, ensure_ascii=False, separators=(",", ":"))
    tmp.replace(path)


def load_config(port: int = 9878) -> DaemonConfig:
    """Resolve the full :class:`DaemonConfig` from disk and environment."""
    sdir = state_dir()
    sp = state_path()
    persisted = load_state(sp)
    return DaemonConfig(
        state_dir=sdir,
        log_path=log_path(),
        pid_path=pid_path(),
        state_path=sp,
        port=int(os.environ.get("TUYA_POCKET_BUDDY_PORT", port)),
        owner_name=str(persisted.get("owner_name") or default_owner_name()),
        device_address=persisted.get("device_address"),
    )


def setup_logging(path: Path, level: int = logging.INFO) -> None:
    """Configure the daemon's root logger with a daily-rotating file handler."""
    path.parent.mkdir(parents=True, exist_ok=True)
    handler = logging.handlers.TimedRotatingFileHandler(
        filename=str(path),
        when="midnight",
        interval=1,
        backupCount=_LOG_RETENTION_DAYS,
        encoding="utf-8",
        utc=False,
    )
    handler.setFormatter(logging.Formatter(
        fmt="%(asctime)s %(levelname)s %(name)s %(message)s"
    ))
    root = logging.getLogger()
    root.setLevel(level)
    # Avoid duplicate handlers when called repeatedly in tests.
    for existing in list(root.handlers):
        if isinstance(existing, logging.handlers.TimedRotatingFileHandler):
            root.removeHandler(existing)
    root.addHandler(handler)


def config_as_dict(cfg: DaemonConfig) -> dict[str, Any]:
    """Return *cfg* as a plain dict (paths stringified) for logging."""
    data = asdict(cfg)
    for key in ("state_dir", "log_path", "pid_path", "state_path"):
        data[key] = str(data[key])
    return data
