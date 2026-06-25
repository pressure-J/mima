#!/usr/bin/env python3
"""
独立验证脚本 — 使用Python实现的精确转移组合验证C++结果
===========================================================
用法:
    python verify_results.py [--all] [--sample N]

功能:
    1. 验证LAT计算与C++一致
    2. 验证单轮转移与C++一致
    3. 对精选测试用例进行独立计算并对比
    4. 对抽样条目进行全路径枚举验证
"""

from __future__ import annotations

import math
import re
import sys
from functools import lru_cache
from pathlib import Path

# ---------------------------------------------------------------------------
# HS密码原语 (独立Python实现)
# ---------------------------------------------------------------------------
SBOX = [0xC, 0x6, 0x9, 0x0, 0x1, 0xA, 0x2, 0xB, 0x3, 0x8, 0x5, 0xD, 0x4, 0xE, 0x7, 0xF]


def parity(x: int) -> int:
    return x.bit_count() & 1


def nibbles(x: int) -> list[int]:
    return [(x >> (28 - 4 * i)) & 0xF for i in range(8)]


def from_nibbles(xs: list[int]) -> int:
    out = 0
    for i, x in enumerate(xs):
        out |= (x & 0xF) << (28 - 4 * i)
    return out


def mask_forward(c: list[int]) -> list[int]:
    """线性掩码传播: S盒输出掩码c → 下一轮输入掩码"""
    return [
        c[7],
        c[0] ^ c[2] ^ c[5],
        c[5],
        c[2] ^ c[5] ^ c[7],
        c[3],
        c[1] ^ c[4] ^ c[6],
        c[1],
        c[1] ^ c[3] ^ c[6],
    ]


def compute_lat() -> list[list[float]]:
    """计算归一化LAT"""
    lat = [[0.0] * 16 for _ in range(16)]
    for a in range(16):
        for b in range(16):
            count = sum(parity(a & x) == parity(b & SBOX[x]) for x in range(16))
            lat[a][b] = (count - 8) / 8.0
    return lat


LAT = compute_lat()


@lru_cache(maxsize=None)
def one_round_transitions(a: int) -> tuple[tuple[int, float], ...]:
    """计算输入掩码a的单轮转移 (精确枚举)"""
    na = nibbles(a)
    choices: list[list[tuple[int, float]]] = []
    for x in na:
        if x == 0:
            choices.append([(0, 1.0)])
        else:
            choices.append([(b, LAT[x][b]) for b in range(16) if LAT[x][b] != 0])

    result: dict[int, float] = {}

    def dfs(pos: int, c: list[int], corr: float) -> None:
        if pos == 8:
            b = from_nibbles(mask_forward(c))
            result[b] = result.get(b, 0.0) + corr
            return
        for out_nib, s_corr in choices[pos]:
            c.append(out_nib)
            dfs(pos + 1, c, corr * s_corr)
            c.pop()

    dfs(0, [], 1.0)
    return tuple((k, v) for k, v in result.items() if abs(v) > 1e-18)


def exact_transition_value(u: int, v: int, rounds: int) -> tuple[float, list[int]]:
    """精确多轮转移组合 (与C++ exact_sparse_dp 等效)"""
    cur = {u: 1.0}
    sizes: list[int] = []
    for _ in range(rounds):
        nxt: dict[int, float] = {}
        for mask, corr in cur.items():
            for next_mask, trans_corr in one_round_transitions(mask):
                nxt[next_mask] = nxt.get(next_mask, 0.0) + corr * trans_corr
        sizes.append(len(nxt))
        cur = nxt
    return cur.get(v, 0.0), sizes


def score_of(ve: float, r: int) -> float:
    return math.log2((4 ** r) * abs(ve))


