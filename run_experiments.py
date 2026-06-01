#!/usr/bin/env python3
"""
快速实验脚本: 生成用于论文的实验数据
赛题三: 矩阵连乘元素的逼近
"""
import subprocess
import sys
import math
import random
from pathlib import Path

ALGO_DIR = Path("E:/gaoxiaom/algorithm")
APPROX_EXE = ALGO_DIR / "approx_cor.exe"

# S盒和LAT
SBOX = [0xC, 0x6, 0x9, 0x0, 0x1, 0xA, 0x2, 0xB, 0x3, 0x8, 0x5, 0xD, 0x4, 0xE, 0x7, 0xF]

def compute_LAT():
    """计算S盒LAT"""
    lat = [[0]*16 for _ in range(16)]
    for a in range(16):
        for b in range(16):
            cnt = 0
            for x in range(16):
                a_dot = bin(a & x).count('1') & 1
                b_dot = bin(b & SBOX[x]).count('1') & 1
                if a_dot == b_dot:
                    cnt += 1
            lat[a][b] = cnt - 8
    return lat

def uint_to_nib(x):
    return [(x >> (28 - 4*i)) & 0xF for i in range(8)]

def nib_to_uint(nib):
    res = 0
    for i, n in enumerate(nib):
        res |= n << (28 - 4*i)
    return res

def mask_backward(b_nib):
    """反向掩码传播: c = L^T(b)"""
    c = [0]*8
    c[0] = b_nib[0] ^ b_nib[1] ^ b_nib[3]
    c[1] = b_nib[6]
    c[2] = b_nib[0] ^ b_nib[2] ^ b_nib[3]
    c[3] = b_nib[4]
    c[4] = b_nib[4] ^ b_nib[5] ^ b_nib[7]
    c[5] = b_nib[2]
    c[6] = b_nib[4] ^ b_nib[6] ^ b_nib[7]
    c[7] = b_nib[0]
    return c

def mask_forward(c_nib):
    """正向掩码传播: b = MC^{-T}(SR^{-T}(c))"""
    b = [0]*8
    b[0] = c_nib[7]
    b[1] = c_nib[0] ^ c_nib[2] ^ c_nib[5]
    b[2] = c_nib[5]
    b[3] = c_nib[2] ^ c_nib[5] ^ c_nib[7]
    b[4] = c_nib[3]
    b[5] = c_nib[1] ^ c_nib[4] ^ c_nib[6]
    b[6] = c_nib[1]
    b[7] = c_nib[1] ^ c_nib[3] ^ c_nib[6]
    return b

def single_round_corr(u, v, lat):
    """单轮相关度"""
    nu = uint_to_nib(u)
    nv = uint_to_nib(v)
    c = mask_backward(nv)
    corr = 1.0
    for j in range(8):
        corr *= lat[nu[j]][c[j]] / 8.0
    return corr

def enumerate_one_round(a, lat, min_abs=1e-10):
    """枚举单轮所有可能输出掩码"""
    na = uint_to_nib(a)
    results = []

    # 对每个S盒收集可能输出
    possible = []
    for j in range(8):
        if na[j] == 0:
            possible.append([0])
        else:
            cand = [cj for cj in range(16) if lat[na[j]][cj] != 0]
            possible.append(cand)

    # 递归枚举
    c_cur = [0]*8
    def dfs(pos, corr_prod):
        nonlocal results
        if pos == 8:
            if abs(corr_prod) >= min_abs:
                b = mask_forward(c_cur)
                results.append((nib_to_uint(b), corr_prod))
            return
        for cj in possible[pos]:
            c_cur[pos] = cj
            new_corr = corr_prod * lat[na[pos]][cj] / 8.0
            if abs(new_corr) >= min_abs / 10:  # 宽松剪枝
                dfs(pos + 1, new_corr)
    dfs(0, 1.0)
    return results

def approx_cor_sparse(u, v, r, lat, beam=50000):
    """稀疏矩阵迭代逼近"""
    if r == 0:
        return 1.0 if u == v else 0.0
    if r == 1:
        return single_round_corr(u, v, lat)

    cur = {u: 1.0}
    for rd in range(r):
        nxt = {}
        # 动态阈值
        abs_corrs = sorted([abs(c) for c in cur.values()], reverse=True)
        threshold = abs_corrs[min(beam, len(abs_corrs))-1] * 1e-3 if abs_corrs else 1e-12

        cnt = 0
        for mask, corr in cur.items():
            if abs(corr) < threshold and cnt > beam:
                continue
            cnt += 1

            local_thresh = threshold / max(1.0, abs(corr)) * 0.1
            for nm, tc in enumerate_one_round(mask, lat, local_thresh):
                nxt[nm] = nxt.get(nm, 0.0) + corr * tc

        # beam pruning
        if len(nxt) > beam:
            scored = sorted([(abs(c), m) for m, c in nxt.items()], reverse=True)
            cur = {m: nxt[m] for _, m in scored[:beam]}
        else:
            cur = nxt

        # print(f"  Round {rd+1}: {len(cur)} entries")

    return cur.get(v, 0.0)

def run_cpp_exact(u, v, r):
    """运行C++精确计算"""
    cmd = [str(APPROX_EXE), "1", f"0x{u:08X}", f"0x{v:08X}", str(r)]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
        for line in result.stdout.split('\n'):
            if "VT =" in line:
                return float(line.split("VT =")[1].strip())
    except:
        pass
    return None

