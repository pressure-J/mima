#!/usr/bin/env python3
"""Verify selected high-score entries by exact LAT transition composition.

This script avoids the 2^32 plaintext enumeration for r=3 by composing the
single-round correlation transition exactly. For the selected single-active
inputs the full transition support is small enough to enumerate without beam
pruning.
"""

from __future__ import annotations

import math
from functools import lru_cache

SBOX = [0xC, 0x6, 0x9, 0x0, 0x1, 0xA, 0x2, 0xB, 0x3, 0x8, 0x5, 0xD, 0x4, 0xE, 0x7, 0xF]

CHECKS = [
    (2, 0x20000000, 0x00000888, 1.0),
    (2, 0x00002000, 0x08880000, 1.0),
    (2, 0x70000000, 0x00000DDD, 0.75),
    (2, 0x60000000, 0x00000999, 0.75),
    (3, 0x20000000, 0xEEE00E0E, 0.125),
    (3, 0x20000000, 0x44400404, 0.125),
    (3, 0x20000000, 0xE44A0404, 0.125),
    (3, 0x40000000, 0x88800808, 0.125),
]


def parity(x: int) -> int:
    return x.bit_count() & 1


def nibbles(x: int) -> list[int]:
    return [(x >> (28 - 4 * i)) & 0xF for i in range(8)]


def from_nibbles(xs: list[int]) -> int:
    out = 0
    for i, x in enumerate(xs):
        out |= x << (28 - 4 * i)
    return out


def mask_forward(c: list[int]) -> list[int]:
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


LAT = [[0.0] * 16 for _ in range(16)]
for a in range(16):
    for b in range(16):
        count = sum(parity(a & x) == parity(b & SBOX[x]) for x in range(16))
        LAT[a][b] = (count - 8) / 8.0


@lru_cache(maxsize=None)
def one_round(a: int) -> tuple[tuple[int, float], ...]:
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
        for out_mask, s_corr in choices[pos]:
            c.append(out_mask)
            dfs(pos + 1, c, corr * s_corr)
            c.pop()

    dfs(0, [], 1.0)
    return tuple(result.items())


def exact_transition_value(u: int, v: int, rounds: int) -> tuple[float, list[int]]:
    cur = {u: 1.0}
    sizes: list[int] = []
    for _ in range(rounds):
        nxt: dict[int, float] = {}
        for mask, corr in cur.items():
            for next_mask, trans_corr in one_round(mask):
                nxt[next_mask] = nxt.get(next_mask, 0.0) + corr * trans_corr
        sizes.append(len(nxt))
        cur = nxt
    return cur.get(v, 0.0), sizes


def score(ve: float, r: int) -> float:
    return math.log2((4**r) * abs(ve))


def main() -> None:
    print("r,u,v,expected,computed,score,support_sizes,status")
    ok = True
    for r, u, v, expected in CHECKS:
        computed, sizes = exact_transition_value(u, v, r)
        matched = abs(computed - expected) < 1e-15
        ok = ok and matched
        print(
            f"{r},0x{u:08X},0x{v:08X},{expected:.15g},"
            f"{computed:.15g},{score(computed, r):.4f},{sizes},"
            f"{'OK' if matched else 'FAIL'}"
        )
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