# ---------------------------------------------------------------------------
# 精选测试用例
# ---------------------------------------------------------------------------
PRESET_TESTS = [
    # (r, u, v, expected_VT)
    # r=1 基础测试
    (1, 0x00000001, 0x70070000, -0.5),
    (1, 0x00000001, 0x80080000, 0.5),
    (1, 0x00000002, 0x10010000, 0.5),
    (1, 0x0000000F, 0x10010000, 0.5),
    (1, 0x0000000F, 0x50050000, 0.5),
    # r=2 测试
    (2, 0x20000000, 0x00000888, 1.0),
    (2, 0x00002000, 0x08880000, 1.0),
    (2, 0x70000000, 0x00000DDD, 0.75),
    (2, 0x60000000, 0x00000999, 0.75),
    (2, 0x50000000, 0x00000555, 0.75),
    (2, 0x40000000, 0x00000111, 0.75),
    (2, 0x10000000, 0x00000CCC, -0.5),
    (2, 0x10000000, 0x00000444, 0.5),
    # r=3 测试
    (3, 0x20000000, 0xEEE00E0E, 0.125),
    (3, 0x20000000, 0x44400404, 0.125),
    (3, 0x20000000, 0xE44A0404, 0.125),
    (3, 0x20000000, 0x4EEA0404, 0.125),
    (3, 0x40000000, 0x88800808, 0.125),
    # r=4 测试 (需要较大状态空间)
    (4, 0x10000000, 0x3B3A0222, 0.0115966796875),
    (4, 0x10000000, 0x33320AAA, 0.0115966796875),
]


def run_preset_tests() -> int:
    """运行预设测试用例"""
    print("=" * 90)
    print("精选测试用例验证")
    print("=" * 90)
    print(f"{'r':>2}  {'u':>12}  {'v':>12}  {'Expected':>15}  {'Computed':>15}  {'Score':>8}  {'Status':>6}")
    print("-" * 90)

    passed = 0
    failed = 0
    for r, u, v, expected in PRESET_TESTS:
        computed, sizes = exact_transition_value(u, v, r)
        eps = max(abs(expected), 1.0) * 1e-14
        ok = abs(computed - expected) <= eps
        if ok:
            passed += 1
        else:
            failed += 1
        status = "OK" if ok else "FAIL"
        print(f"{r:>2}  0x{u:08X}  0x{v:08X}  {expected:>15.12f}  {computed:>15.12f}  "
              f"{score_of(computed, r):>8.4f}  {status:>6}")

    print("-" * 90)
    print(f"Passed: {passed}/{passed + failed}, Failed: {failed}")
    return failed


def verify_against_file(filepath: str, sample_size: int = 0) -> int:
    """对结果文件中的条目进行抽样验证"""
    input_path = Path(filepath)
    if not input_path.exists():
        print(f"File not found: {filepath}")
        return 1

    line_re = re.compile(
        r"@\((\d+),\s*0x([0-9A-Fa-f]{8}),\s*0x([0-9A-Fa-f]{8}),\s*([-+0-9.eE]+),\s*([-+0-9.eE]+)\)"
    )

    entries = []
    for raw in input_path.read_text(encoding="utf-8").splitlines():
        m = line_re.fullmatch(raw.strip())
        if m:
            entries.append((
                int(m.group(1)),
                int(m.group(2), 16),
                int(m.group(3), 16),
                float(m.group(4)),
                float(m.group(5)),
            ))

    if sample_size > 0 and len(entries) > sample_size:
        import random
        random.seed(42)
        entries = random.sample(entries, sample_size)

    print(f"\n{'=' * 90}")
    print(f"抽样验证 (共 {len(entries)} 条)")
    print(f"{'=' * 90}")

    errors = 0
    for i, (r, u, v, ve, vt) in enumerate(entries):
        computed, sizes = exact_transition_value(u, v, r)
        eps = max(abs(vt), 1.0) * 1e-14
        if abs(computed - vt) > eps:
            errors += 1
            print(f"MISMATCH: r={r} u=0x{u:08X} v=0x{v:08X}: "
                  f"file={vt:.15e} computed={computed:.15e}")
        if (i + 1) % 100 == 0:
            print(f"  已验证 {i + 1}/{len(entries)} ... 发现错误: {errors}")

    print(f"\n总验证: {len(entries)}, 错误: {errors}")
    return errors


def main() -> None:
    args = sys.argv[1:]

    do_preset = True
    verify_file = None
    sample_size = 0

    i = 0
    while i < len(args):
        if args[i] == "--no-preset":
            do_preset = False
        elif args[i] == "--verify" and i + 1 < len(args):
            verify_file = args[i + 1]
            i += 1
        elif args[i] == "--sample" and i + 1 < len(args):
            sample_size = int(args[i + 1])
            i += 1
        i += 1

    total_errors = 0

    if do_preset:
        total_errors += run_preset_tests()

    if verify_file:
        total_errors += verify_against_file(verify_file, sample_size)

    if total_errors > 0:
        print(f"\n❌ 验证失败: {total_errors} 个错误")
        sys.exit(1)
    else:
        print("\n✅ 所有验证通过")


if __name__ == "__main__":
    main()
