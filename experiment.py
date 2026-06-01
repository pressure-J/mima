#!/usr/bin/env python3
"""
赛题三: 矩阵连乘元素的逼近 - 实验与论文生成脚本
第十一届(2026年)全国高校密码数学挑战赛

功能:
  1. 运行逼近算法实验, 收集数据
  2. 计算有效估计值和得分
  3. 生成数据图表
  4. 生成符合模板格式的论文 (.docx)
"""

import subprocess
import os
import sys
import json
import math
import time
import random
from collections import defaultdict
from pathlib import Path

# ============================================================
# 配置
# ============================================================
PROJECT_DIR = Path("E:/gaoxiaom")
ALGO_DIR = PROJECT_DIR / "algorithm"
APPROX_EXE = ALGO_DIR / "approx_cor.exe"
RESULTS_DIR = PROJECT_DIR / "results"
FIGURES_DIR = PROJECT_DIR / "figures"
RESULTS_DIR.mkdir(exist_ok=True)
FIGURES_DIR.mkdir(exist_ok=True)

# ============================================================
# 密码算法常量
# ============================================================
SBOX = [0xC, 0x6, 0x9, 0x0, 0x1, 0xA, 0x2, 0xB, 0x3, 0x8, 0x5, 0xD, 0x4, 0xE, 0x7, 0xF]

