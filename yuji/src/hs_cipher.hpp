// =============================================================================
// hs_cipher.hpp — HS(r) 密码算法原语与线性逼近表(LAT)
// =============================================================================
// 赛题三: 矩阵连乘元素的逼近
// 第十一届(2026)全国高校密码数学挑战赛
//
// 本文件实现:
//   1. S盒定义
//   2. LAT (Linear Approximation Table) 计算
//   3. 轮函数 F = MC ∘ SR ∘ SC 的正确实现
//   4. 线性掩码传播: 给定S盒输出掩码c, 计算下一轮输入掩码
//   5. 单轮相关转移枚举: 给定输入掩码, 枚举所有非零相关的输出掩码
// =============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hs {

using u32 = uint32_t;
using u64 = uint64_t;

// ---------------------------------------------------------------------------
// S盒定义 (4-bit, 与赛题完全一致)
// ---------------------------------------------------------------------------
constexpr std::array<int, 16> SBOX = {
    0xC, 0x6, 0x9, 0x0, 0x1, 0xA, 0x2, 0xB,
    0x3, 0x8, 0x5, 0xD, 0x4, 0xE, 0x7, 0xF,
};

// ---------------------------------------------------------------------------
// 比特奇偶性
// ---------------------------------------------------------------------------
inline int parity32(u32 x) {
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return static_cast<int>(x & 1U);
}

// ---------------------------------------------------------------------------
// 半字节(nibble)操作
// 半字节顺序: nibble[0] = bits 28-31 (最高位), nibble[7] = bits 0-3 (最低位)
// ---------------------------------------------------------------------------
inline std::array<int, 8> split_nibbles(u32 x) {
    std::array<int, 8> out{};
    for (int i = 0; i < 8; ++i) {
        out[i] = static_cast<int>((x >> (28 - 4 * i)) & 0xFU);
    }
    return out;
}

inline u32 pack_nibbles(const std::array<int, 8>& xs) {
    u32 out = 0;
    for (int i = 0; i < 8; ++i) {
        out |= static_cast<u32>(xs[i] & 0xF) << (28 - 4 * i);
    }
    return out;
}

inline int active_nibble_count(u32 x) {
    int cnt = 0;
    for (int i = 0; i < 8; ++i) {
        cnt += ((x >> (4 * i)) & 0xFU) != 0U ? 1 : 0;
    }
    return cnt;
}

// ---------------------------------------------------------------------------
// 轮函数 F = MC ∘ SR ∘ SC
// 直接在 nibble 数组上进行原地变换
// ---------------------------------------------------------------------------
inline void round_function(std::array<int, 8>& state) {
    // SC: S盒层 (8个并行4-bit S盒)
    for (int i = 0; i < 8; ++i) {
        state[i] = SBOX[static_cast<size_t>(state[i])];
    }

    // SR: 行移位 (与赛题定义一致)
    // (x0,x1,x2,x3,x4,x5,x6,x7) → (x0,x5,x2,x7,x4,x1,x6,x3)
    const int t0 = state[0];
    const int t1 = state[5];
    const int t2 = state[2];
    const int t3 = state[7];
    const int t4 = state[4];
    const int t5 = state[1];
    const int t6 = state[6];
    const int t7 = state[3];

    // MC: 列混合 (与赛题定义一致, 在 F_2 上)
    // y0 = x0⊕x2⊕x3,  y1 = x0
    // y2 = x1⊕x2,      y3 = x0⊕x2
    // y4 = x4⊕x6⊕x7,  y5 = x4
    // y6 = x5⊕x6,      y7 = x4⊕x6
    state[0] = t0 ^ t2 ^ t3;
    state[1] = t0;
    state[2] = t1 ^ t2;
    state[3] = t0 ^ t2;
    state[4] = t4 ^ t6 ^ t7;
    state[5] = t4;
    state[6] = t5 ^ t6;
    state[7] = t4 ^ t6;
}

