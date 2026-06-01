"""
方式2: 矩阵连乘元素逼近算法 (修正版)
第十一届全国高校密码数学挑战赛赛题三

公式: M(1)[v,u] = Π_i CS[(SR^T·MC^T·v)_i, u_i]

对R=1, 这是精确值 (=VT)
"""

import numpy as np
from itertools import product as iter_product
import sys, os
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

SBOX = [0xC,0x6,0x9,0x0,0x1,0xA,0x2,0xB,0x3,0x8,0x5,0xD,0x4,0xE,0x7,0xF]

CS = np.zeros((16, 16), dtype=np.float64)
for v in range(16):
    for u in range(16):
        t = 0
        for x in range(16):
            t += (-1) ** (bin(u&x).count('1')%2 ^ bin(v&SBOX[x]).count('1')%2)
        CS[v, u] = t / 16.0

def split(x):
    return [(x>>(28-4*i))&0xF for i in range(8)]
def combine(c):
    r = 0
    for i in range(8): r |= c[i] << (28-4*i)
    return r

# MC^T (转置矩阵)
def mcT(c):
    return [
        c[0]^c[1]^c[3],  # v0+v1+v3
        c[2],             # v2
        c[0]^c[2]^c[3],  # v0+v2+v3
        c[0],             # v0
        c[4]^c[5]^c[7],  # v4+v5+v7
        c[6],             # v6
        c[4]^c[6]^c[7],  # v4+v6+v7
        c[4]              # v4
    ]

# (MC^T)^{-1}: 逆矩阵
def mcT_inv(c):
    return [
        c[3],                         # v0
        c[0]^c[1]^c[2]^c[3],         # v1
        c[1],                         # v2
        c[1]^c[2]^c[3],               # v3
        c[7],                         # v4
        c[4]^c[5]^c[6]^c[7],         # v5
        c[5],                         # v6
        c[5]^c[6]^c[7]                # v7
    ]

# SR^T (= SR, 对称置换矩阵)
def srT(c):
    return [c[0],c[5],c[2],c[7],c[4],c[1],c[6],c[3]]

def transform(v):
    """SR^T·MC^T·v (S盒层输出掩码)"""
    return combine(srT(mcT(split(v))))

def transform_inv(w):
    """逆变换: 给定S盒输出w, 计算原始输出掩码v = (MC^T)^(-1)·SR·w"""
    return combine(mcT_inv(srT(split(w))))

def method2_R1(u, v):
    """
    R=1相关度计算 (精确值 = VT)
    复杂度: O(1)
    """
    w = transform(v)
    wc = split(w)
    uc = split(u)
    cor = 1.0
    for i in range(8):
        c = CS[wc[i], uc[i]]
        if abs(c) < 1e-10:
            return 0.0
        cor *= c
    return cor

def generate_R1_estimates():
    """生成R=1的所有有效估计值"""
    valid = []

    for u_cell in range(1, 16):
        for sbox_idx in range(8):
            u = u_cell << (28 - 4*sbox_idx)
            uc = split(u)

            # S盒输出掩码w必须: CS[w_i, u_i] ≠ 0 for all i
            per_sbox_opts = []
            for i in range(8):
                opts = [vc for vc in range(16) if abs(CS[vc, uc[i]]) > 1e-10]
                per_sbox_opts.append(opts)

            for combo in iter_product(*per_sbox_opts):
                w = combine(list(combo))  # S盒输出掩码
                # 反推实际输出掩码v
                v = transform_inv(w)
                VE = method2_R1(u, v)
                if VE != 0 and u != 0 and v != 0:
                    VT = VE  # R=1时VE=VT
                    valid.append((1, u, v, VT, VE))

    # 去重
    seen = {}
    for R, u, v, VT, VE in valid:
        key = (R, u, v)
        if key not in seen:
            seen[key] = (R, u, v, VT, VE)
    return list(seen.values())

def calculate_score(R, VE):
    return 2*R + np.log2(abs(VE))

def main():
    print("="*70)
    print("方式2: 矩阵连乘元素逼近算法 (修正版)")
    print("第十一届全国高校密码数学挑战赛赛题三")
    print("="*70)

    estimates = generate_R1_estimates()
    print(f"\n非零相关度对: {len(estimates)}对")

    # 计算得分
    all_valid = []
    score_dist = {}
    for R, u, v, VT, VE in estimates:
        s = calculate_score(R, VE)
        all_valid.append((R, u, v, VT, VE, s))
        si = round(s)
        score_dist[si] = score_dist.get(si, 0) + 1

    all_valid.sort(key=lambda x: x[5], reverse=True)

    # 得分>0的
    positive = [e for e in all_valid if e[5] > 0]
    total = sum(e[5] for e in all_valid)
    total_pos = sum(e[5] for e in positive)

    print(f"\n得分分布 (全部):")
    for s, n in sorted(score_dist.items()):
        print(f"  {s:3d}分: {n:4d}个")

    print(f"\n全部估计值: {len(all_valid)}个, 总得分: {total:.4f}")
    print(f"正得分估计值: {len(positive)}个, 总得分: {total_pos:.4f}")

    # Top-10
    print("\nTop-10得分:")
    for R, u, v, VT, VE, s in all_valid[:10]:
        print(f"  R={R}, u=0x{u:08X}, v=0x{v:08X}, |VE|={abs(VE):.4f}, score={s:.2f}")

    # 保存valid_estimates.txt
    outdir = "E:/gaoxiaom/提交材料/数据文档"
    os.makedirs(outdir, exist_ok=True)
    txt = os.path.join(outdir, "valid_estimates.txt")
    with open(txt, "w", encoding="utf-8") as f:
        f.write("# 有效估计值列表\n")
        f.write("# 第十一届全国高校密码数学挑战赛赛题三\n\n")
        for R, u, v, VT, VE, s in all_valid:
            f.write(f"@({R}, 0x{u:08X}, 0x{v:08X}, {VT:.10f}, {VE:.10f})\n")
    print(f"\n数据已保存: {txt}")

    # 保存得分报告
    scoredir = "E:/gaoxiaom/提交材料/得分报告"
    os.makedirs(scoredir, exist_ok=True)
    with open(os.path.join(scoredir, "team_score.txt"), "w", encoding="utf-8") as f:
        f.write(f"队伍总得分: {total:.4f}\n")
        f.write(f"有效估计值总数: {len(all_valid)}\n")
        f.write(f"正得分估计值: {len(positive)}个, 正得分总计: {total_pos:.4f}\n\n")
        f.write("得分分布:\n")
        for s, n in sorted(score_dist.items()):
            f.write(f"  {s}分: {n}个\n")
    print(f"得分已保存: {scoredir}/team_score.txt")

    return all_valid, total

if __name__ == "__main__":
    valid, total = main()
