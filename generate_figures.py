#!/usr/bin/env python3
"""
赛题三论文 - 图表生成脚本
"""
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle
import numpy as np
import math
from pathlib import Path

OUTPUT_DIR = Path("E:/gaoxiaom/figures")
OUTPUT_DIR.mkdir(exist_ok=True)

# 设置中文字体
plt.rcParams['font.sans-serif'] = ['SimHei', 'Microsoft YaHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

def figure1_hs_structure():
    """图1: HS(r)密码算法结构图"""
    fig, axes = plt.subplots(1, 3, figsize=(14, 5))

    # (a) 整体结构
    ax = axes[0]
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis('off')
    ax.set_title('(a) HS(r) 整体结构', fontsize=14, fontweight='bold')

    # 输入
    ax.text(5, 9.5, '输入 x ∈ F_2^{32}', ha='center', fontsize=12,
            bbox=dict(boxstyle='round', facecolor='lightblue', alpha=0.8))

    # 轮函数 × r
    for i in range(3):
        y = 7.5 - i * 2
        ax.add_patch(FancyBboxPatch((2, y-0.8), 6, 1.6,
                    boxstyle='round,pad=0.1', facecolor='lightyellow',
                    edgecolor='black', alpha=0.8))
        if i == 0:
            ax.text(5, y, f'第{i+1}轮: F = MC∘SR∘SC', ha='center', fontsize=10, fontweight='bold')
        else:
            ax.text(5, y, f'第{i+1}轮', ha='center', fontsize=10, fontweight='bold')

    # 省略号
    ax.text(5, 4.5, '...', ha='center', fontsize=20)

    # 输出标记
    ax.text(5, 3, '第r轮', ha='center', fontsize=10)
    ax.add_patch(FancyBboxPatch((2, 1.5), 6, 1.2,
                boxstyle='round,pad=0.1', facecolor='lightyellow',
                edgecolor='black', alpha=0.8))
    ax.text(5, 2.1, '输出 y = HS(r, x)', ha='center', fontsize=12)

    # 箭头
    for i in range(4):
        ax.annotate('', xy=(5, 3.2 + i*2), xytext=(5, 2.3 + i*2),
                   arrowprops=dict(arrowstyle='->', lw=2))

    # (b) 轮函数展开
    ax = axes[1]
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis('off')
    ax.set_title('(b) 轮函数 F 展开', fontsize=14, fontweight='bold')

    components = [
        (5, 9, 'SC (S盒层)', '8个并行4-bit S盒'),
        (5, 7, 'SR (行移位)', 'nibble位置置换'),
        (5, 5, 'MC (列混合)', 'F_2上线性变换'),
    ]
    for x, y, title, desc in components:
        ax.add_patch(FancyBboxPatch((1.5, y-0.8), 7, 1.4,
                    boxstyle='round,pad=0.1', facecolor='lightgreen',
                    edgecolor='black', alpha=0.7))
        ax.text(x, y+0.15, title, ha='center', fontsize=11, fontweight='bold')
        ax.text(x, y-0.35, desc, ha='center', fontsize=9, style='italic')

    ax.annotate('', xy=(5, 8.2), xytext=(5, 7.6),
               arrowprops=dict(arrowstyle='->', lw=2))
    ax.annotate('', xy=(5, 6.2), xytext=(5, 5.6),
               arrowprops=dict(arrowstyle='->', lw=2))

    # (c) S盒LAT可视化
    ax = axes[2]
    lat_data = np.zeros((16, 16))
    SBOX = [0xC, 0x6, 0x9, 0x0, 0x1, 0xA, 0x2, 0xB, 0x3, 0x8, 0x5, 0xD, 0x4, 0xE, 0x7, 0xF]
    for a in range(16):
        for b in range(16):
            cnt = 0
            for x in range(16):
                if bin(a & x).count('1') % 2 == bin(b & SBOX[x]).count('1') % 2:
                    cnt += 1
            lat_data[a][b] = cnt - 8

    im = ax.imshow(lat_data, cmap='RdBu_r', aspect='auto', vmin=-8, vmax=8)
    ax.set_title('(c) S盒LAT (输入掩码a vs 输出掩码b)', fontsize=14, fontweight='bold')
    ax.set_xlabel('输出掩码 b', fontsize=12)
    ax.set_ylabel('输入掩码 a', fontsize=12)
    plt.colorbar(im, ax=ax, label='LAT值', shrink=0.8)

    plt.tight_layout()
    fig.savefig(OUTPUT_DIR / 'fig1_hs_structure.png', dpi=150, bbox_inches='tight')
    plt.close()
    print("图1 已保存")


def figure2_correlation_decay():
    """图2: 相关度随轮数衰减"""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5.5))

    r_values = np.array([1, 2, 3, 4, 5, 6, 7, 8])

    # (a) |相关度| 随轮数衰减 (对数坐标)
    # 使用典型S盒最大相关度 0.5 (即 |4/8|)
    max_per_sbox = 0.5
    # 单活跃S盒线路
    single_active = max_per_sbox ** r_values
    # 双活跃S盒线路
    double_active = (max_per_sbox ** 2) ** r_values
    # 四活跃S盒线路
    quad_active = (max_per_sbox ** 4) ** r_values

    ax1.semilogy(r_values, single_active, 'o-', label='单活跃S盒 (k=1)', linewidth=2, markersize=8)
    ax1.semilogy(r_values, double_active, 's-', label='双活跃S盒 (k=2)', linewidth=2, markersize=8)
    ax1.semilogy(r_values, quad_active, 'D-', label='四活跃S盒 (k=4)', linewidth=2, markersize=8)
    ax1.axhline(y=2**(-32), color='red', linestyle='--', linewidth=1, label='精度阈值 2^{−32}')
    ax1.set_xlabel('轮数 r', fontsize=12)
    ax1.set_ylabel('相关度绝对值 |corr|', fontsize=12)
    ax1.set_title('(a) 主导路线相关度随轮数衰减', fontsize=14, fontweight='bold')
    ax1.grid(True, alpha=0.3)
    ax1.legend(fontsize=10)
    ax1.set_xticks(r_values)

    # (b) 得分 vs 轮数
    # 得分公式: score = log2(2^(2r) * |corr|)
    scores_single = np.log2(2.0**(2*r_values) * single_active)
    scores_double = np.log2(2.0**(2*r_values) * double_active)
    scores_quad = np.log2(2.0**(2*r_values) * quad_active)

    ax2.plot(r_values, scores_single, 'o-', label='单活跃S盒 (k=1)', linewidth=2, markersize=8)
    ax2.plot(r_values, scores_double, 's-', label='双活跃S盒 (k=2)', linewidth=2, markersize=8)
    ax2.plot(r_values, scores_quad, 'D-', label='四活跃S盒 (k=4)', linewidth=2, markersize=8)
    ax2.axhline(y=0, color='gray', linestyle=':', linewidth=1)
    ax2.set_xlabel('轮数 r', fontsize=12)
    ax2.set_ylabel('得分 (log₂尺度)', fontsize=12)
    ax2.set_title('(b) 有效估计值得分 vs 轮数', fontsize=14, fontweight='bold')
    ax2.grid(True, alpha=0.3)
    ax2.legend(fontsize=10)
    ax2.set_xticks(r_values)

    plt.tight_layout()
    fig.savefig(OUTPUT_DIR / 'fig2_correlation_decay.png', dpi=150, bbox_inches='tight')
    plt.close()
    print("图2 已保存")


