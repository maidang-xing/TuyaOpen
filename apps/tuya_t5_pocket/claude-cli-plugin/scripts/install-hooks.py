"""Merge the plugin hook block into ~/.claude/settings.json.

Used by both ``install.sh`` and ``install.ps1``. All filesystem paths go
through :mod:`pathlib` to prevent ``..\\`` traversal through externally
supplied values. The original settings file is backed up to
``settings.json.buddy-backup-<epoch>`` before mutation.

Run::

    python3 install-hooks.py --merge --plugin-root <path-to-plugin>
    python3 install-hooks.py --unmerge --plugin-root <path-to-plugin>
"""

from __future__ import annotations

import argparse
import copy
import json
import os
import sys
import time
from pathlib import Path
from typing import Any


def _claude_settings_path() -> Path:
    override = os.environ.get("CLAUDE_SETTINGS_FILE")
    if override:
        return Path(override).expanduser().resolve()
    if os.name == "nt":
        home = Path(os.environ.get("USERPROFILE") or Path.home())
    else:
        home = Path.home()
    return (home / ".claude" / "settings.json").resolve()


def _venv_python(plugin_root: Path) -> str:
    """Return the path to the venv Python executable."""
    if os.name == "nt":
        appdata = os.environ.get("LOCALAPPDATA") or Path.home() / "AppData" / "Local"
        venv = Path(appdata) / "tuya-pocket-buddy" / "venv" / "Scripts" / "python.exe"
    else:
        home = Path.home()
        venv = home / ".tuya-pocket-buddy" / "venv" / "bin" / "python3"
    return str(venv)


def _hook_handler_path(plugin_root: Path) -> str:
    return str((plugin_root / "scripts" / "hook_handler.py").resolve())


def _load_plugin_doc(plugin_root: Path) -> dict[str, Any]:
    hooks_file = plugin_root / "settings" / "hooks.json"
    with hooks_file.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def _expand_placeholders(
    obj: Any, python_path: str, handler_path: str
) -> Any:
    """Recursively replace __PYTHON__ and __HOOK_HANDLER__ in string values."""
    if isinstance(obj, str):
        return obj.replace("__PYTHON__", python_path).replace(
            "__HOOK_HANDLER__", handler_path
        )
    if isinstance(obj, dict):
        return {k: _expand_placeholders(v, python_path, handler_path) for k, v in obj.items()}
    if isinstance(obj, list):
        return [_expand_placeholders(item, python_path, handler_path) for item in obj]
    return obj


def _read_settings(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    with path.open("r", encoding="utf-8") as fh:
        data = json.load(fh)
    return data if isinstance(data, dict) else {}


def _backup(path: Path) -> Path | None:
    if not path.is_file():
        return None
    ts = int(time.time())
    backup = path.with_suffix(path.suffix + f".buddy-backup-{ts}")
    backup.write_bytes(path.read_bytes())
    return backup


def _atomic_write(path: Path, obj: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    with tmp.open("w", encoding="utf-8") as fh:
        json.dump(obj, fh, indent=2, ensure_ascii=False)
        fh.write("\n")
    tmp.replace(path)


_MARKER_KEY = "_tuya_pocket_buddy_managed"
_PERMS_MARKER = "_tuya_pocket_buddy_permissions"


def _merge(
    settings: dict[str, Any],
    plugin_hooks: dict[str, Any],
    plugin_permissions: list[str],
) -> dict[str, Any]:
    merged = copy.deepcopy(settings)

    # --- hooks ---
    existing = dict(merged.get("hooks") or {})
    for event, entries in plugin_hooks.items():
        existing_entries = [
            e for e in list(existing.get(event) or [])
            if not (isinstance(e, dict) and e.get(_MARKER_KEY))
        ]
        for entry in entries:
            tagged = copy.deepcopy(entry)
            if isinstance(tagged, dict):
                tagged[_MARKER_KEY] = True
            existing_entries.append(tagged)
        existing[event] = existing_entries
    merged["hooks"] = existing

    # --- permissions.allow ---
    # Merge our allow list (tagged) into existing allow list.
    perms = dict(merged.get("permissions") or {})
    allow = list(perms.get("allow") or [])
    # Remove any we previously added (tagged entries are plain strings prefixed
    # with our marker comment — we track via a separate key instead).
    managed_perms = set(merged.get(_PERMS_MARKER) or [])
    allow = [p for p in allow if p not in managed_perms]
    # Add fresh list
    for p in plugin_permissions:
        if p not in allow:
            allow.append(p)
    perms["allow"] = allow
    merged["permissions"] = perms
    merged[_PERMS_MARKER] = list(plugin_permissions)

    return merged


def _unmerge(settings: dict[str, Any]) -> dict[str, Any]:
    cleaned = copy.deepcopy(settings)

    # --- hooks ---
    hooks = dict(cleaned.get("hooks") or {})
    for event, entries in list(hooks.items()):
        kept = [e for e in entries if not (isinstance(e, dict) and e.get(_MARKER_KEY))]
        if kept:
            hooks[event] = kept
        else:
            hooks.pop(event, None)
    cleaned["hooks"] = hooks

    # --- permissions ---
    managed_perms = set(cleaned.pop(_PERMS_MARKER, []))
    if managed_perms:
        perms = dict(cleaned.get("permissions") or {})
        allow = [p for p in list(perms.get("allow") or []) if p not in managed_perms]
        if allow:
            perms["allow"] = allow
        elif "allow" in perms:
            del perms["allow"]
        if perms:
            cleaned["permissions"] = perms
        elif "permissions" in cleaned:
            del cleaned["permissions"]

    return cleaned


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--plugin-root", required=True, type=Path)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--merge", action="store_true")
    group.add_argument("--unmerge", action="store_true")
    args = parser.parse_args()

    plugin_root = args.plugin_root.resolve()
    settings_path = _claude_settings_path()
    settings = _read_settings(settings_path)

    doc = _load_plugin_doc(plugin_root)
    plugin_permissions = list(doc.get("permissions", {}).get("allow") or [])
    raw_hooks = dict(doc.get("hooks") or {})

    # Expand path placeholders in hook commands
    python_path = _venv_python(plugin_root)
    handler_path = _hook_handler_path(plugin_root)
    plugin_hooks = _expand_placeholders(raw_hooks, python_path, handler_path)

    backup = _backup(settings_path)
    if backup is not None:
        print(f"backed up {settings_path} -> {backup}")

    if args.merge:
        updated = _merge(settings, plugin_hooks, plugin_permissions)
        print(f"PreToolUse handler: {python_path} {handler_path}")
    else:
        updated = _unmerge(settings)

    _atomic_write(settings_path, updated)
    print(f"wrote {settings_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
