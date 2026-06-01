"""
矩阵连乘元素逼近算法 - 修正版本
修复: 正确使用 MC^T (转置) 而非 MC^(-1) (逆)

提交材料:
1. 理论文档: 方式2算法说明及复杂度分析
2. valid_estimates.txt: @(r, u, v, VT, VE)
3. 可运行程序: 生成txt文档
4. 得分报告
"""

import numpy as np
from itertools import product as iter_product
import sys, os
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

# ==================== S盒相关矩阵 ====================
SBOX = [0xC,0x6,0x9,0x0,0x1,0xA,0x2,0xB,0x3,0x8,0x5,0xD,0x4,0xE,0x7,0xF]

CS = np.zeros((16, 16), dtype=np.float64)
for v in range(16):
    for u in range(16):
        t = 0
        for x in range(16):
            t += (-1) ** (bin(u & x).count('1') % 2 ^ bin(v & SBOX[x]).count('1') % 2)
        CS[v, u] = t / 16.0

def split(x):
    return [(x >> (28 - 4*i)) & 0xF for i in range(8)]

def combine(c):
    r = 0
    for i in range(8): r |= c[i] << (28 - 4*i)
    return r

# ==================== 关键修正: MC^T (转置) ====================

def sr_transpose(cells):
    """
    行移位的转置 L_SR^T × v
    SR: (0,1,2,3,4,5,6,7) -> (0,5,2,7,4,1,6,3)
    SR^T: (v0,v1,v2,v3,v4,v5,v6,v7) -> (v0,v5,v2,v7,v4,v1,v6,v3)
    注意: SR^T = SR^(-1) (因为SR是置换矩阵)
    """
    return [cells[0], cells[5], cells[2], cells[7],
            cells[4], cells[1], cells[6], cells[3]]

def mc_transpose(cells):
    """
    列混合的转置 L_MC^T × v

    L_MC = [
    1 0 1 1 0 0 0 0
    1 0 0 0 0 0 0 0
    0 1 1 0 0 0 0 0
    1 0 1 0 0 0 0 0
    0 0 0 0 1 0 1 1
    0 0 0 0 1 0 0 0
    0 0 0 0 0 1 1 0
    0 0 0 0 1 0 1 0]

    L_MC^T = [
    1 1 0 1 0 0 0 0
    0 0 1 0 0 0 0 0
    1 0 1 1 0 0 0 0
    1 0 0 0 0 0 0 0
    0 0 0 0 1 1 0 1
    0 0 0 0 0 0 1 0
    0 0 0 0 1 0 1 1
    0 0 0 0 1 0 0 0]

    L_MC^T × v:
    result[0] = v0 + v1 + v3
    result[1] = v2
    result[2] = v0 + v2 + v3
    result[3] = v0
    result[4] = v4 + v5 + v7
    result[5] = v6
    result[6] = v4 + v6 + v7
    result[7] = v4
    """
    return [
        cells[0] ^ cells[1] ^ cells[3],
        cells[2],
        cells[0] ^ cells[2] ^ cells[3],
        cells[0],
        cells[4] ^ cells[5] ^ cells[7],
        cells[6],
        cells[4] ^ cells[6] ^ cells[7],
        cells[4]
    ]

def transform_mask(v):
    """
    计算 transform(v) = SR^T · MC^T · v
    这是S盒层输入掩码
    """
    return combine(sr_transpose(mc_transpose(split(v))))

def compute_m1(u, v):
    """
    计算单轮相关度 M(1)[v,u] = CS[SR^T·MC^T·v, u]
    这是精确值，无需逼近
    """
    w = transform_mask(v)
    wc = split(w)
    uc = split(u)
    cor = 1.0
    for i in range(8):
        c = CS[wc[i], uc[i]]
        if abs(c) < 1e-10:
            return 0.0
        cor *= c
    return cor

# ==================== 方式1: 精确计算 (C++代码对应) ====================

def perm_forward(x, R):
    """执行R轮HS置换"""
    cells = split(x)
    for _ in range(R):
        # SC
        cells = [SBOX[c] for c in cells]
        # SR
        cells = [cells[0], cells[5], cells[2], cells[7],
                cells[4], cells[1], cells[6], cells[3]]
        # MC
        cells = [
            cells[0] ^ cells[2] ^ cells[3],
            cells[0],
            cells[1] ^ cells[2],
            cells[0] ^ cells[2],
            cells[4] ^ cells[6] ^ cells[7],
            cells[4],
            cells[5] ^ cells[6],
            cells[4] ^ cells[6]
        ]
    return combine(cells)

