#!/usr/bin/env python3
"""
最终得分计算脚本
赛题三: 矩阵连乘元素的逼近 - 第十一届(2026年)全国高校密码数学挑战赛

评分公式: score = log2(2^(2r) * |VE|) = log2(4^r * |VE|)
"""
import math

# ============================================================
# 所有有效估计值 (均已通过C++算法验证)
# ============================================================
ALL_DATA = [
    # r=1: 6个有效值 (VE=VT, 误差=0)
    (1, '0x10000000', '0x07000000', -0.5, -0.5, 0.0),
    (1, '0x10000000', '0x08000000',  0.5,  0.5, 0.0),
    (1, '0x10000000', '0x01000000', -0.25,-0.25,0.0),
    (1, '0x00000001', '0x10010000', -0.25,-0.25,0.0),
    (1, '0x00000003', '0x30030000',  0.5,  0.5, 0.0),
    (1, '0x0000000F', '0xF00F0000',  0.25, 0.25,0.0),

    # r=2: 8个高分有效值 (VE=VT, 误差=0)
    (2, '0x20000000', '0x00000888',  1.0,   1.0,  0.0),
    (2, '0x00002000', '0x08880000',  1.0,   1.0,  0.0),
    (2, '0x70000000', '0x00000DDD',  0.75,  0.75, 0.0),
    (2, '0x60000000', '0x00000999',  0.75,  0.75, 0.0),
    (2, '0x50000000', '0x00000555',  0.75,  0.75, 0.0),
    (2, '0x40000000', '0x00000111',  0.75,  0.75, 0.0),
    (2, '0x10000000', '0x00000CCC', -0.5,  -0.5,  0.0),
    (2, '0x10000000', '0x00000444',  0.5,   0.5,  0.0),

    # r=3: 8个完整转移枚举验证值
    (3, '0x20000000', '0xEEE00E0E',  0.125,      None, None),
    (3, '0x20000000', '0x44400404',  0.125,      None, None),
    (3, '0x20000000', '0xE44A0404',  0.125,      None, None),
    (3, '0x20000000', '0x44400E0E',  0.125,      None, None),
    (3, '0x20000000', '0xE44A0E0E',  0.125,      None, None),
    (3, '0x20000000', '0x4EEA0404',  0.125,      None, None),
    (3, '0x40000000', '0x88800808',  0.125,      None, None),
    (3, '0x10000000', '0xA2280202', -0.1015625,  None, None),

    # r=4: 4个有效值 (来自C++枚举，计算时间~100s/对)
    (4, '0x10000000', '0x3B3A0222',  0.0115966796875, None, None),
    (4, '0x10000000', '0x33320AAA',  0.0115966796875, None, None),
    (4, '0x10000000', '0x3B320222',  0.0115966796875, None, None),
    (4, '0x10000000', '0x2A2A0222',  0.0076904296875, None, None),
]

# ============================================================
# 验证函数
# ============================================================
def check_validity(ve, vt, u, v, r):
    """检查是否满足有效估计值条件"""
    if ve == 0: return False, "VE=0"
    if u == 0:  return False, "u=0"
    if v == 0:  return False, "v=0"
    if vt is not None:
        bound = abs(vt) * math.pow(2.0, -2.0 * r)
        if abs(ve - vt) > bound:
            return False, f"|VE-VT|={abs(ve-vt):.4e} > bound={bound:.4e}"
    return True, "OK"

def compute_score(ve, r):
    """计算单条得分"""
    return math.log2(pow(4.0, r) * abs(ve))

# ============================================================
# 生成完整输出
# ============================================================
print("=" * 95)
print("第十一届(2026年)全国高校密码数学挑战赛 - 赛题三")
print("矩阵连乘元素的逼近 - 有效估计值得分计算")
print("=" * 95)
print()
print("评分公式: score = log2(2^(2r) * |VE|) = log2(4^r * |VE|)")
print("有效条件: |VE-VT| <= |VT| * 2^(-2r), VE != 0, u != 0, v != 0")
print()

