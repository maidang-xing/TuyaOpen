#!/usr/bin/env python3
"""port_buddies.py — 将 claude-desktop-buddy/src/buddies/<name>.cpp 机械化迁移
为 TuyaOpen T5AI-Pocket 的 persona_<name>.c。

转换内容：
  - 移除 M5StickC/TFT_eSPI 依赖，替换为 #include "ascii_persona.h"
  - namespace <name> { ... } → 注释标记
  - do<State>(...)      → static void __<name>_<state>(...)
  - buddyPrintSprite    → ascii_print_sprite  （5/4 参数，缺 x_off 时补 0）
  - buddyPrintLine      → ascii_print_line    （4/3 参数，缺 x_off 时补 0）
  - buddySetCursor      → ascii_set_cursor
  - buddySetColor       → ascii_set_color
  - buddyPrint          → ascii_print
  - extern const Species <NAME>_SPECIES = { ... }; →
    const ascii_persona_t PERSONA_<NAME> = { "<name>", { 7 fns } };

  注意：bodyColor 字段（RGB565）被丢弃（单色 OLED）。

用法（在 tuya_t5_pocket_ai 工作目录下执行）：
  python tools/port_buddies.py \
      ../claude-desktop-buddy/src/buddies \
      src/display/ui/buddy_ui

每执行一次会覆盖 src/display/ui/buddy_ui/persona_*.c。修改顶部 HEADER_COMMENT
后需要重跑。
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

SPECIES = [
    ("capybara", "CAPYBARA"),
    ("duck",     "DUCK"),
    ("goose",    "GOOSE"),
    ("blob",     "BLOB"),
    ("cat",      "CAT"),
    ("dragon",   "DRAGON"),
    ("octopus",  "OCTOPUS"),
    ("owl",      "OWL"),
    ("penguin",  "PENGUIN"),
    ("turtle",   "TURTLE"),
    ("snail",    "SNAIL"),
    ("ghost",    "GHOST"),
    ("axolotl",  "AXOLOTL"),
    ("cactus",   "CACTUS"),
    ("robot",    "ROBOT"),
    ("rabbit",   "RABBIT"),
    ("mushroom", "MUSHROOM"),
    ("chonk",    "CHONK"),
]

STATES = ["sleep", "idle", "busy", "attention", "celebrate", "dizzy", "heart"]

HEADER_COMMENT_TEMPLATE = """/**
 * @file persona_{name}.c
 * @brief "{name}" ASCII 人格状态机。
 *
 * 由 tools/port_buddies.py 从 claude-desktop-buddy/src/buddies/{name}.cpp
 * 机械化迁移而来。为保持与上游 1:1 对应，sprite 数据、tick 序列与 overlay
 * 布局一律保留；RGB565 颜色参数在单色 OLED 上被实现忽略。
 *
 * 七个状态函数（对应 buddy_persona_state_e）：
 *   __{name}_sleep / __{name}_idle / __{name}_busy / __{name}_attention /
 *   __{name}_celebrate / __{name}_dizzy / __{name}_heart
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project (port)
 * @copyright Copyright (c) claude-desktop-buddy authors (frame data)
 */

#include "ascii_persona.h"
#include <stdint.h>

