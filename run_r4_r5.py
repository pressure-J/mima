#!/usr/bin/env python3
"""Compute valid estimates for r=4 and r=5 using exact sparse correlation composition.

Key optimizations:
- LRU cache for one_round transitions (all masks, not just ≤2 active nibbles)
- Limits intermediate distribution size to prevent explosion
- Progress reporting per position
"""

from __future__ import annotations

import math
import sys
from functools import lru_cache
from pathlib import Path
from collections import defaultdict

ROOT = Path(__file__).resolve().parent
RESULTS_DIR = ROOT / "results"

SBOX = [0xC, 0x6, 0x9, 0x0, 0x1, 0xA, 0x2, 0xB, 0x3, 0x8, 0x5, 0xD, 0x4, 0xE, 0x7, 0xF]


def parity(x: int) -> int:
    return x.bit_count() & 1


def nibbles(x: int) -> list[int]:
    return [(x >> (28 - 4 * i)) & 0xF for i in range(8)]


def from_nibbles(xs: list[int]) -> int:
    out = 0
    for i, x in enumerate(xs):
        out |= x << (28 - 4 * i)
    return out


def active_nibbles_count(x: int) -> int:
    count = 0
    for i in range(8):
        if ((x >> (4 * i)) & 0xF) != 0:
            count += 1
    return count


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


# Build LAT
LAT = [[0.0] * 16 for _ in range(16)]
NONZERO_LAT = [[] for _ in range(16)]
for a in range(16):
    for b in range(16):
        count = sum(parity(a & x) == parity(b & SBOX[x]) for x in range(16))
        val = (count - 8) / 8.0
        LAT[a][b] = val
        if abs(val) > 1e-15:
            NONZERO_LAT[a].append((b, val))


@lru_cache(maxsize=200000)
def one_round_transitions(a: int) -> tuple[tuple[int, float], ...]:
    """Compute all one-round correlation transitions for input mask a."""
    na = nibbles(a)
    choices = []
    for x in na:
        if x == 0:
            choices.append([(0, 1.0)])
        else:
            choices.append([(b, LAT[x][b]) for b in range(16) if abs(LAT[x][b]) > 1e-15])

    result: dict[int, float] = {}

    def dfs(pos: int, c: list[int], corr: float) -> None:
        if pos == 8:
            b = from_nibbles(mask_forward(c))
            if b != 0:
                result[b] = result.get(b, 0.0) + corr
            return
        for out_mask, s_corr in choices[pos]:
            c.append(out_mask)
            dfs(pos + 1, c, corr * s_corr)
            c.pop()

    dfs(0, [], 1.0)
    # Filter tiny values
    return tuple((k, v) for k, v in result.items() if abs(v) > 1e-18)


def exact_distribution(u: int, rounds: int, max_states: int = 5000000) -> dict[int, float]:
    """Compute exact sparse correlation distribution after `rounds` rounds.

    Args:
        u: Input mask
        rounds: Number of rounds
        max_states: Maximum number of intermediate states before pruning

    Returns:
        Dict mapping output mask to correlation value
    """
    current = {u: 1.0}

    for r in range(rounds):
        nxt: dict[int, float] = {}
        for mask, corr in current.items():
            transitions = one_round_transitions(mask)
            for next_mask, trans_corr in transitions:
                nxt[next_mask] = nxt.get(next_mask, 0.0) + corr * trans_corr

        # Remove tiny values
        current = {k: v for k, v in nxt.items() if abs(v) > 1e-18}

        # If distribution is too large, keep only the strongest entries
        if len(current) > max_states:
            sorted_items = sorted(current.items(), key=lambda x: abs(x[1]), reverse=True)
            current = dict(sorted_items[:max_states])

        sys.stderr.write(f"  Round {r+1}: {len(current)} states\n")

    return current


def score_of(rounds: int, ve: float) -> float:
    return math.log2((4.0 ** rounds) * abs(ve))


def single_active_inputs() -> list[int]:
    """All single-active input masks (one nibble non-zero)."""
    masks = []
    for pos in range(8):
        for nib in range(1, 16):
            masks.append(nib << (28 - 4 * pos))
    return masks


