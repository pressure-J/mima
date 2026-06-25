// =============================================================================
// approx_engine.hpp — 方式2 逼近引擎
// =============================================================================
// 赛题三: 矩阵连乘元素的逼近
//
// 本文件实现三种互补的逼近策略:
//
//   Strategy A: 精确稀疏DP (Exact Sparse DP)
//     - 对单活跃输入掩码, 完整枚举所有相关转移路径
//     - 当状态空间可控时给出精确结果 (VE = VT)
//     - 复杂度: O(r × 10^k) 每轮, k = 平均活跃半字节数
//
//   Strategy B: 波束搜索 (Beam Search)
//     - 每轮只保留相关度绝对值最大的 B 个状态
//     - 追踪被裁剪状态的 |corr| 总和作为误差上界
//     - 复杂度: O(r × B × log B)
//     - 保证: |VE - VT| ≤ 裁剪误差上界
//
//   Strategy C: 蒙特卡洛采样 (Monte Carlo)
//     - 随机采样 N 个输入, 统计相关度
//     - 无偏估计, 标准误差 = 1/√N
//     - 复杂度: O(N × r), 完全可控
//     - 用于交叉验证和补充
//
// 综合策略: 优先使用 A, 状态爆炸时切换到 B, 使用 C 交叉验证
// =============================================================================

#pragma once

#include "hs_cipher.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace hs {

// ===========================================================================
// 结果条目
// ===========================================================================
struct EstimateEntry {
    int rounds;
    u32 u, v;
    double ve;       // 估计值
    double vt;       // 真实值 (若可精确计算), 否则等于 ve
    double error_bound;  // 误差上界
    double score;
    bool is_exact;   // 是否为精确结果 (无裁剪)
};

// ===========================================================================
// Strategy A: 精确稀疏DP
//
// 对所有单轮转移进行完整枚举, 不做任何裁剪.
// 仅当状态空间 ≤ max_states 时使用, 否则返回空结果.
// ===========================================================================
struct ExactSparseResult {
    std::unordered_map<u32, double> distribution;
    bool completed;  // true 表示完整计算, false 表示状态爆炸
    size_t max_state_count;
};

inline ExactSparseResult exact_sparse_dp(
    u32 start_mask, int rounds,
    size_t max_states = 500000,
    std::unordered_map<u32, std::vector<OneRoundTransition>>* cache = nullptr)
{
    // 局部缓存 (如果调用方没有提供)
    std::unordered_map<u32, std::vector<OneRoundTransition>> local_cache;
    if (!cache) cache = &local_cache;

    std::unordered_map<u32, double> cur;
    cur.reserve(1024);
    cur[start_mask] = 1.0;

    size_t max_seen = 1;
    bool completed = true;

    for (int r = 0; r < rounds; ++r) {
        std::unordered_map<u32, double> nxt;
        nxt.reserve(std::min(cur.size() * 8, max_states));

        for (const auto& [mask, corr] : cur) {
            const auto& trans = get_one_round_transitions(mask, *cache);
            for (const auto& t : trans) {
                nxt[t.next_mask] += corr * t.corr;
            }
        }

        // 清除极小值
        for (auto it = nxt.begin(); it != nxt.end(); ) {
            if (std::fabs(it->second) <= 1e-20) {
                it = nxt.erase(it);
            } else {
                ++it;
            }
        }

        max_seen = std::max(max_seen, nxt.size());

        if (nxt.size() > max_states) {
            completed = false;
            break;
        }

        cur = std::move(nxt);
    }

    return {completed ? std::move(cur) : std::unordered_map<u32, double>{},
            completed, max_seen};
}

// ===========================================================================
// Strategy B: 波束搜索 (Beam Search)
//
// 每轮保留相关度绝对值最大的 beam_width 个状态.
// 裁剪误差 = Σ 所有被裁剪状态的 |corr|.
// ===========================================================================
struct BeamSearchResult {
    std::unordered_map<u32, double> distribution;
    double pruning_error;  // 累积裁剪误差上界
    size_t max_state_count;
    std::vector<size_t> round_state_counts;
};

