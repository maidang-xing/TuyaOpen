"""Re-exports of the mutable session state type.

The heavyweight state machine lives in :mod:`tuya_pocket_buddy.hook_router`
so the router has tight control over mutations. This module exists to
match the spec §3.1 layout and provide a public import surface.
"""

from __future__ import annotations

from .hook_router import State

__all__ = ["State"]