def compute_entries_for_round(rounds: int, position: int | None = None) -> list[tuple[int, int, int, float, float, float]]:
    """Compute all positive-score entries for a given round.

    Args:
        rounds: Number of rounds
        position: If set, only compute for this nibble position (0-7). If None, compute all.

    Returns:
        List of (rounds, u, v, ve, vt, score) tuples
    """
    entries = []
    min_abs = 1.0 / (4.0 ** rounds)

    if position is not None:
        inputs = [(position, nib) for nib in range(1, 16)]
    else:
        inputs = [(pos, nib) for pos in range(8) for nib in range(1, 16)]

    for idx, (pos, nib) in enumerate(inputs):
        u = nib << (28 - 4 * pos)
        sys.stderr.write(f"  [{idx+1}/{len(inputs)}] pos={pos} nib={nib:#x} u=0x{u:08X} r={rounds}\n")

        # Clear cache periodically to avoid memory issues
        if idx % 10 == 0:
            one_round_transitions.cache_clear()

        dist = exact_distribution(u, rounds, max_states=3000000)

        for v, corr in dist.items():
            if v == 0:
                continue
            if abs(corr) <= min_abs:
                continue
            sc = score_of(rounds, corr)
            entries.append((rounds, u, v, corr, corr, sc))

    return entries


def write_results(all_entries: list[tuple[int, int, int, float, float, float]], output_dir: Path) -> None:
    """Write results in the required format."""
    output_dir.mkdir(parents=True, exist_ok=True)

    # Sort entries: by rounds, then by score descending, then by u, then by v
    all_entries.sort(key=lambda e: (e[0], -e[5], e[1], e[2]))

    # Write valid_estimates.txt (append mode for new rounds)
    estimates_path = output_dir / "valid_estimates.txt"

    # Read existing entries to avoid duplicates
    existing = set()
    if estimates_path.exists():
        for line in estimates_path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line.startswith("@("):
                # Parse existing entry to get (r, u, v)
                import re
                m = re.match(r"@\((\d+),\s*0x([0-9A-Fa-f]{8}),\s*0x([0-9A-Fa-f]{8})", line)
                if m:
                    existing.add((int(m.group(1)), int(m.group(2), 16), int(m.group(3), 16)))

    new_entries = [e for e in all_entries if (e[0], e[1], e[2]) not in existing]

    if new_entries:
        with open(estimates_path, "a", encoding="utf-8") as f:
            f.write(f"\n# Added r={new_entries[0][0]} entries\n")
            for r, u, v, ve, vt, sc in new_entries:
                f.write(f"@({r}, 0x{u:08X}, 0x{v:08X}, {ve:.15f}, {vt:.15f})\n")
        print(f"Appended {len(new_entries)} new entries for r={new_entries[0][0]}")

    # Write separate round file
    for rounds in sorted(set(e[0] for e in all_entries)):
        round_entries = [e for e in all_entries if e[0] == rounds]
        round_path = output_dir / f"r{rounds}_estimates.txt"
        with open(round_path, "w", encoding="utf-8") as f:
            f.write(f"# Valid estimates for r={rounds}\n")
            f.write(f"# Format: @(r, u, v, VE, VT)\n")
            for r, u, v, ve, vt, sc in round_entries:
                f.write(f"@({r}, 0x{u:08X}, 0x{v:08X}, {ve:.15f}, {vt:.15f})\n")
        print(f"Wrote {len(round_entries)} entries to {round_path}")

    # Print score summary
    print(f"\nScore Summary:")
    grouped = defaultdict(list)
    for e in all_entries:
        grouped[e[0]].append(e[5])

    total_score = 0.0
    for rounds in sorted(grouped):
        scores = grouped[rounds]
        total = sum(scores)
        total_score += total
        print(f"  r={rounds}: count={len(scores)}, sum={total:.4f}, max={max(scores):.4f}, "
              f"min={min(scores):.4f}, avg={total/len(scores):.4f}")
    print(f"  Total: {total_score:.4f}")


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Compute valid estimates for r=4 and r=5")
    parser.add_argument("--rounds", type=int, nargs="+", default=[4, 5], help="Rounds to compute")
    parser.add_argument("--position", type=int, default=None, help="Only compute specific position (0-7)")
    parser.add_argument("--output-dir", type=str, default=str(RESULTS_DIR))
    args = parser.parse_args()

    output_dir = Path(args.output_dir)

    all_entries = []
    for r in args.rounds:
        print(f"\n{'='*60}")
        print(f"Computing r={r}")
        print(f"{'='*60}")
        entries = compute_entries_for_round(r, position=args.position)
        all_entries.extend(entries)
        print(f"Found {len(entries)} valid entries for r={r}")

    if all_entries:
        write_results(all_entries, output_dir)

        # Also run the scoring script
        print(f"\nRun: python final_scores.py to generate updated scores")


if __name__ == "__main__":
    main()
