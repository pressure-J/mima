#!/usr/bin/env python3
"""
批量实验运行脚本
=================
协调C++程序的批量执行, 收集结果, 计算得分.

用法:
    python run_experiments.py --max-r 3 [--beam 200000] [--output ../results]
    python run_experiments.py --positions 0,2,3,4,6,7 --max-r 3
    python run_experiments.py --missing-only --max-r 3  # 只计算之前缺失的
"""

from __future__ import annotations

import math
import os
import re
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# 配置
# ---------------------------------------------------------------------------
ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "src"
EXE = ROOT / "approx_cor"
if os.name == "nt":
    EXE = EXE.with_suffix(".exe")

RESULTS_DIR = ROOT / "results"
TMP_DIR = RESULTS_DIR / "tmp"

# 120个单活跃输入掩码
ALL_INPUTS = []
for pos in range(8):
    for nib in range(1, 16):
        mask = nib << (28 - 4 * pos)
        ALL_INPUTS.append((pos, nib, mask))

# 已知有问题的位置 (状态爆炸)
PROBLEM_POSITIONS = {1, 5}


def build() -> bool:
    """编译C++程序"""
    print("Building C++ program...")
    result = subprocess.run(
        ["g++", "-O3", "-std=c++17", "-Wall", "-march=native",
         "-o", str(EXE), str(SRC_DIR / "main.cpp"), "-I", str(SRC_DIR)],
        cwd=str(ROOT),
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"Build failed:\n{result.stderr}")
        return False
    print("Build successful.")
    return True


def run_batch_position(pos: int, max_r: int, beam: int, output_dir: Path) -> tuple[int, float, str]:
    """运行单个位置的批量搜索"""
    output_dir.mkdir(parents=True, exist_ok=True)
    output_file = output_dir / f"pos{pos}_r{max_r}.txt"

    start = time.time()
    result = subprocess.run(
        [str(EXE), "batch-position", str(max_r), str(pos),
         "--beam", str(beam), "--output", str(output_dir)],
        cwd=str(ROOT),
        capture_output=True,
        text=True,
        timeout=7200,  # 2小时超时
    )
    elapsed = time.time() - start

    # 解析条目数
    count = 0
    for line in result.stdout.splitlines():
        m = re.search(r"Total: (\d+) entries", line)
        if m:
            count = int(m.group(1))
            break

    return count, elapsed, result.stdout


def parse_existing_entries(filepath: Path) -> set[tuple[int, int, int]]:
    """解析已有结果文件, 获取 (r, u, v) 集合"""
    entries = set()
    if not filepath.exists():
        return entries

    line_re = re.compile(
        r"@\((\d+),\s*0x([0-9A-Fa-f]{8}),\s*0x([0-9A-Fa-f]{8}),"
    )
    for line in filepath.read_text(encoding="utf-8").splitlines():
        m = line_re.match(line.strip())
        if m:
            entries.add((int(m.group(1)), int(m.group(2), 16), int(m.group(3), 16)))
    return entries


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser(description="批量实验运行器")
    parser.add_argument("--max-r", type=int, default=3, help="最大轮数")
    parser.add_argument("--beam", type=int, default=200000, help="波束宽度")
    parser.add_argument("--output", type=str, default=str(RESULTS_DIR), help="输出目录")
    parser.add_argument("--positions", type=str, default=None,
                        help="指定位置 (逗号分隔), 默认全部")
    parser.add_argument("--skip-build", action="store_true", help="跳过编译")
    parser.add_argument("--timeout", type=int, default=7200, help="每个位置超时(秒)")
    args = parser.parse_args()

    output_dir = Path(args.output)
    tmp_dir = output_dir / "tmp"
    tmp_dir.mkdir(parents=True, exist_ok=True)

    # 编译
    if not args.skip_build:
        if not build():
            return

    # 确定要处理的位置
    if args.positions:
        positions = [int(p) for p in args.positions.split(",")]
    else:
        positions = list(range(8))

    print(f"\n{'=' * 60}")
    print(f"批量实验: max_r={args.max_r}, beam={args.beam}")
    print(f"位置: {positions}")
    print(f"输出: {output_dir}")
    print(f"{'=' * 60}\n")

    all_entries_count = 0
    total_score = 0.0
    successes = 0
    failures = 0

    for pos in positions:
        print(f"[pos={pos}] ", end="", flush=True)

        try:
            count, elapsed, output = run_batch_position(
                pos, args.max_r, args.beam, tmp_dir
            )
            if count > 0:
                status = "OK"
                successes += 1
            else:
                status = "EMPTY"
                failures += 1

            print(f"{count:>6} entries, {elapsed:>8.1f}s [{status}]")
            all_entries_count += count

            # 解析得分
            for line in output.splitlines():
                m = re.search(r"Total: [\d.]+ entries, score = ([\d.]+)", line)
                if m:
                    total_score += float(m.group(1))

        except subprocess.TimeoutExpired:
            print(f"{'TIMEOUT':>20} [FAIL]")
            failures += 1
        except Exception as e:
            print(f"{'ERROR':>20} [{e}]")
            failures += 1

    # 合并所有位置的结果
    print(f"\n{'=' * 60}")
    print(f"合并结果...")
    combined_file = output_dir / "valid_estimates.txt"
    all_lines = []
    all_lines.append("# Valid estimates generated by Way-2 approximation algorithm")
    all_lines.append("# Format: @(r, u, v, VE, VT)")
    all_lines.append(f"# Parameters: max_r={args.max_r}, beam={args.beam}")
    all_lines.append("")

    for pos in positions:
        pos_file = tmp_dir / f"pos{pos}_r{args.max_r}.txt"
        if pos_file.exists():
            content = pos_file.read_text(encoding="utf-8")
            for line in content.splitlines():
                if line.startswith("@"):
                    all_lines.append(line)

    combined_file.write_text("\n".join(all_lines) + "\n", encoding="utf-8")
    print(f"合并完成: {combined_file} ({len(all_lines) - 4} entries)")

    # 计分
    print(f"\n{'=' * 60}")
    print(f"运行计分脚本...")
    score_file = output_dir / "scores.txt"
    subprocess.run(
        [sys.executable, str(ROOT / "scripts" / "compute_scores.py"),
         str(combined_file), str(score_file)],
        cwd=str(ROOT),
    )

    print(f"\n{'=' * 60}")
    print(f"实验完成")
    print(f"  成功位置: {successes}, 失败: {failures}")
    print(f"  总条目: {all_entries_count}")
    print(f"  结果目录: {output_dir}")
    print(f"{'=' * 60}")


if __name__ == "__main__":
    main()
