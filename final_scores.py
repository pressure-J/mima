#!/usr/bin/env python3
"""Score valid estimates from results/valid_estimates.txt."""

from __future__ import annotations

import math
import re
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent
RESULTS_DIR = ROOT / "results"
INPUT_PATH = RESULTS_DIR / "valid_estimates.txt"
OUTPUT_PATH = RESULTS_DIR / "scores.txt"

LINE_RE = re.compile(
    r"@\((\d+),\s*0x([0-9A-Fa-f]{8}),\s*0x([0-9A-Fa-f]{8}),\s*([-+0-9.eE]+),\s*([-+0-9.eE]+)\)"
)


def parse_entries(path: Path) -> list[tuple[int, int, int, float, float]]:
    entries: list[tuple[int, int, int, float, float]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip().lstrip("\ufeff")
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
    if ve == 0.0 or u == 0 or v == 0:
        return False, 0.0
    bound = abs(vt) * math.pow(2.0, -2.0 * rounds)
    return abs(ve - vt) <= bound, bound


def score_of(rounds: int, ve: float) -> float:
    return math.log2(math.pow(4.0, rounds) * abs(ve))


def main() -> None:
    entries = parse_entries(INPUT_PATH)
    grouped: dict[int, list[tuple[int, int, int, float, float]]] = defaultdict(list)
    for entry in entries:
        grouped[entry[0]].append(entry)

    lines: list[str] = []
    total_score = 0.0
    valid_count = 0

    lines.append("赛题三有效估计值计分结果")
    lines.append("评分公式: score = log2(2^(2r) * |VE|) = log2(4^r * |VE|)")
    lines.append("")

    for rounds in sorted(grouped):
        lines.append("=" * 90)
        lines.append(f"r = {rounds}")
        lines.append("=" * 90)
        lines.append("No  u           v           VE                 VT                 Bound              Score")

        round_scores: list[float] = []
        for idx, (r, u, v, ve, vt) in enumerate(grouped[rounds], start=1):
            valid, bound = is_valid(r, u, v, ve, vt)
            if not valid:
                continue
            score = score_of(r, ve)
            total_score += score
            valid_count += 1
            round_scores.append(score)
            lines.append(
                f"{idx:2d}  0x{u:08X}  0x{v:08X}  "
                f"{ve:18.15f}  {vt:18.15f}  {bound:16.8e}  {score:8.4f}"
            )

        if round_scores:
            lines.append(
                f"r={rounds}: count={len(round_scores)}, sum={sum(round_scores):.4f}, "
                f"max={max(round_scores):.4f}, min={min(round_scores):.4f}, "
                f"avg={sum(round_scores)/len(round_scores):.4f}"
            )
        lines.append("")

    lines.append("=" * 90)
    lines.append(f"Total valid estimates: {valid_count}")
    lines.append(f"Total score: {total_score:.4f}")

    text = "\n".join(lines)
    print(text)
    OUTPUT_PATH.write_text(text + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
