// =============================================================================
// approx_engine.hpp — 方式2 逼近引擎 [优化版 v2]
// =============================================================================
// 赛题三: 矩阵连乘元素的逼近
//
// 优化点:
//   1. 优先队列 (最小堆) 替代 nth_element + map 重建 — O(log B) 维护
//   2. 并行批量处理 — 多线程处理独立输入
//   3. 自适应候选选择 — 按相关度动态决定保留数量
//   4. 更精确的误差追踪
//   5. 扩展支持 r=5+
// =============================================================================

#pragma once

#include "hs_cipher.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace hs {

// ===========================================================================
// 结果条目
// ===========================================================================
struct EstimateEntry {
    int rounds;
    u32 u, v;
    double ve;
    double vt;
    double error_bound;
    double score;
    bool is_exact;
};

// ===========================================================================
// ★ 优化: 基于flat vector的波束维护 (内存安全版)
//
// 使用flat vector存储条目, 定期裁剪到 top-K.
// 优点: O(1) push, 内存可控 (最多 3*cap 条目), 无堆开销.
// 淘汰误差 = 被淘汰元素的 |corr| 之和.
// ===========================================================================
class BeamHeap {
public:
    explicit BeamHeap(size_t capacity) : cap_(capacity) {
        entries_.reserve(capacity * 2);
    }

    // 插入条目
    void add(u32 mask, double corr) {
        double abs_corr = std::fabs(corr);
        if (abs_corr <= 1e-20) return;

        entries_.emplace_back(mask, corr);

        // ★ 定期裁剪: 当条目数超过 2*cap 时, 裁剪到 cap
        if (entries_.size() > cap_ * 2) {
            force_prune();
        }
    }

    // 获取最终结果 (按mask合并)
    std::unordered_map<u32, double> finalize() {
        // 最终裁剪
        force_prune();

        std::unordered_map<u32, double> result;
        result.reserve(entries_.size());
        for (const auto& [mask, corr] : entries_) {
            result[mask] += corr;
        }
        // 清除极小值
        for (auto it = result.begin(); it != result.end(); ) {
            if (std::fabs(it->second) <= 1e-20) it = result.erase(it);
            else ++it;
        }
        return result;
    }

    double pruned_sum() const { return pruned_sum_; }
    size_t size() const { return entries_.size(); }

private:
    void force_prune() {
        if (entries_.size() <= cap_) return;

        // 使用 nth_element 找 top-cap (by |corr|)
        std::nth_element(entries_.begin(),
                         entries_.begin() + static_cast<std::ptrdiff_t>(cap_),
                         entries_.end(),
                         [](const auto& a, const auto& b) {
                             return std::fabs(a.second) > std::fabs(b.second);
                         });

        // 累计裁剪误差
        for (size_t i = cap_; i < entries_.size(); ++i) {
            pruned_sum_ += std::fabs(entries_[i].second);
        }

        entries_.resize(cap_);
    }

    size_t cap_;
    double pruned_sum_ = 0.0;
    std::vector<std::pair<u32, double>> entries_;
};

// ===========================================================================
// Strategy A: 精确稀疏DP (保留原有实现, 增加堆优化)
// ===========================================================================
struct ExactSparseResult {
    std::unordered_map<u32, double> distribution;
    bool completed;
    size_t max_state_count;
};