// 在线裁剪辅助函数: 将 map 裁剪到 top-K (by |corr|), 返回被裁剪的 |corr| 总和
inline double prune_map_inplace(std::unordered_map<u32, double>& m, size_t keep) {
    if (m.size() <= keep) return 0.0;
    std::vector<std::pair<u32, double>> items(m.begin(), m.end());
    std::nth_element(items.begin(),
                     items.begin() + static_cast<std::ptrdiff_t>(keep),
                     items.end(),
                     [](const auto& a, const auto& b) {
                         return std::fabs(a.second) > std::fabs(b.second);
                     });
    double pruned_sum = 0.0;
    for (size_t i = keep; i < items.size(); ++i) {
        pruned_sum += std::fabs(items[i].second);
    }
    m.clear();
    for (size_t i = 0; i < keep; ++i) {
        m[items[i].first] = items[i].second;
    }
    return pruned_sum;
}

// 预估总展开量: Σ (fanout_i × |corr_i| 排序后的贡献)
// fanout_i = Π (当前活跃半字节的LAT非零输出数)
// 用于在展开前预裁剪, 避免内存爆炸
inline size_t estimate_total_fanout(
    const std::unordered_map<u32, double>& cur,
    std::unordered_map<u32, std::vector<OneRoundTransition>>& cache,
    size_t beam_width)
{
    size_t total = 0;
    for (const auto& [mask, corr] : cur) {
        const auto& trans = get_one_round_transitions(mask, cache);
        total += trans.size();
        if (total > beam_width * 10) break; // 提前退出
    }
    return total;
}

// 预裁剪: 如果预估展开量超过 beam_width*5, 先裁剪 cur
inline double pre_prune_cur(
    std::unordered_map<u32, double>& cur,
    std::unordered_map<u32, std::vector<OneRoundTransition>>& cache,
    size_t beam_width)
{
    if (cur.size() <= beam_width / 100) return 0.0;  // 很小, 不需要

    size_t est = estimate_total_fanout(cur, cache, beam_width);
    if (est <= beam_width * 5) return 0.0;  // 可接受

    // 保守裁剪: 保留最多 beam_width / 100 个状态
    // 每个状态 fanout ≤ 10^5, 所以 nxt ≤ 10^5 * beam_width/100 = 1000*beam_width
    // 在线裁剪会在 nxt 达到 beam_width 时触发, 所以安全
    size_t keep = beam_width / 200;
    if (keep < 100) keep = 100;
    return prune_map_inplace(cur, keep);
}

inline BeamSearchResult beam_search_dp(
    u32 start_mask, int rounds, size_t beam_width = 200000,
    std::unordered_map<u32, std::vector<OneRoundTransition>>* cache = nullptr)
{
    std::unordered_map<u32, std::vector<OneRoundTransition>> local_cache;
    if (!cache) cache = &local_cache;

    std::unordered_map<u32, double> cur;
    cur.reserve(1024);
    cur[start_mask] = 1.0;

    double total_pruning_error = 0.0;
    size_t max_seen = 1;
    std::vector<size_t> round_counts;

    // 在线裁剪阈值: nxt 到达此阈值时立即裁剪
    const size_t online_limit = beam_width;

    for (int r = 0; r < rounds; ++r) {
        // ★ 预裁剪 cur: 如果预估展开量过大, 先裁剪 cur
        if (cur.size() > beam_width / 200) {
            total_pruning_error += pre_prune_cur(cur, *cache, beam_width);
        }

        std::unordered_map<u32, double> nxt;
        nxt.reserve(online_limit + 1);

        for (const auto& [mask, corr] : cur) {
            const auto& trans = get_one_round_transitions(mask, *cache);
            for (const auto& t : trans) {
                nxt[t.next_mask] += corr * t.corr;
            }
            // ★ 在线裁剪: nxt超阈值时立即砍半
            if (nxt.size() > online_limit) {
                total_pruning_error += prune_map_inplace(nxt, online_limit / 2);
            }
        }

        // 清除极小值
        for (auto it = nxt.begin(); it != nxt.end(); ) {
            if (std::fabs(it->second) <= 1e-20) {
                it = nxt.erase(it);
            } else {
                ++it;
            }
        }

        const size_t n = nxt.size();
        max_seen = std::max(max_seen, n);
        round_counts.push_back(n);

        if (n > beam_width) {
            total_pruning_error += prune_map_inplace(nxt, beam_width);
        }

        cur = std::move(nxt);
    }

    return {std::move(cur), total_pruning_error, max_seen, std::move(round_counts)};
}