def hw_nibble(x):
    """nibble Hamming weight"""
    return sum(1 for n in uint_to_nib(x) if n != 0)

# ============================================================
# 主实验
# ============================================================
def main():
    print("=" * 70)
    print("赛题三: 矩阵连乘元素的逼近 - 实验数据生成")
    print("=" * 70)

    lat = compute_LAT()

    # 生成系统化测试向量
    random.seed(12345)
    test_vectors = []

    # 类型1: 单活跃S盒
    for pos in range(8):
        for mask_val in [1, 3, 7, 0xF]:
            u = mask_val << (28 - 4*pos)
            # 找一个合适的输出掩码
            na = uint_to_nib(u)
            # 选择最大非零LAT输出
            c = [0]*8
            for j in range(8):
                if na[j] != 0:
                    best_b = max(range(16), key=lambda b: abs(lat[na[j]][b]))
                    c[j] = best_b
            v_nib = mask_forward(c)
            v = nib_to_uint(v_nib)
            if v != 0:
                test_vectors.append((u, v, f"单活跃(pos={pos},mask={mask_val})"))

    # 类型2: 双活跃S盒
    for p1, p2 in [(0,4), (1,5), (2,6), (3,7), (0,1), (2,3), (4,5), (6,7)]:
        for mask_val in [1, 3, 0xF]:
            u = (mask_val << (28 - 4*p1)) | (mask_val << (28 - 4*p2))
            na = uint_to_nib(u)
            c = [0]*8
            for j in range(8):
                if na[j] != 0:
                    c[j] = max(range(16), key=lambda b: abs(lat[na[j]][b]))
            v_nib = mask_forward(c)
            v = nib_to_uint(v_nib)
            if v != 0:
                test_vectors.append((u, v, f"双活跃({p1},{p2})"))

    # 添加赛题示例
    test_vectors.append((0x000ee0f0, 0x08088880, "赛题示例"))

    print(f"\n生成测试向量: {len(test_vectors)} 组")

    # ============================================================
    # 实验1: r=1,2 精确验证
    # ============================================================
    print("\n" + "=" * 70)
    print("实验1: r=1,2 精确验证 (方法2 vs 方法1)")
    print("=" * 70)

    for r in [1, 2]:
        print(f"\n--- r={r} ---")
        valid_count = 0
        total = 0

        for u, v, desc in test_vectors:
            if hw_nibble(u) > 2 and r >= 2:  # 对于r=2, 跳过3+活跃S盒以节省时间
                continue
            total += 1

            ve = approx_cor_sparse(u, v, r, lat)
            vt = run_cpp_exact(u, v, r)

            if vt is not None and ve != 0 and u != 0 and v != 0:
                bound = abs(vt) * math.pow(2.0, -2.0 * r)
                err = abs(ve - vt)
                if err <= bound:
                    valid_count += 1
                    score = math.log2(math.pow(2.0, 2.0 * r) * abs(ve))
                    print(f"  ✓ 0x{u:08X} → 0x{v:08X}: VE={ve:.8f}, VT={vt:.8f}, "
                          f"err={err:.2e}≤{bound:.2e}, 得分={score:.4f} [{desc}]")
                else:
                    print(f"  ✗ 0x{u:08X} → 0x{v:08X}: VE={ve:.8f}, VT={vt:.8f}, "
                          f"err={err:.2e}>{bound:.2e} [{desc}]")

        print(f"  r={r}: 有效/总数 = {valid_count}/{total}")

    # ============================================================
    # 实验2: r=3,4,5 方法2结果
    # ============================================================
    print("\n" + "=" * 70)
    print("实验2: r=3,4,5 方法2逼近结果")
    print("=" * 70)

    for r in [3, 4, 5]:
        print(f"\n--- r={r} ---")
        valid_count = 0

        # 对于较大r, 只测试单活跃S盒的向量以控制复杂度
        vectors_r = [(u, v, d) for u, v, d in test_vectors if hw_nibble(u) <= 1]

        for u, v, desc in vectors_r:
            ve = approx_cor_sparse(u, v, r, lat, beam=10000)

            if ve != 0 and u != 0 and v != 0:
                valid_count += 1
                score = math.log2(math.pow(2.0, 2.0 * r) * abs(ve))
                print(f"  ✓ 0x{u:08X} → 0x{v:08X}: VE={ve:.10f}, 得分={score:.4f} [{desc}]")
            else:
                print(f"  - 0x{u:08X} → 0x{v:08X}: VE={ve} (无效) [{desc}]")

        print(f"  r={r}: 有效估计值数 = {valid_count}")

    # ============================================================
    # 实验3: 复杂度对比
    # ============================================================
    print("\n" + "=" * 70)
    print("实验3: 复杂度对比")
    print("=" * 70)
    print(f"方式1 (精确计算): O(2^32) ≈ 4.3×10^9 次迭代")
    print(f"方式2 (逼近算法):")
    for r in [1, 2, 3, 4, 5]:
        # 对于单活跃S盒, 每轮最多16种输出
        # 对于双活跃S盒, 每轮最多256种输出
        max_entries_single = min(16**r, 50000)  # beam pruning
        max_entries_double = min(256**r, 50000)
        print(f"  r={r}: 单活跃≤{16**r}条, 双活跃≤{min(256**r, 50000)}条 (剪枝后)")
    print(f"  复杂度远低于 O(2^32)")

    print("\n实验完成!")

if __name__ == "__main__":
    main()