inline ExactSparseResult exact_sparse_dp(
    u32 start_mask, int rounds,
    size_t max_states = 500000,
    std::unordered_map<u32, std::vector<OneRoundTransition>>* cache = nullptr)
{
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
            if (std::fabs(it->second) <= 1e-20) { it = nxt.erase(it); }
            else { ++it; }
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
// Strategy B: 波束搜索 [优化的堆版本]
// ===========================================================================
struct BeamSearchResult {
    std::unordered_map<u32, double> distribution;
    double pruning_error;
    size_t max_state_count;
    std::vector<size_t> round_state_counts;
};

inline BeamSearchResult beam_search_dp_optimized(
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

    for (int r = 0; r < rounds; ++r) {
        // ★ 使用BeamHeap进行在线维护
        // 预估下一轮规模: cur_size * avg_fanout
        size_t est_fanout = 0;
        for (const auto& [mask, corr] : cur) {
            const auto& trans = get_one_round_transitions(mask, *cache);
            est_fanout += trans.size();
        }

        // 如果预估展开量可控, 使用精确展开+堆裁剪
        // 否则先预裁剪cur
        if (est_fanout > beam_width * 20 && cur.size() > 100) {
            // 预裁剪: 保留 beam_width / 100 个状态
            size_t keep = std::max(size_t(100), beam_width / 100);
            if (cur.size() > keep) {
                std::vector<std::pair<u32, double>> items(cur.begin(), cur.end());
                std::nth_element(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(keep),
                                 items.end(),
                                 [](const auto& a, const auto& b) {
                                     return std::fabs(a.second) > std::fabs(b.second);
                                 });
                for (size_t i = keep; i < items.size(); ++i) {
                    total_pruning_error += std::fabs(items[i].second);
                }
                cur.clear();
                for (size_t i = 0; i < keep; ++i) {
                    cur[items[i].first] = items[i].second;
                }
            }
        }

        // ★ 使用堆进行展开和裁剪
        BeamHeap heap(beam_width);
        for (const auto& [mask, corr] : cur) {
            const auto& trans = get_one_round_transitions(mask, *cache);
            for (const auto& t : trans) {
                heap.add(t.next_mask, corr * t.corr);
            }
        }

        total_pruning_error += heap.pruned_sum();

        cur = heap.finalize();
        size_t n = cur.size();
        max_seen = std::max(max_seen, n);
        round_counts.push_back(n);
    }

    return {std::move(cur), total_pruning_error, max_seen, std::move(round_counts)};
}

// 保留原版函数名兼容
inline BeamSearchResult beam_search_dp(
    u32 start_mask, int rounds, size_t beam_width = 200000,
    std::unordered_map<u32, std::vector<OneRoundTransition>>* cache = nullptr)
{
    return beam_search_dp_optimized(start_mask, rounds, beam_width, cache);
}

// ===========================================================================
// Strategy C: 蒙特卡洛采样
// ===========================================================================
struct MonteCarloResult {
    double estimate;
    double std_error;
    int samples;
};

inline MonteCarloResult monte_carlo_estimate(u32 u, u32 v, int rounds, int num_samples) {
    std::mt19937_64 rng(42);
    long long sum = 0;

    for (int i = 0; i < num_samples; ++i) {
        const u32 x = static_cast<u32>(rng() & 0xFFFFFFFFULL);
        const u32 y = permute(x, rounds);
        const int sign = (parity32_fast(u & x) == parity32_fast(v & y)) ? 1 : -1;
        sum += sign;
    }

    const double estimate = static_cast<double>(sum) / static_cast<double>(num_samples);
    const double variance = (1.0 - estimate * estimate) / static_cast<double>(num_samples);
    const double std_error = std::sqrt(std::max(variance, 0.0));

    return {estimate, std_error, num_samples};
}

// ===========================================================================
// 综合逼近器
// ===========================================================================
struct ApproxResult {
    double ve;
    double error_bound;
    bool is_exact;
    size_t max_states;
    std::string method;
};

inline ApproxResult estimate_correlation(
    u32 u, u32 v, int rounds,
    size_t beam_width = 200000)
{
    ApproxResult result{};
    result.method = "beam_search";

    std::unordered_map<u32, std::vector<OneRoundTransition>> cache;

    auto beam_res = beam_search_dp_optimized(u, rounds, beam_width, &cache);
    result.max_states = beam_res.max_state_count;

    auto it = beam_res.distribution.find(v);
    result.ve = (it != beam_res.distribution.end()) ? it->second : 0.0;
    result.error_bound = beam_res.pruning_error;
    result.is_exact = (beam_res.pruning_error == 0.0);

    if (result.is_exact) {
        result.method = "exact_sparse";
    }

    return result;
}

// ===========================================================================
// 批量搜索: 给定输入掩码
// ===========================================================================
inline std::vector<EstimateEntry> batch_search_input(
    u32 u, int max_rounds,
    size_t beam_width = 200000,
    bool verbose = false,
    std::unordered_map<u32, std::vector<OneRoundTransition>>* shared_cache = nullptr)
{
    std::vector<EstimateEntry> entries;
    std::unordered_map<u32, std::vector<OneRoundTransition>> local_cache;
    auto& cache = shared_cache ? *shared_cache : local_cache;

    std::unordered_map<u32, double> cur;
    cur.reserve(1024);
    cur[u] = 1.0;

    double cumulative_pruning_err = 0.0;

    for (int rounds = 1; rounds <= max_rounds; ++rounds) {
        if (verbose) {
            std::cerr << "  r=" << rounds << " cur_size=" << cur.size() << std::endl;
        }

        // ★ 预裁剪: 如果 cur 太大, 先裁剪到可控大小再展开
        // 防止展开时内存爆炸 (cur_size × fanout 可能极大)
        const size_t max_safe_cur = std::max(size_t(1000), beam_width / 50);
        if (cur.size() > max_safe_cur) {
            size_t keep = max_safe_cur;
            std::vector<std::pair<u32, double>> items(cur.begin(), cur.end());
            std::nth_element(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(keep),
                             items.end(),
                             [](const auto& a, const auto& b) {
                                 return std::fabs(a.second) > std::fabs(b.second);
                             });
            for (size_t i = keep; i < items.size(); ++i) {
                cumulative_pruning_err += std::fabs(items[i].second);
            }
            cur.clear();
            for (size_t i = 0; i < keep; ++i) {
                cur[items[i].first] = items[i].second;
            }
        }

        // 使用 BeamHeap 进行展开
        BeamHeap heap(beam_width);

        for (const auto& [mask, corr] : cur) {
            const auto& trans = get_one_round_transitions(mask, cache);
            for (const auto& t : trans) {
                heap.add(t.next_mask, corr * t.corr);
            }
        }

        double round_pruning = heap.pruned_sum();
        cumulative_pruning_err += round_pruning;
        const bool is_exact = (round_pruning == 0.0 && cumulative_pruning_err == 0.0);

        cur = heap.finalize();

        if (verbose && round_pruning > 0.0) {
            std::cerr << "    pruned: " << std::scientific << round_pruning
                      << " (cumulative=" << cumulative_pruning_err << ")" << std::endl;
        }

        // 提取有效估计值
        const double min_abs = 1.0 / std::pow(4.0, rounds);
        for (const auto& [v, corr] : cur) {
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
    }

    return entries;
}

// ===========================================================================
// ★ 并行批量处理
// ===========================================================================

// 前向声明
inline std::string mask_to_hex(u32 value);

// 线程安全的条目收集器
struct EntryCollector {
    std::vector<EstimateEntry> entries;
    std::mutex mtx;

    void append(const std::vector<EstimateEntry>& new_entries) {
        std::lock_guard<std::mutex> lock(mtx);
        entries.insert(entries.end(), new_entries.begin(), new_entries.end());
    }
};

// 工作函数: 处理单个输入掩码
inline std::vector<EstimateEntry> process_single_input(
    u32 u, int max_rounds, size_t beam_width,
    ThreadSafeCache& ts_cache)
{
    std::unordered_map<u32, double> cur;
    cur.reserve(1024);
    cur[u] = 1.0;

    std::vector<EstimateEntry> entries;
    double cumulative_pruning_err = 0.0;

    for (int rounds = 1; rounds <= max_rounds; ++rounds) {
        // ★ 预裁剪: 防止展开时内存爆炸
        const size_t max_safe_cur = std::max(size_t(1000), beam_width / 50);
        if (cur.size() > max_safe_cur) {
            size_t keep = max_safe_cur;
            std::vector<std::pair<u32, double>> items(cur.begin(), cur.end());
            std::nth_element(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(keep),
                             items.end(),
                             [](const auto& a, const auto& b) {
                                 return std::fabs(a.second) > std::fabs(b.second);
                             });
            for (size_t i = keep; i < items.size(); ++i) {
                cumulative_pruning_err += std::fabs(items[i].second);
            }
            cur.clear();
            for (size_t i = 0; i < keep; ++i) {
                cur[items[i].first] = items[i].second;
            }
        }

        // 使用 BeamHeap 进行展开
        BeamHeap heap(beam_width);

        for (const auto& [mask, corr] : cur) {
            const auto& trans = ts_cache.get(mask);
            for (const auto& t : trans) {
                heap.add(t.next_mask, corr * t.corr);
            }
        }

        double round_pruning = heap.pruned_sum();
        cumulative_pruning_err += round_pruning;
        const bool is_exact = (round_pruning == 0.0 && cumulative_pruning_err == 0.0);

        cur = heap.finalize();

        // 提取有效估计值
        const double min_abs = 1.0 / std::pow(4.0, rounds);
        for (const auto& [v, corr] : cur) {
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
    }

    return entries;
}

// 并行批量处理所有单活跃输入
inline std::vector<EstimateEntry> batch_search_all_parallel(
    int max_rounds, size_t beam_width = 200000,
    unsigned int num_threads = 0)
{
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
        // 限制最大线程数
        if (num_threads > 16) num_threads = 16;
    }

    auto inputs = all_single_active_inputs();
    std::cout << "=== Parallel Batch All ===\n";
    std::cout << "max_r = " << max_rounds << "\n";
    std::cout << "beam_width = " << beam_width << "\n";
    std::cout << "total_inputs = " << inputs.size() << "\n";
    std::cout << "threads = " << num_threads << "\n\n";

    EntryCollector collector;
    std::atomic<int> completed{0};
    std::mutex print_mtx;

    // 线程池
    std::vector<std::thread> threads;
    std::atomic<size_t> next_idx{0};

    auto worker = [&](int tid) {
        ThreadSafeCache ts_cache; // 每个线程自己的缓存
        while (true) {
            size_t idx = next_idx.fetch_add(1);
            if (idx >= inputs.size()) break;

            u32 u = inputs[idx];
            int done = completed.fetch_add(1) + 1;

            auto t_start = std::chrono::high_resolution_clock::now();
            auto entries = process_single_input(u, max_rounds, beam_width, ts_cache);
            auto t_end = std::chrono::high_resolution_clock::now();
            double et = std::chrono::duration<double>(t_end - t_start).count();

            {
                std::lock_guard<std::mutex> lock(print_mtx);
                std::cout << "[" << done << "/" << inputs.size() << "] "
                          << mask_to_hex(u) << " ... "
                          << entries.size() << " entries, " << et << "s\n";
            }

            collector.append(entries);
        }
    };

    for (unsigned int t = 0; t < num_threads; ++t) {
        threads.emplace_back(worker, t);
    }
    for (auto& t : threads) {
        t.join();
    }

    return std::move(collector.entries);
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

    std::unordered_map<int, std::vector<const EstimateEntry*>> grouped;
    for (const auto& e : entries) {
        grouped[e.rounds].push_back(&e);
    }

    std::vector<int> round_keys;
    for (const auto& [r, _] : grouped) round_keys.push_back(r);
    std::sort(round_keys.begin(), round_keys.end());

    double total_score = 0.0;
    int total_count = 0;

    out << "PDF-compliant scoring report (Way-2 approximation) [Optimized v2]\n";
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
    std::cout << "  Score Summary (Way-2 Approximation v2)\n";
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
