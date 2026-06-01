"""
方式2: 矩阵连乘元素逼近算法
第十一届全国高校密码数学挑战赛赛题三

算法原理:
  对于R=1: M(1)[v,u] = Π_i CS[(SR^T·MC^T·v)_i, u_i]  (精确值)
  对于R>1: 使用主导路线法逼近

复杂度: O(K×R) << O(2^32)
"""

import numpy as np
from itertools import product as iter_product
import sys, os
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

# ==================== 密码组件 ====================
SBOX = [0xC,0x6,0x9,0x0,0x1,0xA,0x2,0xB,0x3,0x8,0x5,0xD,0x4,0xE,0x7,0xF]

# S盒相关矩阵
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

# MC^T (转置, 非逆)
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

# SR^T (对称矩阵)
def srT(c):
    return [c[0],c[5],c[2],c[7],c[4],c[1],c[6],c[3]]

def transform(v):
    """计算 SR^T·MC^T·v (S盒层输入掩码)"""
    return combine(srT(mcT(split(v))))

# ==================== 方式2: 逼近算法 ====================

def method2_R1(u, v):
    """
    R=1时的精确值 (也是方式2的输出)
    M(1)[v,u] = Π_i CS[(SR^T·MC^T·v)_i, u_i]
    复杂度: O(8×16) = O(1) << O(2^32)
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

def method2(u, v, R):
    """
    对任意R的方式2算法
    R=1: 精确公式
    R>1: 主导路线逼近
    """
    if R == 1:
        return method2_R1(u, v)

    # 对于R>1: 主导路线法
    # 找到所有使M(1)[w,u]≠0的w
    current = {}
    uc = split(u)

    per_sbox = []
    for i in range(8):
        opts = [vc for vc in range(16) if abs(CS[vc, uc[i]]) > 1e-10]
        per_sbox.append(opts)

    for combo in iter_product(*per_sbox):
        w_cells = list(combo)
        w = combine(w_cells)
        cor = method2_R1(u, w)
        if abs(cor) > 1e-15:
            current[w] = cor

    # 迭代
    for r in range(2, R+1):
        next_cor = {}
        for w, cor1 in current.items():
            cor2 = method2_R1(w, v)
            if abs(cor2) > 1e-15:
                new_cor = cor1 * cor2
                if v in next_cor:
                    next_cor[v] += new_cor
                else:
                    next_cor[v] = new_cor

        if r < R:
            expanded = {}
            for wp, cp in next_cor.items():
                wc = split(wp)
                per_sbox2 = []
                for i in range(8):
                    opts2 = [vc for vc in range(16) if abs(CS[vc, wc[i]]) > 1e-10]
                    per_sbox2.append(opts2)
                for combo2 in iter_product(*per_sbox2):
                    w2 = combine(list(combo2))
                    cor3 = method2_R1(wp, w2)
                    if abs(cor3) > 1e-15:
                        nc = cp * cor3
                        expanded[w2] = expanded.get(w2, 0) + nc
            sorted_items = sorted(expanded.items(), key=lambda x: abs(x[1]), reverse=True)
            current = dict(sorted_items[:50])
        else:
            current = next_cor

    return current.get(v, 0.0)

# ==================== 生成所有有效估计值 ====================

def generate_R1_estimates():
    """生成R=1的所有有效估计值"""
    valid = []

    for u_cell in range(1, 16):
        for sbox_idx in range(8):
            u = u_cell << (28 - 4*sbox_idx)
            uc = split(u)

            # 找所有使M(1)[v,u]≠0的v
            per_sbox = []
            for i in range(8):
                opts = [vc for vc in range(16) if abs(CS[vc, uc[i]]) > 1e-10]
                per_sbox.append(opts)

            for combo in iter_product(*per_sbox):
                v = combine(list(combo))
                VE = method2_R1(u, v)
                if VE != 0:
                    VT = VE  # R=1时精确值
                    valid.append((1, u, v, VT, VE))

    # 去重
    unique = {}
    for R, u, v, VT, VE in valid:
        key = (R, u, v)
        if key not in unique:
            unique[key] = (R, u, v, VT, VE)
    return list(unique.values())

def score(R, VE):
    """单条得分"""
    return 2*R + np.log2(abs(VE))

def main():
    print("="*70)
    print("方式2: 矩阵连乘元素逼近算法")
    print("第十一届全国高校密码数学挑战赛赛题三")
    print("="*70)

    # R=1: 找到所有有效估计值
    estimates = generate_R1_estimates()

    # 过滤: 得分>0的
    valid = []
    total_score = 0
    for R, u, v, VT, VE in estimates:
        s = score(R, VE)
        if s > 0:  # 只保留正得分
            valid.append((R, u, v, VT, VE, s))
            total_score += s

    valid.sort(key=lambda x: x[5], reverse=True)
    print(f"\n正得分有效估计值: {len(valid)}条")
    print(f"总得分: {total_score:.4f}")

    # 保存txt文件
    outdir = "E:/gaoxiaom/提交材料/数据文档"
    os.makedirs(outdir, exist_ok=True)
    txt_path = os.path.join(outdir, "valid_estimates.txt")
    with open(txt_path, "w", encoding="utf-8") as f:
        f.write("# 有效估计值\n")
        f.write("# 第十一届全国高校密码数学挑战赛赛题三\n\n")
        for R, u, v, VT, VE, s in valid:
            f.write(f"@({R}, 0x{u:08X}, 0x{v:08X}, {VT:.10f}, {VE:.10f})\n")
    print(f"已保存: {txt_path}")

    # 保存得分
    score_path = "E:/gaoxiaom/提交材料/得分报告/team_score.txt"
    os.makedirs(os.path.dirname(score_path), exist_ok=True)
    with open(score_path, "w", encoding="utf-8") as f:
        f.write(f"队伍得分: {total_score:.4f}\n")
        f.write(f"有效估计值数: {len(valid)}\n")
    print(f"已保存: {score_path}")

    return valid, total_score

if __name__ == "__main__":
    valid, total = main()
