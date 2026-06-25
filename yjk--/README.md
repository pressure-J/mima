# 方式2逼近算法 — 矩阵连乘元素的逼近

赛题三: 矩阵连乘元素的逼近
第十一届(2026)全国高校密码数学挑战赛

## 项目结构

```
yuji/
├── src/                        # C++源码 (算法实现)
│   ├── main.cpp               # CLI入口
│   ├── hs_cipher.hpp          # HS(r)密码原语 + LAT
│   └── approx_engine.hpp      # 逼近引擎 (核心算法)
├── scripts/                    # Python脚本 (数据分析)
│   ├── compute_scores.py      # 计分脚本
│   ├── verify_results.py      # 独立验证脚本
│   └── run_experiments.py     # 批量实验运行器
├── experiments/                # 实验配置
├── results/                    # 实验结果输出
├── tests/                      # 测试用例
├── Makefile                    # 构建
└── README.md
```

## 算法概述

### 三种互补策略

| 策略 | 方法 | 复杂度 | 精度 | 适用场景 |
|------|------|--------|------|----------|
| A: 精确稀疏DP | 完整枚举相关转移 | O(r·S) | 精确(VE=VT) | 状态数≤50万 |
| B: 波束搜索 | 保留Top-B状态 | O(r·B·log B) | 有界误差 | 通用 |
| C: 蒙特卡洛 | 随机采样 | O(N·r) | σ=1/√N | 交叉验证 |

### 综合策略
1. 优先使用精确稀疏DP (当状态空间可控时)
2. 状态爆炸时自动切换到波束搜索
3. 使用蒙特卡洛进行交叉验证
4. 所有方法复杂度均 < 2^32

## 构建

```bash
cd yuji
make        # 或: g++ -O3 -std=c++17 -o approx_cor src/main.cpp -I src
```

## 使用

```bash
# 查看信息
./approx_cor info

# 单次估计
./approx_cor estimate 0x20000000 0x00000888 2

# 精确计算
./approx_cor exact 0x20000000 0x00000888 2

# 批量搜索 (全部单活跃输入)
./approx_cor batch-all 3 --beam 200000 --output results

# 批量搜索 (指定位置)
./approx_cor batch-position 3 0 --beam 200000 --output results/tmp

# 交叉验证
./approx_cor verify 0x20000000 0x00000888 2 --mc 100000

# 暴力验证 (方式1, 谨慎使用)
./approx_cor brute 0x20000000 0x00000888 2
```

## 运行实验

```bash
# 方法1: 直接使用C++ CLI
make run-r3 BEAM=200000

# 方法2: 使用Python脚本
python scripts/run_experiments.py --max-r 3 --beam 200000

# 计分
python scripts/compute_scores.py results/valid_estimates.txt results/scores.txt

# 验证
python scripts/verify_results.py
python scripts/verify_results.py --verify results/valid_estimates.txt --sample 100
```

## 算法复杂度分析

与方式1 (2^32 ≈ 4.3×10^9) 对比:

| 策略 | r=3 | r=5 | r=8 | 加速比 |
|------|-----|-----|-----|--------|
| 精确稀疏DP | ~10^4 | ~10^5 | ~10^6 | 10^3-10^5 |
| 波束搜索(B=50K) | ~5×10^6 | ~8×10^6 | ~1.3×10^7 | 300-800 |
| 蒙特卡洛(N=10^6) | ~3×10^6 | ~5×10^6 | ~8×10^6 | 500-1400 |

## 误差分析

- **精确稀疏DP**: error = 0 (VE = VT)
- **波束搜索**: |VE - VT| ≤ Σ_{裁剪状态} |corr| (保守上界)
- **蒙特卡洛**: 95% 置信区间 = VE ± 1.96/√N