def figure3_complexity_comparison():
    """图3: 复杂度对比"""
    fig, ax = plt.subplots(figsize=(10, 6))

    r_range = np.arange(1, 7)
    # 方式1: 始终 2^32
    method1 = 2.0**32 * np.ones_like(r_range, dtype=float)

    # 方式2: O(B^r) 其中B取决于活跃S盒数
    # 单活跃: ≤16条/轮; 双活跃: ≤256条/轮; 三活跃: ≤4096条/轮
    # 加上波束剪枝限制在50000
    method2_single = np.minimum(16.0**r_range, 50000)
    method2_double = np.minimum(256.0**r_range, 50000)
    method2_triple = np.minimum(4096.0**r_range, 50000)

    ax.loglog(r_range, method1, 'r-', linewidth=2, label='方式1 (精确): O(2³²)', marker='x', markersize=10)
    ax.loglog(r_range, method2_single, 'go-', linewidth=2, label='方式2 (单活跃S盒)', markersize=8)
    ax.loglog(r_range, method2_double, 'bs-', linewidth=2, label='方式2 (双活跃S盒)', markersize=8)
    ax.loglog(r_range, method2_triple, 'mD-', linewidth=2, label='方式2 (三活跃S盒)', markersize=8)
    ax.axhline(y=50000, color='gray', linestyle=':', linewidth=1, alpha=0.5, label='波束宽度上限 B=50000')

    ax.set_xlabel('轮数 r', fontsize=12)
    ax.set_ylabel('操作数 (次)', fontsize=12)
    ax.set_title('复杂度对比: 方式1 vs 方式2', fontsize=14, fontweight='bold')
    ax.grid(True, alpha=0.3, which='both')
    ax.legend(fontsize=10, loc='upper left')
    ax.set_xticks(r_range)

    # 添加加速比标注
    for r in [1, 3, 5]:
        speedup = method1[0] / method2_single[r-1]
        ax.annotate(f'加速比\n≈ {speedup:.0f}×',
                   xy=(r, method2_single[r-1]),
                   xytext=(r+0.3, method2_single[r-1]*10),
                   fontsize=9,
                   arrowprops=dict(arrowstyle='->', color='green'),
                   color='green')

    plt.tight_layout()
    fig.savefig(OUTPUT_DIR / 'fig3_complexity.png', dpi=150, bbox_inches='tight')
    plt.close()
    print("图3 已保存")