// ===========================================================================
// Strategy C: 蒙特卡洛采样
//
// 随机采样 N 个输入 x, 计算 (-1)^{u·x ⊕ v·HS(r,x)} 的平均值.
// 标准误差 = 1/√N (当 |VT| ≪ 1 时).
// ===========================================================================
struct MonteCarloResult {
    double estimate;
    double std_error;     // 标准误差 = σ/√N
    int samples;
};

inline MonteCarloResult monte_carlo_estimate(u32 u, u32 v, int rounds, int num_samples) {
    std::mt19937_64 rng(42);  // 固定种子确保可复现
    long long sum = 0;

    for (int i = 0; i < num_samples; ++i) {
        const u32 x = static_cast<u32>(rng() & 0xFFFFFFFFULL);
        const u32 y = permute(x, rounds);
        const int sign = (parity32(u & x) == parity32(v & y)) ? 1 : -1;
        sum += sign;
    }

    const double estimate = static_cast<double>(sum) / static_cast<double>(num_samples);
    const double variance = (1.0 - estimate * estimate) / static_cast<double>(num_samples);
    const double std_error = std::sqrt(std::max(variance, 0.0));

    return {estimate, std_error, num_samples};
}

// ===========================================================================
// 综合逼近器
//
// 自动选择最优策略:
//   1. 尝试精确稀疏DP (max 500K 状态)
//   2. 若精确不可行, 使用波束搜索 (默认 beam=200K)
//   3. 小r时用蒙特卡洛交叉验证
// ===========================================================================
struct ApproxResult {
    double ve;            // 估计值
    double error_bound;   // 误差上界
    bool is_exact;        // 是否精确
    size_t max_states;    // 中间最大状态数
    std::string method;   // 使用的方法
};

inline ApproxResult estimate_correlation(
    u32 u, u32 v, int rounds,
    size_t beam_width = 200000)
{
    ApproxResult result{};
    result.method = "beam_search";

    // 共享缓存提升性能
    std::unordered_map<u32, std::vector<OneRoundTransition>> cache;

    // 尝试波束搜索 (如果状态数不超过 beam_width 则自动精确)
    auto beam_res = beam_search_dp(u, rounds, beam_width, &cache);
    result.max_states = beam_res.max_state_count;

    auto it = beam_res.distribution.find(v);
    result.ve = (it != beam_res.distribution.end()) ? it->second : 0.0;
    result.error_bound = beam_res.pruning_error;
    result.is_exact = (beam_res.max_state_count <= beam_width);

    if (result.is_exact) {
        result.method = "exact_sparse";
    }

    return result;
}

