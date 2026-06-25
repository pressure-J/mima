// =============================================================================
// hs_cipher.hpp — HS(r) 密码算法原语与线性逼近表(LAT) [优化版 v2]
// =============================================================================
// 赛题三: 矩阵连乘元素的逼近
// 第十一届(2026)全国高校密码数学挑战赛
//
// 优化点:
//   1. LAT 预计算所有单活跃掩码转移 (避免重复DFS)
//   2. 迭代笛卡尔积替代递归DFS
//   3. 更快的数据结构 (vector + sort 替代 unordered_map 合并)
//   4. 更激进但智能的候选裁剪
// =============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
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

// 快速奇偶性 (使用内置函数)
inline int parity32_fast(u32 x) {
    return __builtin_parity(x);
}

// ---------------------------------------------------------------------------
// 半字节(nibble)操作
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
// 轮函数 F = MC ∘ SR ∘ SC (直接在 nibble 数组上原地变换)
// ---------------------------------------------------------------------------
inline void round_function(std::array<int, 8>& state) {
    // SC: S盒层
    for (int i = 0; i < 8; ++i) {
        state[i] = SBOX[static_cast<size_t>(state[i])];
    }
    // SR + MC 合并
    const int t0 = state[0];
    const int t1 = state[5];
    const int t2 = state[2];
    const int t3 = state[7];
    const int t4 = state[4];
    const int t5 = state[1];
    const int t6 = state[6];
    const int t7 = state[3];

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
// 线性掩码前向传播: (MC^{-1})^T ∘ SR
//    b[0] = c[7]
//    b[1] = c[0] ⊕ c[2] ⊕ c[5]
//    b[2] = c[5]
//    b[3] = c[2] ⊕ c[5] ⊕ c[7]
//    b[4] = c[3]
//    b[5] = c[1] ⊕ c[4] ⊕ c[6]
//    b[6] = c[1]
//    b[7] = c[1] ⊕ c[3] ⊕ c[6]
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

// 内联快速版本: 直接写入数组
inline void linear_mask_forward_fast(const int c[8], int b[8]) {
    b[0] = c[7];
    b[1] = c[0] ^ c[2] ^ c[5];
    b[2] = c[5];
    b[3] = c[2] ^ c[5] ^ c[7];
    b[4] = c[3];
    b[5] = c[1] ^ c[4] ^ c[6];
    b[6] = c[1];
    b[7] = c[1] ^ c[3] ^ c[6];
}

// ---------------------------------------------------------------------------
// LAT (Linear Approximation Table)
// ---------------------------------------------------------------------------
struct LatTable {
    int raw[16][16]{};
    double norm[16][16]{};
    // nonzero[a] = [(b, corr), ...] 按 |corr| 降序排列
    std::vector<std::pair<int, double>> nonzero[16];

    LatTable() { compute(); }

    void compute() {
        for (int a = 0; a < 16; ++a) {
            nonzero[a].clear();
            for (int b = 0; b < 16; ++b) {
                int count = 0;
                for (int x = 0; x < 16; ++x) {
                    if (parity32_fast(static_cast<u32>(a & x))
                        == parity32_fast(static_cast<u32>(b & SBOX[static_cast<size_t>(x)]))) {
                        ++count;
                    }
                }
                raw[a][b] = count - 8;
                norm[a][b] = static_cast<double>(raw[a][b]) / 8.0;
                if (raw[a][b] != 0) {
                    nonzero[a].emplace_back(b, norm[a][b]);
                }
            }
            // 预先按 |corr| 降序排列
            std::sort(nonzero[a].begin(), nonzero[a].end(),
                      [](const auto& x, const auto& y) {
                          return std::fabs(x.second) > std::fabs(y.second);
                      });
        }
    }
};

extern LatTable g_lat;

// ---------------------------------------------------------------------------
// 单轮相关转移 (优化版)
// ---------------------------------------------------------------------------
struct OneRoundTransition {
    u32 next_mask;
    double corr;
};

// 最大每半字节候选数 (常规)
constexpr int MAX_CANDIDATES_PER_NIBBLE = 6;
// 最大每半字节候选数 (高活跃度, >=6个活跃半字节)
constexpr int MAX_CANDIDATES_HIGH_ACTIVITY = 3;
// 最大总组合数
constexpr size_t MAX_COMBINATIONS = 25000;

// ---------------------------------------------------------------------------
// ★ 优化1: 预计算所有单活跃掩码转移
// 对每个位置 pos∈[0,7], 每个 nibble∈[1,15], 预计算所有输出转移
// 这些是批量计算中最常被查询的掩码
// ---------------------------------------------------------------------------
struct PrecomputedTransitions {
    // single_trans[pos][nibble] = 该单活跃掩码的所有转移
    std::vector<OneRoundTransition> single_trans[8][16];

    PrecomputedTransitions() {
        for (int pos = 0; pos < 8; ++pos) {
            for (int nib = 1; nib < 16; ++nib) {
                std::array<int, 8> in{};
                in[pos] = nib;
                u32 mask = pack_nibbles(in);
                single_trans[pos][nib] = compute_transitions_optimized(mask);
            }
        }
    }

    // 优化的转移计算: 对于单活跃掩码, 结果直接来自LAT
    static std::vector<OneRoundTransition> compute_transitions_optimized(u32 input_mask) {
        const auto in = split_nibbles(input_mask);
        int active_count = 0;
        for (int i = 0; i < 8; ++i) if (in[i] != 0) ++active_count;

        // 对于单活跃掩码, 直接枚举LAT的非零输出
        if (active_count == 1) {
            int pos = 0;
            int nib = 0;
            for (int i = 0; i < 8; ++i) {
                if (in[i] != 0) { pos = i; nib = in[i]; break; }
            }
            const auto& candidates = g_lat.nonzero[nib];
            std::vector<OneRoundTransition> result;
            result.reserve(candidates.size());
            std::array<int, 8> sbox_out{};
            for (const auto& [out_nib, c] : candidates) {
                sbox_out[pos] = out_nib;
                int b[8];
                linear_mask_forward_fast(sbox_out.data(), b);
                u32 next = pack_nibbles(std::array<int, 8>{b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]});
                if (next != 0U) {
                    result.push_back({next, c});
                }
                sbox_out[pos] = 0;
            }
            return result;
        }

        // 多活跃掩码: 迭代笛卡尔积
        return compute_cartesian(in);
    }

    // 活跃位置的候选 (用于笛卡尔积枚举)
    struct NibbleChoice {
        int pos;
        std::vector<std::pair<int, double>> cands; // (out_nib, corr)
    };

    // 迭代笛卡尔积: 避免递归开销
    static std::vector<OneRoundTransition> compute_cartesian(const std::array<int, 8>& in) {
        // 统计活跃位置
        int active_count = 0;
        for (int i = 0; i < 8; ++i) if (in[i] != 0) ++active_count;

        // 对于高活跃度掩码, 使用更少的候选数
        int max_cands = (active_count >= 6) ? MAX_CANDIDATES_HIGH_ACTIVITY
                                            : MAX_CANDIDATES_PER_NIBBLE;
        // 总组合数也可以更严格
        size_t max_comb = (active_count >= 5) ? MAX_COMBINATIONS / 5 : MAX_COMBINATIONS;

        // 收集每个活跃位置的候选
        std::vector<NibbleChoice> active_positions;
        for (int i = 0; i < 8; ++i) {
            if (in[i] != 0) {
                const auto& cands = g_lat.nonzero[in[i]];
                size_t keep = std::min(cands.size(), static_cast<size_t>(max_cands));
                std::vector<std::pair<int, double>> kept(cands.begin(), cands.begin() + static_cast<std::ptrdiff_t>(keep));
                active_positions.push_back({i, std::move(kept)});
            }
        }

        if (active_positions.empty()) return {};

        // 估算总组合数
        size_t total_combinations = 1;
        for (const auto& ap : active_positions) {
            total_combinations *= ap.cands.size();
            if (total_combinations > max_comb * 2) break;
        }

        // 使用 vector<pair<u32,double>> 存储, 最后排序合并
        std::vector<std::pair<u32, double>> merged;
        if (total_combinations <= max_comb) {
            merged.reserve(total_combinations);
            enumerate_all(active_positions, merged, max_comb);
        } else {
            merged.reserve(max_comb);
            enumerate_truncated(active_positions, merged, max_comb);
        }

        // 合并相同next_mask
        if (merged.empty()) return {};
        std::sort(merged.begin(), merged.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        std::vector<OneRoundTransition> result;
        result.reserve(merged.size());
        u32 cur_mask = merged[0].first;
        double cur_corr = merged[0].second;
        for (size_t i = 1; i < merged.size(); ++i) {
            if (merged[i].first == cur_mask) {
                cur_corr += merged[i].second;
            } else {
                if (std::fabs(cur_corr) > 1e-18) {
                    result.push_back({cur_mask, cur_corr});
                }
                cur_mask = merged[i].first;
                cur_corr = merged[i].second;
            }
        }
        if (std::fabs(cur_corr) > 1e-18) {
            result.push_back({cur_mask, cur_corr});
        }

        // 按 |corr| 降序排列
        std::sort(result.begin(), result.end(),
                  [](const OneRoundTransition& a, const OneRoundTransition& b) {
                      if (std::fabs(a.corr) != std::fabs(b.corr))
                          return std::fabs(a.corr) > std::fabs(b.corr);
                      return a.next_mask < b.next_mask;
                  });
        return result;
    }

    // 完整枚举 (带上限)
    static void enumerate_all(
        const std::vector<NibbleChoice>& active,
        std::vector<std::pair<u32, double>>& out,
        size_t max_entries)
    {
        const size_t n = active.size();
        std::vector<size_t> indices(n, 0);
        std::array<int, 8> sbox_out{};
        int c_arr[8] = {};

        while (true) {
            // 构建当前组合
            double corr = 1.0;
            for (size_t i = 0; i < n; ++i) {
                int out_nib = active[i].cands[indices[i]].first;
                sbox_out[active[i].pos] = out_nib;
                corr *= active[i].cands[indices[i]].second;
            }
            linear_mask_forward_fast(sbox_out.data(), c_arr);
            std::array<int, 8> next_arr{c_arr[0], c_arr[1], c_arr[2], c_arr[3], c_arr[4], c_arr[5], c_arr[6], c_arr[7]};
            u32 next_mask = pack_nibbles(next_arr);
            if (next_mask != 0U) {
                out.emplace_back(next_mask, corr);
                if (out.size() >= max_entries) return;
            }
            // 清除
            for (size_t i = 0; i < n; ++i) sbox_out[active[i].pos] = 0;

            // 递增索引
            size_t pos = n;
            while (pos > 0) {
                --pos;
                if (++indices[pos] < active[pos].cands.size()) break;
                indices[pos] = 0;
            }
            if (pos == 0 && indices[0] == 0) break;
        }
    }

    // 截断枚举
    static void enumerate_truncated(
        const std::vector<NibbleChoice>& active,
        std::vector<std::pair<u32, double>>& out,
        size_t max_entries)
    {
        enumerate_all(active, out, max_entries);
    }
};

// 全局预计算单例
extern PrecomputedTransitions g_precomp;

// ---------------------------------------------------------------------------
// 原始 compute_one_round_transitions (保留兼容)
// ---------------------------------------------------------------------------
inline std::vector<OneRoundTransition> compute_one_round_transitions(u32 input_mask) {
    // 如果是单活跃, 使用预计算
    auto in = split_nibbles(input_mask);
    int active_count = 0;
    int active_pos = -1;
    int active_nib = 0;
    for (int i = 0; i < 8; ++i) {
        if (in[i] != 0) { ++active_count; active_pos = i; active_nib = in[i]; }
    }
    if (active_count == 1 && g_precomp.single_trans[active_pos][active_nib].size() > 0) {
        return g_precomp.single_trans[active_pos][active_nib];
    }
    return PrecomputedTransitions::compute_transitions_optimized(input_mask);
}

// ---------------------------------------------------------------------------
// 带缓存的单轮转移查询 (线程安全版本)
// ---------------------------------------------------------------------------
inline const std::vector<OneRoundTransition>&
get_one_round_transitions(u32 input_mask,
                          std::unordered_map<u32, std::vector<OneRoundTransition>>& cache) {
    auto it = cache.find(input_mask);
    if (it != cache.end()) return it->second;
    return cache.emplace(input_mask, compute_one_round_transitions(input_mask))
        .first->second;
}

// 线程安全版本: 带互斥锁的缓存
struct ThreadSafeCache {
    std::unordered_map<u32, std::vector<OneRoundTransition>> cache;
    std::mutex mtx;

    const std::vector<OneRoundTransition>& get(u32 mask) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = cache.find(mask);
            if (it != cache.end()) return it->second;
        }
        auto trans = compute_one_round_transitions(mask);
        std::lock_guard<std::mutex> lock(mtx);
        auto [it, _] = cache.emplace(mask, std::move(trans));
        return it->second;
    }
};

// ---------------------------------------------------------------------------
// 暴力枚举方式1: 精确计算 M(r)[v,u]
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
        count += (parity32_fast(u & static_cast<u32>(x)) == parity32_fast(v & y)) ? 1 : -1;
    }
    return static_cast<double>(count) / static_cast<double>(total);
}

// ---------------------------------------------------------------------------
// 得分公式
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
// 生成所有单活跃输入掩码
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