"""

# ---------------------------------------------------------------------------
# 正则
# ---------------------------------------------------------------------------
NAMESPACE_OPEN_RE = re.compile(r"namespace\s+(\w+)\s*\{")
NAMESPACE_CLOSE_RE = re.compile(r"\}\s*//\s*namespace\s+\w+")
DOFN_RE = re.compile(
    r"static\s+void\s+do(Sleep|Idle|Busy|Attention|Celebrate|Dizzy|Heart)\s*\(",
    re.IGNORECASE,
)
SPECIES_BLOCK_RE = re.compile(
    r"extern\s+const\s+Species\s+\w+_SPECIES\s*=\s*\{[^}]*\{[^}]*\}\s*\};",
    re.DOTALL,
)

# 头部 include 相关行 — 全部删除后统一重写
PREAMBLE_STRIP_RE = re.compile(
    r'^\s*#include\s+"\.\./buddy\.h"\s*$|'
    r'^\s*#include\s+"\.\./buddy_common\.h"\s*$|'
    r'^\s*#include\s+<M5StickCPlus\.h>\s*$|'
    r'^\s*#include\s+<string\.h>\s*$|'
    r'^\s*extern\s+TFT_eSprite\s+spr\s*;\s*$',
    re.MULTILINE,
)

# 调用改名 + 可选参数补齐
CALL_RENAMES = {
    "buddyPrintSprite": "ascii_print_sprite",
    "buddyPrintLine":   "ascii_print_line",
    "buddySetCursor":   "ascii_set_cursor",
    "buddySetColor":    "ascii_set_color",
    "buddyPrint":       "ascii_print",
}


def split_top_level_args(arg_str: str) -> list[str]:
    """按最外层逗号切分参数；忽略括号/方括号内嵌套。"""
    out, buf, depth = [], [], 0
    for ch in arg_str:
        if ch in "([{":
            depth += 1
            buf.append(ch)
        elif ch in ")]}":
            depth -= 1
            buf.append(ch)
        elif ch == "," and depth == 0:
            out.append("".join(buf).strip())
            buf = []
        else:
            buf.append(ch)
    tail = "".join(buf).strip()
    if tail or out:
        out.append(tail)
    return out


def rewrite_call(match: re.Match, new_name: str, pad_to: int | None) -> str:
    """重写一个已匹配的函数调用；可选补齐 x_off=0。"""
    head, args_str = match.group(1), match.group(2)
    args = split_top_level_args(args_str)
    if pad_to and len(args) == pad_to - 1:
        args.append("0")
    return f"{new_name}({', '.join(args)}"


def replace_calls(src: str) -> str:
    """按 CALL_RENAMES 替换函数名，并对 sprite/line 调用补 x_off。"""
    # buddyPrintSprite: 4 参数补 0，5 参数原样
    src = re.sub(
        r"(\bbuddyPrintSprite\s*)\(([^;]*?)\)",
        lambda m: rewrite_call(m, "ascii_print_sprite", 5) + ")",
        src,
    )
    # buddyPrintLine: 3 参数补 0，4 参数原样
    src = re.sub(
        r"(\bbuddyPrintLine\s*)\(([^;]*?)\)",
        lambda m: rewrite_call(m, "ascii_print_line", 4) + ")",
        src,
    )
    # 其他调用直接 1:1 改名（无参数补齐）
    for old, new in CALL_RENAMES.items():
        if old in ("buddyPrintSprite", "buddyPrintLine"):
            continue
        src = re.sub(r"\b" + re.escape(old) + r"\b", new, src)
    return src


def rename_dofns(src: str, persona: str) -> str:
    """do<State> → __<persona>_<state>（在函数定义和引用处都生效）。"""
    lower = persona.lower()
    mapping = {f"do{s.capitalize()}": f"__{lower}_{s}" for s in STATES}
    # 先处理命名空间限定的调用：<persona>::doSleep → __<persona>_sleep
    src = re.sub(
        rf"\b{lower}\s*::\s*do({'|'.join(s.capitalize() for s in STATES)})",
        lambda m: f"__{lower}_{m.group(1).lower()}",
        src,
    )
    # 再处理其余 doXxx 标识（函数定义 / 同命名空间内的局部引用）
    for old, new in mapping.items():
        src = re.sub(r"\b" + re.escape(old) + r"\b", new, src)
    return src


def strip_namespace_brackets(src: str) -> str:
    """移除 namespace <name> { 与其闭合 }; 保留内部内容。"""
    src = NAMESPACE_OPEN_RE.sub(
        lambda m: f"/* ---- persona: {m.group(1)} ---- */",
        src,
    )
    src = NAMESPACE_CLOSE_RE.sub("/* ---- end persona ---- */", src)
    return src


def build_registration(persona: str, upper: str) -> str:
    lower = persona.lower()
    fns = ", ".join(f"__{lower}_{s}" for s in STATES)
    return (
        f"\n/* ---------------------------------------------------------------------------\n"
        f" * Registration\n"
        f" * --------------------------------------------------------------------------- */\n"
        f"const ascii_persona_t PERSONA_{upper} = {{\n"
        f"    .name = \"{lower}\",\n"
        f"    .states = {{ {fns} }},\n"
        f"}};\n"
    )


def convert(src_path: Path, out_path: Path, persona: str, upper: str) -> None:
    raw = src_path.read_text(encoding="utf-8")

    # 先删除最后的 Species 注册块（防止其中的标识符被后续替换干扰）
    raw = SPECIES_BLOCK_RE.sub("", raw)

    # 移除 preamble / TFT_eSprite 引用
    raw = PREAMBLE_STRIP_RE.sub("", raw)

    # namespace → 注释
    raw = strip_namespace_brackets(raw)

    # 重命名 do<State>
    raw = rename_dofns(raw, persona)

    # 替换绘制原语名
    raw = replace_calls(raw)

    # 清理开头连续空行
    raw = re.sub(r"^\s*\n+", "", raw)

    out = HEADER_COMMENT_TEMPLATE.format(name=persona) + raw.rstrip() + "\n"
    out += build_registration(persona, upper)

    out_path.write_text(out, encoding="utf-8", newline="\n")
    print(f"[ok] {persona:<10} -> {out_path.name} ({out_path.stat().st_size} bytes)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("src_dir", help="claude-desktop-buddy/src/buddies 目录")
    ap.add_argument("out_dir", help="persona_*.c 输出目录")
    ap.add_argument("--only", nargs="*", help="仅处理指定人格名列表")
    args = ap.parse_args()

    src_dir = Path(args.src_dir)
    out_dir = Path(args.out_dir)
    if not src_dir.is_dir():
        print(f"ERROR: src_dir {src_dir} not found", file=sys.stderr)
        return 2
    out_dir.mkdir(parents=True, exist_ok=True)

    for persona, upper in SPECIES:
        if args.only and persona not in args.only:
            continue
        src_path = src_dir / f"{persona}.cpp"
        if not src_path.is_file():
            print(f"[skip] {persona}: {src_path} missing", file=sys.stderr)
            continue
        convert(src_path, out_dir / f"persona_{persona}.c", persona, upper)

    return 0


if __name__ == "__main__":
    sys.exit(main())