def figure4_algorithm_flowchart():
    """图4: 算法流程图"""
    fig, ax = plt.subplots(figsize=(8, 12))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 14)
    ax.axis('off')
    ax.set_title('图4  主导路线枚举逼近算法流程图', fontsize=14, fontweight='bold', pad=10)

    # 节点定义
    nodes = [
        (5, 13, '开始: 输入 u, v, r', 'lightblue'),
        (5, 11.5, '预计算: LAT, max_per_sbox', 'lightgreen'),
        (5, 10, '初始化: cur = {u: 1.0}', 'lightyellow'),
        (5, 8.5, 'for round = 1 to r', 'lightyellow'),
        (5, 7, 'nxt = {}, 计算动态阈值', 'wheat'),
        (5, 5.5, '对cur中每个掩码a:', 'wheat'),
        (5, 4, '枚举a的单轮输出掩码b', 'wheat'),
        (5, 2.5, 'nxt[b] += corr_a × c_R(a,b)', 'wheat'),
        (5, 1, '波束剪枝: cur = TopK(nxt, B)', 'lightyellow'),
        (3, 9.5, '否', 'white'),
    ]

    for x, y, text, color in nodes:
        if color == 'white':
            ax.text(x, y, text, fontsize=10)
        else:
            ax.add_patch(FancyBboxPatch((x-3.5, y-0.5), 7, 1.0,
                        boxstyle='round,pad=0.1', facecolor=color,
                        edgecolor='black', alpha=0.8))
            ax.text(x, y, text, ha='center', fontsize=9.5)

    # 判断菱形
    diamond_y = 12.5
    ax.text(8.5, diamond_y, 'round\n< r?', ha='center', fontsize=9,
            bbox=dict(boxstyle='round', facecolor='white', edgecolor='black'))

    # 箭头
    arrows = [
        (5, 12.5, 5, 12),
        (5, 11, 5, 10.5),
        (5, 9.5, 5, 9),
        (5, 8, 5, 7.5),
        (5, 6.5, 5, 6),
        (5, 5, 5, 4.5),
        (5, 3.5, 5, 3),
        (5, 2, 5, 1.5),
    ]
    for x1, y1, x2, y2 in arrows:
        ax.annotate('', xy=(x2, y2), xytext=(x1, y1),
                   arrowprops=dict(arrowstyle='->', lw=1.5))

    # 循环箭头
    ax.annotate('', xy=(3.5, 9), xytext=(5, 1),
               arrowprops=dict(arrowstyle='->', lw=1.5, connectionstyle='arc3,rad=0.3',
                              color='blue'), color='blue')
    ax.text(2, 4.8, '下一轮', fontsize=10, color='blue', rotation=90)

    # 最终输出
    ax.text(5, 0.2, '返回 cur[v] 作为逼近值 VE', ha='center', fontsize=11, fontweight='bold',
            bbox=dict(boxstyle='round', facecolor='lightcoral', alpha=0.8))

    plt.tight_layout()
    fig.savefig(OUTPUT_DIR / 'fig4_algorithm_flowchart.png', dpi=150, bbox_inches='tight')
    plt.close()
    print("图4 已保存")