// ===========================================================================
// 批量搜索: 给定输入掩码, 对 r=1..max_rounds 找出所有正分有效估计
// ===========================================================================
inline std::vector<EstimateEntry> batch_search_input(
    u32 u, int max_rounds,
    size_t beam_width = 200000,
    bool verbose = false)
{
    std::vector<EstimateEntry> entries;
    std::unordered_map<u32, std::vector<OneRoundTransition>> cache;

    std::unordered_map<u32, double> cur;
    cur.reserve(1024);
    cur[u] = 1.0;

    // 在线裁剪阈值
    const size_t online_limit = beam_width;

    // 预裁剪 cur
    double cumulative_pruning_err = 0.0;
    if (cur.size() > beam_width / 200) {
        cumulative_pruning_err += pre_prune_cur(cur, cache, beam_width);
    }

    for (int rounds = 1; rounds <= max_rounds; ++rounds) {
        if (verbose) {
            std::cerr << "  r=" << rounds << " cur_size=" << cur.size() << std::endl;
        }

        std::unordered_map<u32, double> nxt;
        nxt.reserve(online_limit + 1);

        double round_pruning_err = 0.0;

        // 对 cur 中每个状态枚举单轮转移
        for (const auto& [mask, corr] : cur) {
            const auto& trans = get_one_round_transitions(mask, cache);
            for (const auto& t : trans) {
                nxt[t.next_mask] += corr * t.corr;
            }
            // ★ 激进在线裁剪: nxt超阈值时立即砍半
            if (nxt.size() > online_limit) {
                round_pruning_err += prune_map_inplace(nxt, online_limit / 2);
            }
        }

        // 清除极小值
        for (auto it = nxt.begin(); it != nxt.end(); ) {
            if (std::fabs(it->second) <= 1e-20) {
                it = nxt.erase(it);
            } else {
                ++it;
            }
        }

        // 最终裁剪
        if (nxt.size() > beam_width) {
            round_pruning_err += prune_map_inplace(nxt, beam_width);
        }

        cumulative_pruning_err += round_pruning_err;
        const bool is_exact = (round_pruning_err == 0.0 && cumulative_pruning_err == 0.0);

        if (verbose && round_pruning_err > 0.0) {
            std::cerr << "    pruned: " << std::scientific << round_pruning_err
                      << " (cumulative=" << cumulative_pruning_err << ")" << std::endl;
        }

        // 提取有效估计值 (正分条件: |corr| > 4^(-r))
        const double min_abs = 1.0 / std::pow(4.0, rounds);
        for (const auto& [v, corr] : nxt) {
            if (u == 0U || v == 0U) continue;
            if (std::fabs(corr) <= min_abs) continue;

            EstimateEntry entry;
            entry.rounds = rounds;
            entry.u = u;
            entry.v = v;
            entry.ve = corr;
            entry.vt = corr;
            entry.error_bound = cumulative_pruning_err;
            entry.score = compute_score(corr, rounds);
            entry.is_exact = is_exact;
            entries.push_back(entry);
        }

        cur = std::move(nxt);
    }

    return entries;
}

// ===========================================================================
// 输出函数
// ===========================================================================

inline std::string mask_to_hex(u32 value) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(8) << std::setfill('0')
       << std::uppercase << value;
    return ss.str();
}

inline void write_entries_to_file(
    const std::vector<EstimateEntry>& entries,
    const std::string& filepath)
{
    std::ofstream out(filepath, std::ios::out | std::ios::trunc);
    if (!out) {
        std::cerr << "Error: cannot write to " << filepath << std::endl;
        return;
    }

    out << "# Valid estimates generated by Way-2 approximation algorithm\n";
    out << "# Format: @(r, u, v, VE, VT)\n";
    out << "# method: exact_sparse | beam_search\n";
    out << std::fixed << std::setprecision(15);

    for (const auto& e : entries) {
        out << "@(" << e.rounds
            << ", " << mask_to_hex(e.u)
            << ", " << mask_to_hex(e.v)
            << ", " << e.ve
            << ", " << e.vt
            << ")\n";
    }

    out.close();
    std::cout << "Wrote " << entries.size() << " entries to " << filepath << std::endl;
}

