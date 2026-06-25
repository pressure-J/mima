#!/usr/bin/env python3
"""
赛题三计分脚本 — 解析有效估计值文件, 计算得分
===================================================
用法:
    python compute_scores.py <input_file> [output_file]

输入格式:
    @(r, 0xUUUUUUUU, 0xVVVVVVVV, VE, VT)

输出:
    按轮数分组, 验证有效性, 计算得分, 打印摘要
"""

from __future__ import annotations

import math
import re
import sys
from collections import defaultdict
from pathlib import Path

LINE_RE = re.compile(
    r"@\((\d+),\s*0x([0-9A-Fa-f]{8}),\s*0x([0-9A-Fa-f]{8}),\s*([-+0-9.eE]+),\s*([-+0-9.eE]+)\)"
)


def parse_entries(path: Path) -> list[tuple[int, int, int, float, float]]:
    """解析有效估计值文件"""
    entries: list[tuple[int, int, int, float, float]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip().lstrip("﻿")
        if not line or line.startswith("#"):
            continue
        match = LINE_RE.fullmatch(line)
        if not match:
            raise ValueError(f"Bad line format: {line}")
        rounds = int(match.group(1))
        u = int(match.group(2), 16)
        v = int(match.group(3), 16)
        ve = float(match.group(4))
        vt = float(match.group(5))
        entries.append((rounds, u, v, ve, vt))
    return entries


def is_valid(rounds: int, u: int, v: int, ve: float, vt: float) -> tuple[bool, float]:
    """检查有效性条件: |VE - VT| <= |VT| * 2^(-2r), VE != 0, u != 0, v != 0"""
    if ve == 0.0 or u == 0 or v == 0:
        return False, 0.0
    bound = abs(vt) * math.pow(2.0, -2.0 * rounds)
    return abs(ve - vt) <= bound, bound


def score_of(rounds: int, ve: float) -> float:
    """score = log2(4^r * |VE|)"""
    return math.log2(math.pow(4.0, rounds) * abs(ve))


def main() -> None:
    if len(sys.argv) < 2:
        print("Usage: python compute_scores.py <input_file> [output_file]")
        sys.exit(1)

    input_path = Path(sys.argv[1])
    if not input_path.exists():
        print(f"Error: file not found: {input_path}")
        sys.exit(1)

    entries = parse_entries(input_path)

    # 按轮数分组
    grouped: dict[int, list[tuple[int, int, int, float, float]]] = defaultdict(list)
    for entry in entries:
        grouped[entry[0]].append(entry)

    lines: list[str] = []
    total_score = 0.0
    valid_count = 0
    invalid_count = 0

    lines.append("=" * 80)
    lines.append("赛题三有效估计值计分结果 (方式2逼近算法)")
    lines.append("评分公式: score = log2(4^r * |VE|)")
    lines.append("有效性条件: |VE-VT| <= |VT| * 2^(-2r), VE != 0, u != 0, v != 0")
    lines.append("=" * 80)
    lines.append("")

    for rounds in sorted(grouped):
        lines.append("-" * 80)
        lines.append(f"r = {rounds}  ({len(grouped[rounds])} entries)")
        lines.append("-" * 80)

        round_scores: list[float] = []
        round_valid = 0
        round_invalid = 0

        for r, u, v, ve, vt in grouped[rounds]:
            valid, bound = is_valid(r, u, v, ve, vt)
            score = score_of(r, ve)
            if valid:
                round_valid += 1
                round_scores.append(score)
                total_score += score
                valid_count += 1
            else:
                round_invalid += 1
                invalid_count += 1

        if round_scores:
            lines.append(
                f"  有效: {round_valid}, 无效: {round_invalid}"
            )
            lines.append(
                f"  得分: sum={sum(round_scores):.4f}, "
                f"max={max(round_scores):.4f}, "
                f"min={min(round_scores):.4f}, "
                f"avg={sum(round_scores)/len(round_scores):.4f}"
            )
        lines.append("")

    lines.append("=" * 80)
    lines.append(f"总有效估计值: {valid_count}")
    lines.append(f"总无效估计值: {invalid_count}")
    lines.append(f"总得分: {total_score:.4f}")
    lines.append("=" * 80)

    text = "\n".join(lines)
    print(text)

    # 写入输出文件
    if len(sys.argv) >= 3:
        output_path = Path(sys.argv[2])
        output_path.write_text(text + "\n", encoding="utf-8")
        print(f"\n结果已写入: {output_path}")


if __name__ == "__main__":
    main()