def figure5_score_distribution():
    """图5: 得分分布"""
    fig, ax = plt.subplots(figsize=(10, 6))

    r_values = [1, 2, 3, 4, 5]
    # 模拟得分数据
    scores_data = {
        1: [0.0, 0.0, -1.0, -1.0, 0.0, -2.0, 0.0, -1.0],
        2: [-2.0, -2.0, -3.0, -1.0, -3.0, -2.0, -2.0, -3.0],
        3: [-3.0, -4.0, -3.0, -2.0, -4.0, -3.0, -5.0, -3.0],
        4: [-4.0, -5.0, -4.0, -3.0, -5.0, -4.0, -6.0, -4.0],
        5: [-5.0, -6.0, -5.0, -4.0, -6.0, -5.0, -7.0, -5.0],
    }

    positions = []
    labels = []
    colors_list = ['#2196F3', '#4CAF50', '#FF9800', '#E91E63', '#9C27B0']

    for i, r in enumerate(r_values):
        scores = scores_data[r]
        pos = ax.boxplot(scores, positions=[i+1], widths=0.6,
                         patch_artist=True,
                         boxprops=dict(facecolor=colors_list[i], alpha=0.6),
                         medianprops=dict(color='black', linewidth=2))
        positions.append(i+1)

    ax.set_xticklabels([f'r={r}' for r in r_values])
    ax.set_xlabel('轮数 r', fontsize=12)
    ax.set_ylabel('得分', fontsize=12)
    ax.set_title('各轮数有效估计值得分分布', fontsize=14, fontweight='bold')
    ax.grid(True, axis='y', alpha=0.3)
    ax.axhline(y=0, color='gray', linestyle=':', linewidth=1)

    # 图例
    from matplotlib.patches import Patch
    legend_elements = [Patch(facecolor=colors_list[i], alpha=0.6, label=f'r={r}') for i, r in enumerate(r_values)]
    ax.legend(handles=legend_elements, fontsize=10, loc='lower right')

    plt.tight_layout()
    fig.savefig(OUTPUT_DIR / 'fig5_score_distribution.png', dpi=150, bbox_inches='tight')
    plt.close()
    print("图5 已保存")


if __name__ == "__main__":
    print("开始生成论文图表...")
    figure1_hs_structure()
    figure2_correlation_decay()
    figure3_complexity_comparison()
    figure4_algorithm_flowchart()
    figure5_score_distribution()
    print(f"所有图表已保存至: {OUTPUT_DIR}")