// ---------------------------------------------------------------------------
// 线性掩码前向传播: 给定S盒输出掩码 c, 计算经 MC ∘ SR 后的输出掩码
//
// 推导:
//   设 y = SC(x) 为S盒输出, 其上掩码为 c
//   经 SR: SR(y) 上的掩码 = SR(c)
//   经 MC: MC(SR(y)) 上的掩码 = (MC^{-1})^T(SR(c))
//
// 已知:
//   SR:  (c0,c1,c2,c3,c4,c5,c6,c7) → (c0,c5,c2,c7,c4,c1,c6,c3)
//   MC^{-1}:
//     x0 = y1,  x1 = y1⊕y2⊕y3,  x2 = y1⊕y3,  x3 = y0⊕y3
//     x4 = y5,  x5 = y5⊕y6⊕y7,  x6 = y5⊕y7,  x7 = y4⊕y7
//
// 合成得 (MC^{-1})^T(SR(c)):
//   b[0] = c[7]
//   b[1] = c[0] ⊕ c[2] ⊕ c[5]
//   b[2] = c[5]
//   b[3] = c[2] ⊕ c[5] ⊕ c[7]
//   b[4] = c[3]
//   b[5] = c[1] ⊕ c[4] ⊕ c[6]
//   b[6] = c[1]
//   b[7] = c[1] ⊕ c[3] ⊕ c[6]
// ---------------------------------------------------------------------------
inline std::array<int, 8> linear_mask_forward(const std::array<int, 8>& c) {
    return {{
        c[7],
        c[0] ^ c[2] ^ c[5],
        c[5],
        c[2] ^ c[5] ^ c[7],
        c[3],
        c[1] ^ c[4] ^ c[6],
        c[1],
        c[1] ^ c[3] ^ c[6],
    }};
}

// ---------------------------------------------------------------------------
// LAT (Linear Approximation Table)
// LAT[a][b] = #{x∈F_2^4 : parity(a·x) = parity(b·S(x))} - 8
// 归一化: LAT_NORM[a][b] = LAT[a][b] / 8.0
//
// LAT 在首次调用时计算并缓存 (线程安全由调用方保证)
// ---------------------------------------------------------------------------
struct LatTable {
    int raw[16][16]{};          // LAT 原始值 (范围 [-8, 8])
    double norm[16][16]{};      // 归一化相关度 (范围 [-1, 1])
    std::vector<std::pair<int, double>> nonzero[16];  // 每个输入掩码的非零输出

    LatTable() { compute(); }

    void compute() {
        for (int a = 0; a < 16; ++a) {
            nonzero[a].clear();
            for (int b = 0; b < 16; ++b) {
                int count = 0;
                for (int x = 0; x < 16; ++x) {
                    if (parity32(static_cast<u32>(a & x))
                        == parity32(static_cast<u32>(b & SBOX[static_cast<size_t>(x)]))) {
                        ++count;
                    }
                }
                raw[a][b] = count - 8;
                norm[a][b] = static_cast<double>(raw[a][b]) / 8.0;
                if (raw[a][b] != 0) {
                    nonzero[a].emplace_back(b, norm[a][b]);
                }
            }
        }
    }
};

// 全局 LAT 单例 (在 main.cpp 中定义为全局变量)
// extern 声明, 定义在 main.cpp 中
extern LatTable g_lat;

// ---------------------------------------------------------------------------
// 单轮相关转移枚举
//
// 给定输入掩码 input_mask, 枚举所有 输出掩码→相关度 的非零转移.
//
// 为了避免组合爆炸, 对每个活跃半字节只保留 top_k 个候选 (按 |corr| 降序).
// 总枚举组合数 ≤ top_k^k, 其中 k = 活跃半字节数.
// 默认 top_k = 8, 总组合上限 ≈ 8^6 ≈ 262K (6个活跃半字节).
// ---------------------------------------------------------------------------
struct OneRoundTransition {
    u32 next_mask;    // 下一轮输入掩码
    double corr;      // 相关度
};

// 最大每半字节候选数 (按 |corr| 降序取 top-k)
constexpr int MAX_CANDIDATES_PER_NIBBLE = 6;
// 最大总组合数 (超限时截断)
constexpr size_t MAX_COMBINATIONS = 100000;