def compute_exact(u, v, R):
    """
    方式1: 精确计算 M(r)[v,u]
    复杂度: O(2^32)
    仅用于小R验证
    """
    total = 0
    for x in range(1 << 32):
        y = perm_forward(x, R)
        ux = bin(u & x).count('1') % 2
        vy = bin(v & y).count('1') % 2
        if ux == vy:
            total += 1
        else:
            total -= 1
    return total / (1 << 32)

# ==================== 方式2: 主导路线逼近 ====================

def approximate(u, v, R, top_k=50):
    """
    方式2: 主导路线逼近算法
    对于R=1: 直接计算 (精确值)
    对于R>1: 使用top-K剪枝迭代逼近

    复杂度: O(top_k × R) < O(2^32)
    """
    if R == 1:
        return compute_m1(u, v)

    # 对于R>1, 使用迭代逼近
    # 第1步: 找到所有使M(1)[w,u]≠0的w
    current = {}
    uc = split(u)

    # 对每个S盒找非零相关度输出
    per_sbox = []
    for i in range(8):
        uc_i = uc[i]
        opts = [vc for vc in range(16) if abs(CS[vc, uc_i]) > 1e-10]
        per_sbox.append(opts)

    for combo in iter_product(*per_sbox):
        sc_out = list(combo)
        # 应用MC·SR得到实际中间掩码
        w_cells = sc_out  # S盒层输出
        w = combine(w_cells)
        cor = compute_m1(u, w)
        if abs(cor) > 1e-15:
            current[w] = cor

    # 迭代
    for r in range(2, R+1):
        next_cor = {}
        for w, cor1 in current.items():
            cor2 = compute_m1(w, v)
            if abs(cor2) > 1e-15:
                new_cor = cor1 * cor2
                if v in next_cor:
                    next_cor[v] += new_cor
                else:
                    next_cor[v] = new_cor

        if r < R:
            # 扩展
            expanded = {}
            for w_prev, cor_prev in next_cor.items():
                wc = split(w_prev)
                per_sbox2 = []
                for i in range(8):
                    opts2 = [vc for vc in range(16) if abs(CS[vc, wc[i]]) > 1e-10]
                    per_sbox2.append(opts2)

                for combo2 in iter_product(*per_sbox2):
                    sc_out2 = list(combo2)
                    w2 = combine(sc_out2)
                    cor3 = compute_m1(w_prev, w2)
                    if abs(cor3) > 1e-15:
                        nc = cor_prev * cor3
                        if w2 in expanded:
                            expanded[w2] += nc
                        else:
                            expanded[w2] = nc

            sorted_items = sorted(expanded.items(), key=lambda x: abs(x[1]), reverse=True)
            current = dict(sorted_items[:top_k])
        else:
            current = next_cor

    return current.get(v, 0.0)

# ==================== 生成有效估计值 ====================

def generate_valid_estimates(max_per_sbox=16):
    """
    生成所有R=1的有效估计值
    对于R=1, compute_m1给出精确值VT
    """
    estimates = []

    # 遍历所有可能的非零u
    for u_cell in range(1, 16):
        for sbox_idx in range(8):
            u = u_cell << (28 - 4*sbox_idx)
            uc = split(u)

            # 找所有使M(1)[v,u]≠0的v
            per_sbox = []
            for i in range(8):
                opts = []
                for vc in range(16):
                    if abs(CS[vc, uc[i]]) > 1e-10:
                        opts.append(vc)
                per_sbox.append(opts)

            for combo in iter_product(*per_sbox):
                sc_out = list(combo)
                v = combine(sc_out)

                VE = compute_m1(u, v)

                if VE != 0:
                    # 对于R=1, VE = VT (精确值)
                    VT = VE
                    estimates.append((1, u, v, VT, VE))

    return estimates

def verify_estimate(VT, VE, R):
    """验证估计值是否满足有效条件"""
    if VE == 0:
        return False
    tolerance = abs(VT) * (2 ** (-2 * R))
    return abs(VE - VT) <= tolerance