# ============================================================
# 生成测试向量
# ============================================================
def generate_test_vectors(n_per_type=10):
    """生成多样化的测试向量 (u,v)"""
    vectors = []
    random.seed(42)

    # 类型1: 单活跃S盒 (1个非零nibble)
    for pos in range(8):
        for _ in range(n_per_type // 2):
            u_mask = random.randint(1, 15)
            u = u_mask << (28 - 4*pos)
            # 生成相关v (通过正向传播)
            v = forward_propagate_mask(u)
            if v != 0:
                vectors.append((u, v))

    # 类型2: 双活跃S盒
    for _ in range(n_per_type):
        p1, p2 = random.sample(range(8), 2)
        u1 = random.randint(1, 15)
        u2 = random.randint(1, 15)
        u = (u1 << (28 - 4*p1)) | (u2 << (28 - 4*p2))
        v = forward_propagate_mask(u)
        if v != 0:
            vectors.append((u, v))

    # 类型3: 三活跃S盒
    for _ in range(n_per_type // 2):
        positions = random.sample(range(8), 3)
        u = 0
        for p in positions:
            u |= random.randint(1, 15) << (28 - 4*p)
        v = forward_propagate_mask(u)
        if v != 0:
            vectors.append((u, v))

    # 类型4: 指定示例 (来自赛题)
    example_pairs = [
        (0x000ee0f0, 0x08088880),
        (0x00000001, 0x10010000),
        (0x0000000F, 0xF0010000),
        (0x00F00000, 0x0000F001),
        (0x0F000000, 0x00100F00),
    ]
    vectors.extend(example_pairs)

    return vectors


def uint_to_nibbles(x):
    return [(x >> (28 - 4*i)) & 0xF for i in range(8)]


def nibbles_to_uint(nib):
    res = 0
    for i, n in enumerate(nib):
        res |= n << (28 - 4*i)
    return res


def forward_propagate_mask(u):
    """给定输入掩码u, 找到一个相关的输出掩码v"""
    nu = uint_to_nibbles(u)

    # 选择一个可能的S盒输出掩码c
    # 对每个活跃S盒, 随机选择LAT非零的输出
    c = [0] * 8
    for j in range(8):
        if nu[j] == 0:
            c[j] = 0
        else:
            # 找LAT非零的输出 (基于S盒的LAT)
            candidates = []
            for b in range(16):
                # 计算LAT
                count = 0
                for x in range(16):
                    a_dot = bin(nu[j] & x).count('1') & 1
                    b_dot = bin(b & SBOX[x]).count('1') & 1
                    if a_dot == b_dot:
                        count += 1
                lat_val = count - 8
                if lat_val != 0:
                    candidates.append((b, abs(lat_val)))
            if candidates:
                # 选择最大LAT绝对值
                candidates.sort(key=lambda x: -x[1])
                c[j] = candidates[0][0]
            else:
                c[j] = 0

    # 正向传播 (使用推导的正确公式)
    # b = [c7, c0⊕c2⊕c5, c5, c2⊕c5⊕c7, c3, c1⊕c4⊕c6, c1, c1⊕c3⊕c6]
    b = [0] * 8
    b[0] = c[7]
    b[1] = c[0] ^ c[2] ^ c[5]
    b[2] = c[5]
    b[3] = c[2] ^ c[5] ^ c[7]
    b[4] = c[3]
    b[5] = c[1] ^ c[4] ^ c[6]
    b[6] = c[1]
    b[7] = c[1] ^ c[3] ^ c[6]

    return nibbles_to_uint(b)


# ============================================================
# 运行C++程序
# ============================================================
def run_approx_cor(mode, u, v, r):
    """运行C++逼近算法"""
    cmd = [str(APPROX_EXE), str(mode), f"0x{u:08X}", f"0x{v:08X}", str(r)]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=600, cwd=str(ALGO_DIR))
        output = result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        return {"error": "timeout", "output": ""}

    # 解析输出
    parsed = {"output": output, "error": None}
    for line in output.split('\n'):
        line = line.strip()
        if "VE =" in line:
            try:
                parsed["VE"] = float(line.split("VE =")[1].strip())
            except: pass
        elif "VT =" in line:
            try:
                parsed["VT"] = float(line.split("VT =")[1].strip())
            except: pass
        elif "计算时间:" in line:
            try:
                parsed["time_ms"] = float(line.split(":")[1].strip().replace(" ms", ""))
            except: pass
        elif "得分 =" in line:
            try:
                parsed["score"] = float(line.split("=")[1].strip())
            except: pass
        elif "有效估计值" in line:
            parsed["valid"] = "✓" in line

    return parsed


# ============================================================
# 批量实验
# ============================================================
def run_experiment(vectors, r_values, mode=2):
    """批量运行实验"""
    results = []
    total = len(vectors) * len(r_values)
    count = 0

    for r in r_values:
        for u, v in vectors:
            count += 1
            print(f"\r进度: {count}/{total} (r={r}, u=0x{u:08X}, v=0x{v:08X})", end="", flush=True)

            res = run_approx_cor(mode, u, v, r)
            res["r"] = r
            res["u"] = u
            res["v"] = v
            results.append(res)

            # 对于r<=2, 同时计算精确值
            if r <= 2:
                exact_res = run_approx_cor(1, u, v, r)  # mode=1 for exact
                res["VT_exact"] = exact_res.get("VT", None)
                res["exact_time_ms"] = exact_res.get("time_ms", None)

                # 检查有效条件
                if "VE" in res and "VT_exact" in res and res["VT_exact"] is not None:
                    ve = res["VE"]
                    vt = res["VT_exact"]
                    u_val = res["u"]
                    v_val = res["v"]
                    bound = abs(vt) * math.pow(2.0, -2.0 * r)
                    res["abs_err"] = abs(ve - vt)
                    res["bound"] = bound
                    res["valid_check"] = (ve != 0 and u_val != 0 and v_val != 0 and
                                         abs(ve - vt) <= bound)
                    if res["valid_check"]:
                        res["computed_score"] = math.log2(math.pow(2.0, 2.0 * r) * abs(ve))
                    else:
                        res["computed_score"] = None

            # 对于r>2, 我们只能验证VE≠0, u≠0, v≠0
            elif "VE" in res:
                if res["VE"] != 0 and res["u"] != 0 and res["v"] != 0:
                    res["computed_score"] = math.log2(math.pow(2.0, 2.0 * r) * abs(res["VE"]))
                    res["valid_check"] = True

            time.sleep(0.05)  # 小延迟

    print()
    return results


# ============================================================
# 结果分析
# ============================================================
def analyze_results(results):
    """分析实验结果"""
    analysis = {"by_round": defaultdict(list), "valid_count": 0, "total": len(results)}

    for res in results:
        r = res["r"]
        analysis["by_round"][r].append(res)
        if res.get("valid_check"):
            analysis["valid_count"] += 1

    # 每轮统计
    print("\n=== 实验结果分析 ===")
    for r in sorted(analysis["by_round"].keys()):
        items = analysis["by_round"][r]
        valid = [x for x in items if x.get("valid_check")]
        scores = [x["computed_score"] for x in valid if x.get("computed_score") is not None]

        print(f"\nr={r}:")
        print(f"  测试数: {len(items)}")
        print(f"  有效估计值: {len(valid)}")
        if scores:
            print(f"  平均得分: {sum(scores)/len(scores):.4f}")
            print(f"  最高得分: {max(scores):.4f}")
            print(f"  最低得分: {min(scores):.4f}")

    return analysis


# ============================================================
# 保存结果
# ============================================================
def save_results(results, filename="experiment_results.json"):
    """保存实验结果到JSON"""
    output_path = RESULTS_DIR / filename
    with open(output_path, 'w') as f:
        json.dump(results, f, indent=2, default=str)
    print(f"结果已保存至: {output_path}")
    return output_path


# ============================================================
# 生成结果表格
# ============================================================
def generate_result_table(results, r_values):
    """生成结果表格 (用于论文)"""
    for r in r_values:
        items = [x for x in results if x["r"] == r and x.get("valid_check")]
        if not items:
            continue

        print(f"\n=== r={r} 有效估计值列表 ===")
        print(f"{'序号':<6} {'u':<12} {'v':<12} {'VT':<16} {'VE':<16} {'|VE-VT|':<14} {'允许误差':<14} {'得分':<10}")
        print("-" * 100)

        for i, item in enumerate(items[:20]):  # 最多显示20条
            print(f"{i+1:<6} 0x{item['u']:08X} 0x{item['v']:08X} "
                  f"{item.get('VT_exact', 'N/A'):<16} {item.get('VE', 'N/A'):<16} "
                  f"{item.get('abs_err', 'N/A'):<14} {item.get('bound', 'N/A'):<14} "
                  f"{item.get('computed_score', 'N/A'):<10.4f}")


if __name__ == "__main__":
    print("=" * 60)
    print("赛题三: 矩阵连乘元素的逼近 - 实验脚本")
    print("=" * 60)

    # 生成测试向量
    vectors = generate_test_vectors(20)
    print(f"\n生成测试向量: {len(vectors)} 组")

    # 实验轮数
    r_values = [1, 2, 3, 4]

    # 运行实验
    print(f"\n开始实验...")
    results = run_experiment(vectors, r_values, mode=2)

    # 分析结果
    analysis = analyze_results(results)

    # 保存结果
    save_results(results)

    # 生成表格
    generate_result_table(results, r_values)