inline std::vector<OneRoundTransition> compute_one_round_transitions(u32 input_mask) {
    const auto in = split_nibbles(input_mask);

    // 对每个活跃半字节, 按 |corr| 降序取 top_k 候选
    std::vector<std::pair<int, double>> choices[8];
    for (int i = 0; i < 8; ++i) {
        const int nib = in[i];
        if (nib == 0) {
            choices[i].emplace_back(0, 1.0);
        } else {
            // 取该输入掩码的非零LAT输出, 按 |corr| 排序
            auto candidates = g_lat.nonzero[nib];
            std::sort(candidates.begin(), candidates.end(),
                      [](const auto& a, const auto& b) {
                          return std::fabs(a.second) > std::fabs(b.second);
                      });
            // 只保留 top MAX_CANDIDATES_PER_NIBBLE
            size_t keep = std::min(candidates.size(), static_cast<size_t>(MAX_CANDIDATES_PER_NIBBLE));
            for (size_t j = 0; j < keep; ++j) {
                choices[i].emplace_back(candidates[j].first, candidates[j].second);
            }
        }
    }

    std::array<int, 8> sbox_out{};
    std::unordered_map<u32, double> merged;

    std::function<void(int, double)> dfs = [&](int pos, double corr) {
        if (merged.size() >= MAX_COMBINATIONS) return;  // 超限退出
        if (pos == 8) {
            const u32 next_mask = pack_nibbles(linear_mask_forward(sbox_out));
            if (next_mask != 0U) {
                merged[next_mask] += corr;
            }
            return;
        }
        for (const auto& [out_nib, c] : choices[pos]) {
            sbox_out[pos] = out_nib;
            dfs(pos + 1, corr * c);
            if (merged.size() >= MAX_COMBINATIONS) break;  // 超限退出
        }
    };

    dfs(0, 1.0);

    // 转为排序后的 vector
    std::vector<OneRoundTransition> result;
    result.reserve(merged.size());
    for (const auto& [mask, corr] : merged) {
        if (std::fabs(corr) > 1e-18) {
            result.push_back({mask, corr});
        }
    }
    std::sort(result.begin(), result.end(),
              [](const OneRoundTransition& a, const OneRoundTransition& b) {
                  if (std::fabs(a.corr) != std::fabs(b.corr))
                      return std::fabs(a.corr) > std::fabs(b.corr);
                  return a.next_mask < b.next_mask;
              });
    return result;
}

// 带缓存的单轮转移查询
// 对于频繁出现的掩码 (如零掩码附近的低活跃度掩码), 缓存可大幅加速
inline const std::vector<OneRoundTransition>&
get_one_round_transitions(u32 input_mask,
                          std::unordered_map<u32, std::vector<OneRoundTransition>>& cache) {
    auto it = cache.find(input_mask);
    if (it != cache.end()) return it->second;
    return cache.emplace(input_mask, compute_one_round_transitions(input_mask))
        .first->second;
}

// ---------------------------------------------------------------------------
// 暴力枚举方式1: 枚举全部 2^32 个明文, 精确计算 M(r)[v,u]
// 仅用于小r的验证, 复杂度 O(2^32), 运行一次需大量时间
// ---------------------------------------------------------------------------
inline u32 permute(u32 x, int rounds) {
    auto state = split_nibbles(x);
    for (int r = 0; r < rounds; ++r) {
        round_function(state);
    }
    return pack_nibbles(state);
}

inline double brute_force_correlation(u32 u, u32 v, int rounds) {
    long long count = 0;
    constexpr u64 total = 1ULL << 32;
    for (u64 x = 0; x < total; ++x) {
        const u32 y = permute(static_cast<u32>(x), rounds);
        count += (parity32(u & static_cast<u32>(x)) == parity32(v & y)) ? 1 : -1;
    }
    return static_cast<double>(count) / static_cast<double>(total);
}

// ---------------------------------------------------------------------------
// 得分公式: score = log2(4^r × |VE|) = log2(2^(2r) × |VE|)
// ---------------------------------------------------------------------------
inline double compute_score(double ve, int rounds) {
    return std::log2(std::pow(4.0, rounds) * std::fabs(ve));
}

// ---------------------------------------------------------------------------
// 有效性条件检查
// ---------------------------------------------------------------------------
inline bool is_valid_estimate(int rounds, u32 u, u32 v, double ve, double vt) {
    if (ve == 0.0 || u == 0U || v == 0U) return false;
    const double bound = std::fabs(vt) * std::pow(2.0, -2.0 * rounds);
    return std::fabs(ve - vt) <= bound;
}

// ---------------------------------------------------------------------------
// 生成所有单活跃输入掩码 (120 = 8×15 个)
// ---------------------------------------------------------------------------
inline std::vector<u32> all_single_active_inputs() {
    std::vector<u32> masks;
    masks.reserve(120);
    for (int pos = 0; pos < 8; ++pos) {
        for (int nib = 1; nib < 16; ++nib) {
            std::array<int, 8> xs{};
            xs[pos] = nib;
            masks.push_back(pack_nibbles(xs));
        }
    }
    return masks;
}

}  // namespace hs
