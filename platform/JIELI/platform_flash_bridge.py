"""TuyaOpen flash bridge for Jieli AC7916A boards.

The AC79 SDK checkout currently contains Windows-only packaging/programming
tools. This bridge intentionally delegates the board-specific USB/serial
operation to a user-provided command instead of guessing a protocol.
"""

from __future__ import annotations

import os
import shlex
import subprocess
from pathlib import Path
from typing import Any


def platform_flash(
    *,
    using_data: dict[str, str],
    binfile: str,
    port: str,
    baud: int,
    boards_root: str,
    logger: Any,
) -> dict[str, object]:
    del boards_root

    image = Path(binfile)
    if not image.is_file() or image.stat().st_size == 0:
        return {"success": False, "message": f"firmware image not found: {image}"}

    command_text = os.environ.get("JIELI_FLASH_CMD", "").strip()
    if not command_text:
        return {
            "success": False,
            "message": (
                "JIELI_FLASH_CMD is not configured; set it to a Jieli uploader "
                "command using {binfile}, {port}, and {baud} placeholders"
            ),
        }

    values = {
        "binfile": str(image),
        "port": port,
        "baud": str(baud or 0),
        "chip": using_data.get("CONFIG_CHIP_CHOICE", "wl82"),
        "board": using_data.get("CONFIG_BOARD_CHOICE", "AC7916A"),
    }
    try:
        command = shlex.split(command_text.format(**values))
    except (KeyError, ValueError) as exc:
        return {"success": False, "message": f"invalid JIELI_FLASH_CMD: {exc}"}
    if not command:
        return {"success": False, "message": "JIELI_FLASH_CMD is empty"}

    logger.info(f"Jieli flash command: {shlex.join(command)}")
    result = subprocess.run(command, check=False)
    if result.returncode != 0:
        return {"success": False, "message": f"uploader exited with {result.returncode}"}
    return {"success": True, "message": "Jieli uploader completed"}