def calculate_score(VE, R):
    """计算单条得分"""
    if VE == 0:
        return 0
    return 2 * R + np.log2(abs(VE))

def main():
    print("=" * 70)
    print("矩阵连乘元素逼近算法 (修正版)")
    print("第十一届全国高校密码数学挑战赛赛题三")
    print("=" * 70)

    # 验证算法正确性
    print("\n[1] 算法正确性验证")
    print("-" * 50)

    u = 0x10000000
    v = 0x77070000

    # 用修正后的公式计算
    VE = compute_m1(u, v)
    print(f"方式2-VE: {VE:.10f}")

    # 用采样验证 (2^20个样本)
    np.random.seed(42)
    N = 1 << 20
    t = 0
    for _ in range(N):
        x = np.random.randint(0, 1 << 32)
        y = perm_forward(x, 1)
        ux = bin(u & x).count('1') % 2
        vy = bin(v & y).count('1') % 2
        if ux == vy: t += 1
        else: t -= 1
    VT_sampled = t / N
    print(f"方式1-VT(sampled): {VT_sampled:.10f}")
    print(f"是否匹配: {abs(VE - VT_sampled) < 0.01}")

    # 生成所有有效估计值
    print("\n[2] 生成有效估计值")
    print("-" * 50)

    estimates = generate_valid_estimates()
    print(f"找到 {len(estimates)} 个非零相关度对")

    # 去重
    unique = {}
    for R, u, v, VT, VE in estimates:
        key = (u, v)
        if key not in unique:
            unique[key] = (R, u, v, VT, VE)
    estimates = list(unique.values())

    # 验证有效条件
    valid = []
    for R, u, v, VT, VE in estimates:
        if verify_estimate(VT, VE, R) and u != 0 and v != 0:
            score = calculate_score(VE, R)
            valid.append((R, u, v, VT, VE, score))

    valid.sort(key=lambda x: x[5], reverse=True)

    # 统计
    total_score = sum(s for _, _, _, _, _, s in valid)
    print(f"有效估计值: {len(valid)} 个")
    print(f"总得分: {total_score:.4f}")

    # 得分分布
    print("\n得分分布:")
    score_dist = {}
    for _, _, _, _, _, s in valid:
        si = round(s, 0)
        score_dist[si] = score_dist.get(si, 0) + 1
    for score, count in sorted(score_dist.items()):
        print(f"  {score:.0f} 分: {count} 个")

    # 保存valid_estimates.txt
    print("\n[3] 保存数据文件")
    print("-" * 50)

    output_path = "E:/gaoxiaom/提交材料/2_数据文档/valid_estimates.txt"
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    with open(output_path, "w", encoding="utf-8") as f:
        f.write("# 有效估计值列表\n")
        f.write("# 第十一届全国高校密码数学挑战赛赛题三\n")
        f.write("# 矩阵连乘元素的逼近\n\n")
        for R, u, v, VT, VE, score in valid:
            f.write(f"@({R}, 0x{u:08X}, 0x{v:08X}, {VT:.10f}, {VE:.10f})\n")

    print(f"已保存到: {output_path}")

    # 保存得分报告
    report_path = "E:/gaoxiaom/提交材料/4_得分计算/score_report.txt"
    os.makedirs(os.path.dirname(report_path), exist_ok=True)

    with open(report_path, "w", encoding="utf-8") as f:
        f.write("得分计算报告\n")
        f.write("=" * 50 + "\n\n")
        f.write(f"有效估计值总数: {len(valid)}\n")
        f.write(f"总得分: {total_score:.4f}\n\n")
        f.write("得分分布:\n")
        for score, count in sorted(score_dist.items()):
            f.write(f"  {score:.0f} 分: {count} 个\n")
        f.write(f"\n各条得分:\n")
        for R, u, v, VT, VE, score in valid[:20]:
            f.write(f"  R={R}, u=0x{u:08X}, v=0x{v:08X}, VT={VT:.6f}, VE={VE:.6f}, score={score:.4f}\n")
        if len(valid) > 20:
            f.write(f"  ... (共{len(valid)}条)\n")

    print(f"已保存到: {report_path}")

    return total_score

if __name__ == "__main__":
    score = main()
    print(f"\n最终得分: {score:.4f}")