for r in sorted(set(d[0] for d in ALL_DATA)):
    items = [d for d in ALL_DATA if d[0] == r]
    has_exact = any(d[4] is not None for d in items)

    print(f"{'='*95}")
    print(f"r = {r}  ({len(items)} valid estimates)")
    print(f"{'='*95}")

    if has_exact:
        print(f"{'No':>3}  {'u':>12}  {'v':>12}  {'VE':>18}  {'VT':>18}  {'|VE-VT|':>12}  {'允许误差':>14}  {'4^r*|VE|':>14}  {'Score':>8}")
        print("-" * 95)
        for i, (rr, u, v, ve, vt, err) in enumerate(items, 1):
            if vt is not None:
                bound = abs(vt) * pow(2.0, -2.0*r)
                actual_err = abs(ve - vt)
                valid, reason = check_validity(ve, vt, int(u, 16), int(v, 16), r)
                prod = 4.0**r * abs(ve)
                score = compute_score(ve, r)
                print(f"{i:3d}  {u:>12}  {v:>12}  {ve:18.15f}  {vt:18.15f}  {actual_err:12.4e}  {bound:14.4e}  {prod:14.8f}  {score:8.4f}  {'OK' if valid else reason}")
            else:
                valid, reason = check_validity(ve, None, int(u, 16), int(v, 16), r)
                prod = 4.0**r * abs(ve)
                score = compute_score(ve, r)
                print(f"{i:3d}  {u:>12}  {v:>12}  {ve:18.15f}  {'N/A':>18}  {'N/A':>12}  {'N/A':>14}  {prod:14.8f}  {score:8.4f}  {reason}")
    else:
        print(f"{'No':>3}  {'u':>12}  {'v':>12}  {'VE':>18}  {'4^r*|VE|':>14}  {'Score':>8}  {'Valid?'}")
        print("-" * 78)
        for i, (rr, u, v, ve, vt, err) in enumerate(items, 1):
            valid, reason = check_validity(ve, None, int(u, 16), int(v, 16), r)
            prod = 4.0**r * abs(ve)
            score = compute_score(ve, r)
            print(f"{i:3d}  {u:>12}  {v:>12}  {ve:18.15f}  {prod:14.8f}  {score:8.4f}  {reason}")

# ============================================================
# 统计汇总
# ============================================================
print()
print("=" * 95)
print("统计汇总")
print("=" * 95)

for r in sorted(set(d[0] for d in ALL_DATA)):
    scores = [compute_score(ve, r) for rr, _, _, ve, _, _ in ALL_DATA if rr == r]
    print(f"r={r}: 有效值={len(scores):2d}, 最高得分={max(scores):8.4f}, 最低得分={min(scores):8.4f}, 平均得分={sum(scores)/len(scores):8.4f}")

all_scores = [compute_score(ve, r) for r, _, _, ve, _, _ in ALL_DATA]
print(f"\n总计: {len(all_scores)} 个有效估计值")
print(f"  总分: {sum(all_scores):.4f}")
print(f"  最高得分: {max(all_scores):.4f}")
print(f"  最低得分: {min(all_scores):.4f}")
print(f"  平均得分: {sum(all_scores)/len(all_scores):.4f}")

# 保存到文件
with open("E:/gaoxiaom/results/scores.txt", "w", encoding="utf-8") as f:
    f.write("赛题三: 矩阵连乘元素的逼近 - 得分计算结果\n")
    f.write("评分公式: score = log2(2^(2r) * |VE|)\n\n")
    for r in sorted(set(d[0] for d in ALL_DATA)):
        items = [d for d in ALL_DATA if d[0] == r]
        f.write(f"\n{'='*80}\n")
        f.write(f"r = {r} ({len(items)} valid estimates)\n")
        f.write(f"{'='*80}\n")
        for i, (rr, u, v, ve, vt, err) in enumerate(items, 1):
            score = compute_score(ve, r)
            f.write(f"  {i:2d}. u={u}, v={v}, VE={ve:.15f}, score={score:.4f}\n")
    f.write(f"\n{'='*80}\n")
    f.write("SUMMARY\n")
    f.write(f"Total valid estimates: {len(all_scores)}\n")
    f.write(f"Total score: {sum(all_scores):.4f}\n")
    f.write(f"Max score: {max(all_scores):.4f}\n")
    f.write(f"Min score: {min(all_scores):.4f}\n")
    f.write(f"Average score: {sum(all_scores)/len(all_scores):.4f}\n")

print("\n得分已保存至 E:/gaoxiaom/results/scores.txt")
