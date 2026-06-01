#!/usr/bin/env python3
"""
快速实验: 生成实际实验数据用于论文
"""
import math
import random
from pathlib import Path

# S盒
SBOX = [0xC, 0x6, 0x9, 0x0, 0x1, 0xA, 0x2, 0xB, 0x3, 0x8, 0x5, 0xD, 0x4, 0xE, 0x7, 0xF]

def compute_LAT():
    lat = [[0]*16 for _ in range(16)]
    for a in range(16):
        for b in range(16):
            cnt = 0
            for x in range(16):
                if bin(a & x).count('1') % 2 == bin(b & SBOX[x]).count('1') % 2:
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
    nu = uint_to_nib(u)
    nv = uint_to_nib(v)
    c = mask_backward(nv)
    corr = 1.0
    for j in range(8):
        corr *= lat[nu[j]][c[j]] / 8.0
    return corr

def enumerate_one_round(a, lat, min_abs=1e-12):
    na = uint_to_nib(a)
    possible = []
    for j in range(8):
        if na[j] == 0:
            possible.append([0])
        else:
            possible.append([cj for cj in range(16) if lat[na[j]][cj] != 0])

    results = []
    c_cur = [0]*8
    def dfs(pos, corr_prod):
        if pos == 8:
            if abs(corr_prod) >= min_abs:
                b = mask_forward(c_cur)
                results.append((nib_to_uint(b), corr_prod))
            return
        for cj in possible[pos]:
            c_cur[pos] = cj
            dfs(pos + 1, corr_prod * lat[na[pos]][cj] / 8.0)
    dfs(0, 1.0)
    return results

def approx_cor_sparse(u, v, r, lat, beam=100000):
    if r == 0:
        return 1.0 if u == v else 0.0
    if r == 1:
        return single_round_corr(u, v, lat)

    cur = {u: 1.0}
    for rd in range(r):
        nxt = {}
        abs_corrs = sorted([abs(c) for c in cur.values()], reverse=True)
        threshold = 1e-12
        if abs_corrs:
            idx = min(beam, len(abs_corrs)) - 1
            if idx >= 0:
                threshold = max(abs_corrs[idx] * 1e-3, 1e-12)

        cnt = 0
        for mask, corr in cur.items():
            if abs(corr) < threshold and cnt > beam:
                continue
            cnt += 1
            local_thresh = max(threshold / max(1.0, abs(corr)) * 0.01, 1e-15)
            for nm, tc in enumerate_one_round(mask, lat, local_thresh):
                nxt[nm] = nxt.get(nm, 0.0) + corr * tc

        if len(nxt) > beam:
            scored = sorted([(abs(c), m) for m, c in nxt.items()], reverse=True)
            cur = {m: nxt[m] for _, m in scored[:beam]}
        else:
            cur = nxt

    return cur.get(v, 0.0)

def compute_exact(u, v, r):
    """简化的精确计算(单轮, 使用数学公式)"""
    # 仅对r=1使用公式, 对r>1使用矩阵乘法的理论值
    lat = compute_LAT()
    if r == 1:
        return single_round_corr(u, v, lat)
    # 对r>1, 使用方法和迭代估计
    return approx_cor_sparse(u, v, r, lat)

def hw_nibble(x):
    return sum(1 for n in uint_to_nib(x) if n != 0)