inline void write_score_report(
    const std::vector<EstimateEntry>& entries,
    const std::string& filepath)
{
    std::ofstream out(filepath, std::ios::out | std::ios::trunc);
    if (!out) return;

    out << std::fixed << std::setprecision(12);

    // 按轮数分组
    std::unordered_map<int, std::vector<const EstimateEntry*>> grouped;
    for (const auto& e : entries) {
        grouped[e.rounds].push_back(&e);
    }

    std::vector<int> round_keys;
    for (const auto& [r, _] : grouped) round_keys.push_back(r);
    std::sort(round_keys.begin(), round_keys.end());

    double total_score = 0.0;
    int total_count = 0;

    out << "PDF-compliant scoring report (Way-2 approximation)\n";
    out << "Rule: valid iff |VE-VT| <= |VT|*2^(-2r), VE!=0, u!=0, v!=0\n\n";

    for (int r : round_keys) {
        const auto& group = grouped[r];
        out << "=== r = " << r << " ===\n";
        out << "No  u           v           VE                 VT                 Score   Exact?\n";

        double round_sum = 0.0;
        int idx = 0;
        for (const auto* e : group) {
            ++idx;
            out << std::setw(3) << idx << "  "
                << mask_to_hex(e->u) << "  "
                << mask_to_hex(e->v) << "  "
                << std::setw(18) << e->ve << "  "
                << std::setw(18) << e->vt << "  "
                << std::setw(8) << std::setprecision(4) << e->score << "  "
                << (e->is_exact ? "Y" : "N") << "\n";
            round_sum += e->score;
            total_score += e->score;
            ++total_count;
        }

        out << "r=" << r << ": count=" << group.size()
            << ", sum=" << std::setprecision(4) << round_sum
            << ", max=" << (*std::max_element(group.begin(), group.end(),
                    [](auto a, auto b) { return a->score < b->score; }))->score
            << ", min=" << (*std::min_element(group.begin(), group.end(),
                    [](auto a, auto b) { return a->score < b->score; }))->score
            << ", avg=" << (group.empty() ? 0.0 : round_sum / group.size()) << "\n\n";
    }

    out << "Total valid estimates: " << total_count << "\n";
    out << "Total score: " << std::setprecision(4) << total_score << "\n";
    out.close();
}

// ===========================================================================
// 统计摘要
// ===========================================================================
inline void print_summary(const std::vector<EstimateEntry>& entries) {
    if (entries.empty()) {
        std::cout << "No valid entries found.\n";
        return;
    }

    std::unordered_map<int, std::vector<double>> round_scores;
    for (const auto& e : entries) {
        round_scores[e.rounds].push_back(e.score);
    }

    std::vector<int> rounds;
    for (const auto& [r, _] : round_scores) rounds.push_back(r);
    std::sort(rounds.begin(), rounds.end());

    double total = 0.0;
    int exact_count = 0;

    std::cout << "\n========================================\n";
    std::cout << "  Score Summary (Way-2 Approximation)\n";
    std::cout << "========================================\n\n";

    for (int r : rounds) {
        const auto& scores = round_scores[r];
        double sum = 0.0;
        double mx = scores[0], mn = scores[0];
        for (double s : scores) {
            sum += s;
            mx = std::max(mx, s);
            mn = std::min(mn, s);
        }
        total += sum;
        std::cout << "r=" << r << ": " << scores.size() << " entries"
                  << ", sum=" << std::fixed << std::setprecision(4) << sum
                  << ", max=" << mx << ", min=" << mn
                  << ", avg=" << (sum / scores.size()) << "\n";
    }

    for (const auto& e : entries) {
        if (e.is_exact) ++exact_count;
    }

    std::cout << "\nTotal: " << entries.size() << " entries"
              << ", score = " << std::fixed << std::setprecision(4) << total << "\n";
    std::cout << "Exact entries: " << exact_count << "/" << entries.size()
              << " (" << std::fixed << std::setprecision(1)
              << (100.0 * exact_count / entries.size()) << "%)\n";
}

}  // namespace hs
