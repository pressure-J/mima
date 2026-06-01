#!/usr/bin/env python3
"""计算所有有效估计值的得分: score = log2(2^(2r) * |VE|)"""
import math

data = [
    # (r, u, v, VE)
    (1, '0x10000000', '0x07000000', -0.5),
    (1, '0x10000000', '0x08000000', 0.5),
    (1, '0x10000000', '0x01000000', -0.25),
    (1, '0x00000001', '0x10010000', -0.25),
    (1, '0x00000003', '0x30030000', 0.5),
    (1, '0x0000000F', '0xF00F0000', 0.25),
    (2, '0x10000000', '0x00000CCC', -0.5),
    (2, '0x10000000', '0x00000444', 0.5),
    (2, '0x10000000', '0x00000111', 0.25),
    (2, '0x10000000', '0x00000222', 0.25),
    (2, '0x10000000', '0x00000555', -0.25),
    (2, '0x10000000', '0x00000999', 0.25),
    (2, '0x10000000', '0x00000666', 0.25),
    (2, '0x10000000', '0x00000AAA', 0.25),
    (2, '0x10000000', '0x00000DDD', -0.25),
    (2, '0x10000000', '0x00000EEE', 0.25),
    (3, '0x10000000', '0xA2280202', -0.1015625),
    (3, '0x10000000', '0x64420404', -0.09375),
    (3, '0x10000000', '0x22200A0A', -0.1015625),
    (3, '0x10000000', '0x2AA80202', -0.1015625),
    (3, '0x10000000', '0xAAA00A0A', -0.1015625),
    (3, '0x10000000', '0x44400606', -0.09375),
    (3, '0x10000000', '0x46620404', -0.09375),
    (3, '0x10000000', '0x66600606', -0.0859375),
    (3, '0x10000000', '0x99900909', -0.0390625),
]

print("=" * 85)
print("Score Formula: score = log2(2^(2r) * |VE|) = log2(4^r * |VE|)")
print("=" * 85)

for r in [1, 2, 3]:
    items = [(u, v, ve) for rr, u, v, ve in data if rr == r]
    print(f"\n=== r = {r} ({len(items)} valid estimates) ===")
    header = f"{'No':>3}  {'u':>12}  {'v':>12}  {'|VE|':>16}  {'4^r*|VE|':>14}  Score"
    print(header)
    print("-" * 75)

    for i, (u, v, ve) in enumerate(items, 1):
        abs_ve = abs(ve)
        factor = 4.0 ** r
        prod = factor * abs_ve
        score = math.log2(prod)
        print(f"{i:3d}  {u:>12}  {v:>12}  {abs_ve:16.10f}  {prod:14.10f}  {score:7.4f}")

print("\n" + "=" * 85)
print("STATISTICS SUMMARY")
print("=" * 85)
for r in [1, 2, 3]:
    scores = [math.log2(4.0**r * abs(ve)) for rr, _, _, ve in data if rr == r]
    print(f"r={r}: count={len(scores):2d}, max={max(scores):7.4f}, min={min(scores):7.4f}, avg={sum(scores)/len(scores):7.4f}")

grand_scores = [math.log2(4.0**r * abs(ve)) for r, _, _, ve in data]
print(f"Total: 25 valid estimates, grand avg score = {sum(grand_scores)/len(grand_scores):7.4f}")
