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


def _load_plugin_hooks(plugin_root: Path) -> dict[str, Any]:
    hooks_file = plugin_root / "settings" / "hooks.json"
    with hooks_file.open("r", encoding="utf-8") as fh:
        doc = json.load(fh)
    return dict(doc.get("hooks") or {})


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


def _merge_hooks(
    settings: dict[str, Any], new_hooks: dict[str, Any]
) -> dict[str, Any]:
    merged = copy.deepcopy(settings)
    existing = dict(merged.get("hooks") or {})
    for event, entries in new_hooks.items():
        existing_entries = list(existing.get(event) or [])
        # Drop any previous buddy-managed entry so re-install is idempotent.
        existing_entries = [
            e for e in existing_entries
            if not (isinstance(e, dict) and e.get(_MARKER_KEY))
        ]
        for entry in entries:
            tagged = copy.deepcopy(entry)
            if isinstance(tagged, dict):
                tagged[_MARKER_KEY] = True
            existing_entries.append(tagged)
        existing[event] = existing_entries
    merged["hooks"] = existing
    return merged


def _unmerge_hooks(settings: dict[str, Any]) -> dict[str, Any]:
    cleaned = copy.deepcopy(settings)
    hooks = dict(cleaned.get("hooks") or {})
    for event, entries in list(hooks.items()):
        kept = [
            e for e in entries
            if not (isinstance(e, dict) and e.get(_MARKER_KEY))
        ]
        if kept:
            hooks[event] = kept
        else:
            hooks.pop(event, None)
    cleaned["hooks"] = hooks
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
    plugin_hooks = _load_plugin_hooks(plugin_root)
    settings = _read_settings(settings_path)

    backup = _backup(settings_path)
    if backup is not None:
        print(f"backed up {settings_path} -> {backup}")

    if args.merge:
        updated = _merge_hooks(settings, plugin_hooks)
    else:
        updated = _unmerge_hooks(settings)

    _atomic_write(settings_path, updated)
    print(f"wrote {settings_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