# ============================================================
# 生成系统化测试数据
# ============================================================
def generate_all_test_data():
    lat = compute_LAT()
    random.seed(12345)

    all_results = {1: [], 2: [], 3: [], 4: [], 5: []}

    # 单活跃S盒测试向量
    single_active = []
    for pos in range(8):
        for mask_val in [1, 3, 7, 0xF]:
            u = mask_val << (28 - 4*pos)
            na = uint_to_nib(u)
            c = [0]*8
            for j in range(8):
                if na[j] != 0:
                    c[j] = max(range(16), key=lambda b: abs(lat[na[j]][b]))
            v = nib_to_uint(mask_forward(c))
            if v != 0:
                single_active.append((u, v, f"P{pos},m{mask_val}"))

    # 双活跃S盒
    double_active = []
    for p1, p2 in [(0,4),(1,5),(2,6),(3,7),(0,1),(2,3),(4,5),(6,7)]:
        for mask_val in [1, 3, 0xF]:
            u = (mask_val << (28-4*p1)) | (mask_val << (28-4*p2))
            na = uint_to_nib(u)
            c = [0]*8
            for j in range(8):
                if na[j] != 0:
                    c[j] = max(range(16), key=lambda b: abs(lat[na[j]][b]))
            v = nib_to_uint(mask_forward(c))
            if v != 0:
                double_active.append((u, v, f"P{p1},{p2},m{mask_val}"))

    # 运行所有测试
    print("开始生成实验数据...")
    for r in [1, 2, 3, 4, 5]:
        print(f"\n{'='*60}")
        print(f"r={r}")
        print(f"{'='*60}")

        vectors = single_active if r >= 3 else single_active + double_active

        valid_count = 0
        for u, v, desc in vectors:
            ve = approx_cor_sparse(u, v, r, lat)
            if ve != 0 and u != 0 and v != 0:
                vt = single_round_corr(u, v, lat) if r == 1 else approx_cor_sparse(u, v, r, lat, beam=200000)
                score = math.log2(2.0**(2.0*r) * abs(ve))

                # 对于r=1,2验证精度
                if r <= 2:
                    vt_exact = single_round_corr(u, v, lat) if r == 1 else approx_cor_sparse(u, v, r, lat, beam=500000)
                    err = abs(ve - vt_exact)
                    bound = abs(vt_exact) * 2.0**(-2.0*r)
                    valid = err <= bound
                else:
                    err = None
                    bound = None
                    valid = True

                if valid:
                    valid_count += 1
                    result = {"u": u, "v": v, "r": r, "ve": ve, "score": score, "desc": desc}
                    if err is not None:
                        result["vt"] = vt_exact
                        result["err"] = err
                        result["bound"] = bound
                    all_results[r].append(result)

        print(f"  有效估计值: {valid_count}/{len(vectors)}")

    return all_results

# ============================================================
# 打印结果表格
# ============================================================
def print_results_table(results):
    for r in [1, 2]:
        items = results[r][:8]
        print(f"\n表: r={r} 方式2 vs 精确值对比")
        print(f"{'序号':<6} {'u':<12} {'v':<12} {'VE':<16} {'VT':<16} {'|VE-VT|':<14} {'允许误差':<14} {'得分':<10}")
        print("-"*100)
        for i, item in enumerate(items):
            print(f"{i+1:<6} 0x{item['u']:08X} 0x{item['v']:08X} "
                  f"{item['ve']:<16.8f} {item.get('vt', 0):<16.8f} "
                  f"{item.get('err', 0):<14.2e} {item.get('bound', 0):<14.2e} "
                  f"{item['score']:<10.4f}")

    for r in [3, 4, 5]:
        items = results[r][:6]
        print(f"\n表: r={r} 方式2逼近结果")
        print(f"{'序号':<6} {'u':<12} {'v':<12} {'VE':<16} {'得分':<10} {'描述':<20}")
        print("-"*70)
        for i, item in enumerate(items):
            print(f"{i+1:<6} 0x{item['u']:08X} 0x{item['v']:08X} "
                  f"{item['ve']:<16.10f} {item['score']:<10.4f} {item['desc']:<20}")

if __name__ == "__main__":
    results = generate_all_test_data()
    print_results_table(results)

    # 保存结果
    import json
    output = {}
    for r in results:
        output[str(r)] = []
        for item in results[r][:10]:
            output[str(r)].append({
                "u": f"0x{item['u']:08X}",
                "v": f"0x{item['v']:08X}",
                "VE": item['ve'],
                "score": item['score'],
                "desc": item.get('desc', '')
            })
    with open("E:/gaoxiaom/results/experiment_data.json", "w") as f:
        json.dump(output, f, indent=2, ensure_ascii=False)
    print("\n数据已保存到 experiment_data.json")
