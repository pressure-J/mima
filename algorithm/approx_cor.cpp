/**
 * 方式2: 主导路线枚举逼近算法 (Dominant Trail Enumeration)
 *
 * 赛题三: 矩阵连乘元素的逼近
 * 第十一届(2026年)全国高校密码数学挑战赛
 *
 * 算法思路:
 *   利用线性密码分析中"相关矩阵可分解为线性路线之和"的性质，
 *   通过分支定界法枚举主导路线(相关度较大的路线)，
 *   用这些路线的相关度之和逼近真实相关度。
 *
 * 复杂度: O(B^r) 其中 B 是每轮平均分支数，远小于方式1的 O(2^32)
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdint>
#include <string>
#include <cmath>
#include <algorithm>
#include <map>
#include <queue>
#include <cassert>
#include <fstream>
#include <chrono>
#include <unordered_map>
#include <functional>

using namespace std;

// ============================================================
// 基本类型和常量
// ============================================================
typedef unsigned int uint;
typedef uint64_t ull;

// S盒 (与参考代码一致)
const int Sbox[16] = {0xC, 0x6, 0x9, 0x0, 0x1, 0xA, 0x2, 0xB, 0x3, 0x8, 0x5, 0xD, 0x4, 0xE, 0x7, 0xF};

// ============================================================
// 基本操作
// ============================================================

// F_2上的点积(奇偶性)
inline int dot(uint u, uint y) {
    uint z = u & y;
    z ^= z >> 1;
    z ^= z >> 2;
    z ^= z >> 4;
    z ^= z >> 8;
    z ^= z >> 16;
    return z & 1;
}

// 4-bit nibble点积
inline int nibble_dot(int a, int b) {
    int z = a & b;
    z ^= z >> 1;
    z ^= z >> 2;
    return z & 1;
}

// ============================================================
// S盒线性逼近表 (LAT)
// LAT[a][b] = #{x: a·x = b·S(x)} - 8, 范围 [-8, 8]
// 归一化相关度: lat_norm[a][b] = LAT[a][b] / 16, 范围 [-0.5, 0.5]
// ============================================================
int LAT[16][16];
double LAT_norm[16][16];

void compute_LAT() {
    for (int a = 0; a < 16; ++a) {
        for (int b = 0; b < 16; ++b) {
            int count = 0;
            for (int x = 0; x < 16; ++x) {
                if (nibble_dot(a, x) == nibble_dot(b, Sbox[x]))
                    count++;
            }
            LAT[a][b] = count - 8;
            LAT_norm[a][b] = (double)LAT[a][b] / 8.0;  // corr = (2*count-16)/16 = 2*LAT/16 = LAT/8
        }
    }
}

// ============================================================
// 线性层掩码传播
// ============================================================

/**
 * 线性层: L = MC ∘ SR
 *
 * SR (行移位):
 *   (y0,y1,y2,y3,y4,y5,y6,y7) = (x0,x5,x2,x7,x4,x1,x6,x3)
 *
 * MC (列混合):
 *   y0 = x0⊕x2⊕x3,  y1 = x0
 *   y2 = x1⊕x2,      y3 = x0⊕x2
 *   y4 = x4⊕x6⊕x7,  y5 = x4
 *   y6 = x5⊕x6,      y7 = x4⊕x6
 *
 * 反向掩码传播: c = L^T(b) = SR^T(MC^T(b))
 *   给定输出掩码 b，计算S盒输出处的掩码 c
 *   c = [b0⊕b1⊕b3, b6, b0⊕b2⊕b3, b4, b4⊕b5⊕b7, b2, b4⊕b6⊕b7, b0]
 *
 * 正向掩码传播: 给定S盒输出掩码 c，计算下一轮输入掩码 b
 *   b = (MC^{-T} ∘ SR^{-T})(c)
 */

// 反向传播: b → c (给定轮输出掩码b, 得到S盒输出掩码c)
void mask_backward(const int b[8], int c[8]) {
    c[0] = b[0] ^ b[1] ^ b[3];    // b0⊕b1⊕b3
    c[1] = b[6];                    // b6
    c[2] = b[0] ^ b[2] ^ b[3];    // b0⊕b2⊕b3
    c[3] = b[4];                    // b4
    c[4] = b[4] ^ b[5] ^ b[7];    // b4⊕b5⊕b7
    c[5] = b[2];                    // b2
    c[6] = b[4] ^ b[6] ^ b[7];    // b4⊕b6⊕b7
    c[7] = b[0];                    // b0
}

// 正向传播: c → b (给定S盒输出掩码c, 得到下一轮输入掩码b)
// b = (MC^{-T} ∘ SR^{-T})(c)
// 推导结果: b = [c7, c0⊕c2⊕c5, c5, c2⊕c5⊕c7, c3, c1⊕c4⊕c6, c1, c1⊕c3⊕c6]
void mask_forward(const int c[8], int b[8]) {
    b[0] = c[7];
    b[1] = c[0] ^ c[2] ^ c[5];
    b[2] = c[5];
    b[3] = c[2] ^ c[5] ^ c[7];
    b[4] = c[3];
    b[5] = c[1] ^ c[4] ^ c[6];
    b[6] = c[1];
    b[7] = c[1] ^ c[3] ^ c[6];
}

// uint ↔ nibble array 转换
void uint_to_nibbles(uint x, int nib[8]) {
    for (int i = 0; i < 8; ++i)
        nib[i] = (x >> (28 - 4*i)) & 0xF;
}

uint nibbles_to_uint(const int nib[8]) {
    uint res = 0;
    for (int i = 0; i < 8; ++i)
        res |= ((uint)nib[i]) << (28 - 4*i);
    return res;
}

// ============================================================
// 单轮相关度计算
// c_R(a, b) = Π_{j=0}^{7} lat_norm[a_j][c_j]  where c = L^T(b)
// ============================================================
double single_round_corr(uint a, uint b) {
    int na[8], nb[8], nc[8];
    uint_to_nibbles(a, na);
    uint_to_nibbles(b, nb);
    mask_backward(nb, nc);

    double corr = 1.0;
    for (int j = 0; j < 8; ++j) {
        corr *= LAT_norm[na[j]][nc[j]];
    }
    return corr;
}

// 计算单轮相关度并返回S盒输出掩码
double single_round_corr_with_c(uint a, uint b, int c_out[8]) {
    int na[8], nb[8];
    uint_to_nibbles(a, na);
    uint_to_nibbles(b, nb);
    mask_backward(nb, c_out);

    double corr = 1.0;
    for (int j = 0; j < 8; ++j) {
        corr *= LAT_norm[na[j]][c_out[j]];
    }
    return corr;
}

// ============================================================
// 方式1: 精确计算 (参考实现, O(2^32))
// ============================================================

// HS(r) 置换
uint perm(uint x, int R) {
    int state[8];
    uint_to_nibbles(x, state);

    for (int r = 0; r < R; ++r) {
        // SC: S盒层
        for (int i = 0; i < 8; ++i)
            state[i] = Sbox[state[i]];

        // SR: 行移位
        int t0 = state[0], t1 = state[5], t2 = state[2], t3 = state[7];
        int t4 = state[4], t5 = state[1], t6 = state[6], t7 = state[3];

        // MC: 列混合
        state[0] = t0 ^ t2 ^ t3;
        state[1] = t0;
        state[2] = t1 ^ t2;
        state[3] = t0 ^ t2;
        state[4] = t4 ^ t6 ^ t7;
        state[5] = t4;
        state[6] = t5 ^ t6;
        state[7] = t4 ^ t6;
    }

    return nibbles_to_uint(state);
}

// 精确计算相关度 (方式1)
double computeCor_exact(uint u, uint v, int R) {
    long long count = 0;
    const uint64_t total = 1ULL << 32;

    for (uint64_t x = 0; x < total; ++x) {
        uint y = perm((uint)x, R);
        if (dot(u, (uint)x) == dot(v, y))
            count++;
        else
            count--;
    }

    return (double)count / (double)total;
}

// ============================================================
// 方式2: 主导路线枚举逼近算法
// ============================================================

/**
 * 路线结构: 存储一条r轮路线
 *   masks[0..r]: 每轮后的掩码 (masks[0] = u, masks[r] = v)
 *   corr: 路线的相关度 (每轮相关度之积)
 */
struct Trail {
    vector<uint> masks;  // masks[0]=u, masks[1], ..., masks[r]=v
    double corr;         // 路线相关度
};

// 用于分支定界的节点
struct SearchNode {
    vector<uint> masks;  // 已确定的掩码
    double corr_so_far;  // 累积相关度
    int depth;           // 已完成轮数
    double upper_bound;  // 上界 (用于优先级队列)

    bool operator<(const SearchNode& other) const {
        return abs(upper_bound) < abs(other.upper_bound);  // 按上界绝对值降序
    }
};

/**
 * 为了高效计算上界, 预计算每轮可能的"最佳相关度乘积"
 *
 * 思路: 对每个可能的非零S盒输入掩码, 记录最大的 |LAT_entry|
 * 上界 = |corr_so_far| × Π remaining_rounds (max_round_corr_factor)
 */

// 每个轮次的最大相关度扩大因子
// max_per_sbox[a]: 对输入掩码a, 输出掩码的最大|LAT_norm[a][b]|
double max_per_sbox[16];

void precompute_max_factors() {
    for (int a = 0; a < 16; ++a) {
        double mx = 0.0;
        for (int b = 0; b < 16; ++b) {
            mx = max(mx, abs(LAT_norm[a][b]));
        }
        max_per_sbox[a] = mx;
    }
}

// 估算从当前掩码出发，剩余轮数的最大可能相关度上界
double estimate_upper_bound(const int na[8], int remaining_rounds) {
    double ub = 1.0;
    for (int j = 0; j < 8; ++j) {
        if (na[j] != 0) {
            ub *= max_per_sbox[na[j]];
        }
        // 如果 na[j]==0, 则只有输出也为0时才有非零相关度,
        // lat_norm[0][0]=1, 但考虑到线性层可能改变, 保守估计为1
    }
    return pow(ub, remaining_rounds);
}

/**
 * 枚举从输入掩码a出发，一轮后所有可能的输出掩码b及其相关度
 * 只返回 |corr| >= threshold 的结果
 */
void enumerate_one_round(uint a, double threshold,
                         vector<pair<uint, double>>& results) {
    int na[8];
    uint_to_nibbles(a, na);

    // 收集每个S盒可能的输出掩码
    vector<vector<int>> possible_c(8);
    for (int j = 0; j < 8; ++j) {
        if (na[j] == 0) {
            // 输入掩码为0时, 只有输出也为0才非零
            possible_c[j].push_back(0);
        } else {
            // 输入掩码非零时, 所有16个输出都可能, 但很多相关度很小
            for (int cj = 0; cj < 16; ++cj) {
                if (abs(LAT_norm[na[j]][cj]) > 0.0)
                    possible_c[j].push_back(cj);
            }
        }
    }

    // 递归枚举所有组合 (带计数限制防爆栈)
    int c_cur[8], b_out[8];
    int max_enumerations = 1000000;  // 最大枚举数，防指数爆炸
    int enum_count = 0;

    function<void(int, double)> dfs =
        [&](int pos, double corr_prod) {
        if (enum_count >= max_enumerations) return;

        // 基于上界的剪枝
        if (abs(corr_prod) > 0 && pos > 0) {
            double max_remain = 1.0;
            for (int k = pos; k < 8; ++k)
                max_remain *= max_per_sbox[na[k]];
            if (abs(corr_prod) * max_remain < threshold)
                return;
        }

        if (pos == 8) {
            enum_count++;
            if (abs(corr_prod) >= threshold) {
                mask_forward(c_cur, b_out);
                uint b = nibbles_to_uint(b_out);
                results.push_back({b, corr_prod});
            }
            return;
        }

        // 按LAT绝对值降序遍历，先访问高相关度分支
        vector<pair<int, double>> sorted_candidates;
        for (int cj : possible_c[pos])
            sorted_candidates.push_back({cj, abs(LAT_norm[na[pos]][cj])});
        sort(sorted_candidates.begin(), sorted_candidates.end(),
             [](auto& p1, auto& p2) { return p1.second > p2.second; });

        for (auto& [cj, _] : sorted_candidates) {
            c_cur[pos] = cj;
            dfs(pos + 1, corr_prod * LAT_norm[na[pos]][cj]);
        }
    };

    dfs(0, 1.0);
}

/**
 * 方式2主体: 主导路线枚举 + 分支定界
 *
 * @param u: 输入掩码
 * @param v: 输出掩码
 * @param r: 轮数
 * @param threshold: 路线相关度阈值 (低于此值的路线被剪枝)
 * @return: 估计的相关度 VE
 */
double approx_cor_trail(uint u, uint v, int r, double threshold = 1e-10) {
    if (r == 0) {
        return (u == v) ? 1.0 : 0.0;
    }

    // 对于r=1, 直接计算
    if (r == 1) {
        return single_round_corr(u, v);
    }

    // 双向搜索: 从u向前, 从v向后, 在中间汇合
    // 对于一般情况, 使用单向分支定界搜索

    // 预计算各轮可能的中间掩码
    // 使用队列进行BFS搜索

    // 存储到达每轮各掩码的累积相关度
    // cur_layer[mask] = 累积相关度
    unordered_map<uint, double> cur_layer;
    cur_layer[u] = 1.0;

    for (int round = 0; round < r; ++round) {
        unordered_map<uint, double> next_layer;

        double round_threshold = threshold / pow(16.0, (double)(r - round - 1));
        // 动态调整阈值: 越接近终点, 越放宽

        for (auto& [mask, corr] : cur_layer) {
            vector<pair<uint, double>> next_masks;
            enumerate_one_round(mask, round_threshold / max(1.0, abs(corr)), next_masks);

            for (auto& [next_mask, trans_corr] : next_masks) {
                double new_corr = corr * trans_corr;
                next_layer[next_mask] += new_corr;
            }
        }

        cur_layer = move(next_layer);

        // 剪枝: 保留 top-K 条目
        if (cur_layer.size() > 100000) {
            vector<pair<double, uint>> sorted;
            for (auto& [mask, corr] : cur_layer) {
                sorted.push_back({abs(corr), mask});
            }
            sort(sorted.rbegin(), sorted.rend());

            unordered_map<uint, double> pruned;
            for (size_t i = 0; i < min((size_t)50000, sorted.size()); ++i) {
                pruned[sorted[i].second] = cur_layer[sorted[i].second];
            }
            cur_layer = move(pruned);
        }
    }

    auto it = cur_layer.find(v);
    if (it != cur_layer.end())
        return it->second;
    return 0.0;
}

/**
 * 方式2增强版: 使用优先队列进行启发式搜索
 * 确保找到主导路线, 同时尽早剪枝
 */
double approx_cor_enhanced(uint u, uint v, int r, int top_k = 100, double min_corr = 1e-8) {
    if (r == 0) return (u == v) ? 1.0 : 0.0;
    if (r == 1) return single_round_corr(u, v);

    // 使用优先队列按相关度绝对值排序
    priority_queue<SearchNode> pq;

    SearchNode root;
    root.masks.push_back(u);
    root.corr_so_far = 1.0;
    root.depth = 0;

    int na_root[8];
    uint_to_nibbles(u, na_root);
    root.upper_bound = estimate_upper_bound(na_root, r);
    pq.push(root);

    double result = 0.0;
    int trails_found = 0;

    while (!pq.empty() && trails_found < top_k * 10) {
        SearchNode node = pq.top();
        pq.pop();

        // 如果上界已经小于阈值, 跳过
        if (abs(node.upper_bound) < min_corr) continue;

        if (node.depth == r) {
            // 到达终点
            uint final_mask = node.masks.back();
            if (final_mask == v) {
                result += node.corr_so_far;
                trails_found++;
            }
            continue;
        }

        // 枚举下一轮
        uint cur_mask = node.masks.back();
        vector<pair<uint, double>> next_masks;

        double local_threshold = min_corr / max(1.0, abs(node.corr_so_far));
        enumerate_one_round(cur_mask, local_threshold, next_masks);

        for (auto& [next_mask, trans_corr] : next_masks) {
            double new_corr = node.corr_so_far * trans_corr;

            int na[8];
            uint_to_nibbles(next_mask, na);
            double ub = abs(new_corr) * estimate_upper_bound(na, r - node.depth - 1);

            if (ub >= min_corr) {
                SearchNode child;
                child.masks = node.masks;
                child.masks.push_back(next_mask);
                child.corr_so_far = new_corr;
                child.depth = node.depth + 1;
                child.upper_bound = ub;
                pq.push(child);
            }
        }
    }

    return result;
}

/**
 * 方式2最终版: 稀疏矩阵迭代法
 * 对每一轮, 维护输入掩码到相关度的稀疏映射
 * 利用阈值的动态调整来控制复杂度
 */
double approx_cor_sparse(uint u, uint v, int r, int beam_width = 50000) {
    if (r == 0) return (u == v) ? 1.0 : 0.0;
    if (r == 1) return single_round_corr(u, v);

    // cur[mask] = 累积相关度
    unordered_map<uint, double> cur;
    cur[u] = 1.0;

    for (int round = 0; round < r; ++round) {
        unordered_map<uint, double> nxt;

        // 动态阈值: 根据当前轮数和条目数调整
        double dyn_threshold = 1e-12;
        if (cur.size() > 1000) {
            // 找出中位数相关度作为参考
            vector<double> abs_corrs;
            for (auto& [m, c] : cur) abs_corrs.push_back(abs(c));
            sort(abs_corrs.begin(), abs_corrs.end());
            if ((int)abs_corrs.size() > beam_width) {
                dyn_threshold = abs_corrs[abs_corrs.size() - beam_width];
            }
        }

        int count = 0;
        for (auto& [mask, corr] : cur) {
            if (abs(corr) < dyn_threshold && count > beam_width) continue;
            count++;

            vector<pair<uint, double>> next_vec;
            // 动态限制每掩码枚举数，避免单轮组合爆炸
            double effective_threshold = dyn_threshold / max(1.0, abs(corr));
            effective_threshold = max(effective_threshold, 1e-10);
            enumerate_one_round(mask, effective_threshold, next_vec);

            for (auto& [nm, tc] : next_vec) {
                nxt[nm] += corr * tc;
            }
        }

        cur = move(nxt);

        // Beam pruning
        if (cur.size() > (size_t)beam_width) {
            vector<pair<double, uint>> scored;
            for (auto& [m, c] : cur)
                scored.push_back({abs(c), m});
            sort(scored.rbegin(), scored.rend());

            unordered_map<uint, double> pruned;
            for (int i = 0; i < beam_width; ++i)
                pruned[scored[i].second] = cur[scored[i].second];
            cur = move(pruned);
        }
    }

    auto it = cur.find(v);
    return (it != cur.end()) ? it->second : 0.0;
}

// ============================================================
// 验证与评估
// ============================================================

struct EvalResult {
    uint u, v;
    int r;
    double vt;   // 真实值
    double ve;   // 估计值
    double abs_err;
    double rel_bound;  // 允许的相对误差上界 = |VT| × 2^(-2r)
    bool valid;  // 是否满足有效条件
    double score; // 单条得分
};

// 检查是否为有效估计值
bool check_valid(double ve, double vt, uint u, uint v, int r) {
    if (ve == 0.0) return false;
    if (u == 0) return false;
    if (v == 0) return false;
    double bound = abs(vt) * pow(2.0, -2.0 * r);
    return abs(ve - vt) <= bound;
}

// 计算单条得分
double compute_score(double ve, int r) {
    if (ve == 0.0) return -1e9;
    return log2(pow(2.0, 2.0 * r) * abs(ve));
}

// ============================================================
// 主程序
// ============================================================

void print_usage() {
    cout << "使用方法:" << endl;
    cout << "  approx_cor.exe <mode> [参数]" << endl;
    cout << endl;
    cout << "模式:" << endl;
    cout << "  1 u v r  -- 精确计算 (方式1, O(2^32))" << endl;
    cout << "  2 u v r  -- 稀疏矩阵迭代逼近 (方式2)" << endl;
    cout << "  3 u v r  -- 主导路线枚举逼近 (方式2增强版)" << endl;
    cout << "  batch r n  -- 批量测试: r轮, 随机n组(u,v)" << endl;
    cout << "  eval r file  -- 从文件读取(u,v)对进行评估" << endl;
    cout << "  lat  -- 打印S盒LAT表" << endl;
    cout << endl;
    cout << "例: approx_cor.exe 2 0x000ee0f0 0x08088880 2" << endl;
}

int main(int argc, char* argv[]) {
    // 预计算
    compute_LAT();
    precompute_max_factors();

    if (argc < 2) {
        print_usage();
        return 0;
    }

    string mode = argv[1];

    if (mode == "lat") {
        cout << "=== S盒线性逼近表 (LAT) ===" << endl;
        cout << "     ";
        for (int b = 0; b < 16; ++b)
            cout << setw(4) << hex << b;
        cout << dec << endl;
        for (int a = 0; a < 16; ++a) {
            cout << setw(2) << hex << a << ": ";
            for (int b = 0; b < 16; ++b) {
                cout << setw(4) << LAT[a][b];
            }
            cout << endl;
        }
        cout << dec << endl;
        return 0;
    }

    if (mode == "1" && argc >= 5) {
        uint u = stoul(argv[2], nullptr, 16);
        uint v = stoul(argv[3], nullptr, 16);
        int r = atoi(argv[4]);

        cout << "=== 方式1: 精确计算 ===" << endl;
        cout << "r=" << r << " u=0x" << hex << setw(8) << setfill('0') << u;
        cout << " v=0x" << setw(8) << setfill('0') << v << dec << endl;

        auto start = chrono::high_resolution_clock::now();
        double vt = computeCor_exact(u, v, r);
        auto end = chrono::high_resolution_clock::now();

        auto duration = chrono::duration_cast<chrono::milliseconds>(end - start).count();

        cout << "真实相关度 VT = " << setprecision(12) << vt << endl;
        cout << "log2(|VT|) = " << log2(abs(vt)) << endl;
        cout << "计算时间: " << duration << " ms" << endl;
        return 0;
    }

    if (mode == "2" && argc >= 5) {
        uint u = stoul(argv[2], nullptr, 16);
        uint v = stoul(argv[3], nullptr, 16);
        int r = atoi(argv[4]);

        cout << "=== 方式2: 稀疏矩阵迭代逼近 ===" << endl;
        cout << "r=" << r << " u=0x" << hex << setw(8) << setfill('0') << u;
        cout << " v=0x" << setw(8) << setfill('0') << v << dec << endl;

        auto start = chrono::high_resolution_clock::now();
        double ve = approx_cor_sparse(u, v, r);
        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - start).count();

        cout << "估计相关度 VE = " << setprecision(12) << ve << endl;
        cout << "log2(|VE|) = " << log2(abs(ve)) << endl;
        cout << "计算时间: " << duration << " ms" << endl;

        // 如果r较小, 同时计算真实值进行对比
        if (r <= 2) {
            cout << endl << "计算真实值进行对比..." << endl;
            double vt = computeCor_exact(u, v, r);
            cout << "真实值 VT = " << setprecision(12) << vt << endl;
            cout << "误差 |VE-VT| = " << abs(ve - vt) << endl;
            cout << "允许误差 ≤ |VT|×2^(-2r) = " << abs(vt) * pow(2.0, -2.0*r) << endl;
            if (check_valid(ve, vt, u, v, r)) {
                cout << "✓ 有效估计值 (满足精度要求)" << endl;
                cout << "得分 = " << compute_score(ve, r) << endl;
            } else {
                cout << "✗ 无效估计值 (不满足精度要求)" << endl;
            }
        }
        return 0;
    }

    if (mode == "3" && argc >= 5) {
        uint u = stoul(argv[2], nullptr, 16);
        uint v = stoul(argv[3], nullptr, 16);
        int r = atoi(argv[4]);

        cout << "=== 方式2增强版: 主导路线枚举 ===" << endl;
        cout << "r=" << r << " u=0x" << hex << setw(8) << setfill('0') << u;
        cout << " v=0x" << setw(8) << setfill('0') << v << dec << endl;

        auto start = chrono::high_resolution_clock::now();
        double ve = approx_cor_enhanced(u, v, r);
        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - start).count();

        cout << "估计相关度 VE = " << setprecision(12) << ve << endl;
        cout << "log2(|VE|) = " << log2(abs(ve)) << endl;
        cout << "计算时间: " << duration << " ms" << endl;
        return 0;
    }

    if (mode == "batch" && argc >= 4) {
        int r = atoi(argv[2]);
        int n = atoi(argv[3]);

        cout << "=== 批量测试 r=" << r << " n=" << n << " ===" << endl;

        // 使用固定的测试向量 (保证可重现)
        // 实际中应使用随机种子
        vector<pair<uint,uint>> test_vecs;

        // 生成具有不同活跃S盒数量的测试向量
        // 单活跃S盒
        for (int pos = 0; pos < 8 && test_vecs.size() < (size_t)n; ++pos) {
            for (int mask = 1; mask < 16 && test_vecs.size() < (size_t)n; ++mask) {
                uint u = ((uint)mask) << (28 - 4*pos);
                for (int vmask = 1; vmask < 16 && test_vecs.size() < (size_t)n; ++vmask) {
                    uint v = ((uint)vmask) << (28 - 4*pos);
                    test_vecs.push_back({u, v});
                }
            }
        }

        // 双活跃S盒
        for (int p1 = 0; p1 < 8 && test_vecs.size() < (size_t)n; ++p1) {
            for (int p2 = p1+1; p2 < 8 && test_vecs.size() < (size_t)n; ++p2) {
                uint u = ((uint)0xF) << (28 - 4*p1) | ((uint)0xF) << (28 - 4*p2);
                for (int vm = 1; vm < 16 && test_vecs.size() < (size_t)n; ++vm) {
                    uint v = ((uint)vm) << (28 - 4*p1);
                    test_vecs.push_back({u, v});
                }
            }
        }

        cout << "生成测试向量: " << test_vecs.size() << " 组" << endl;

        vector<EvalResult> results;
        int valid_count = 0;

        for (size_t i = 0; i < test_vecs.size() && i < (size_t)n; ++i) {
            auto [u, v] = test_vecs[i];

            cout << "\r测试进度: " << (i+1) << "/" << min((size_t)n, test_vecs.size()) << flush;

            double vt = computeCor_exact(u, v, r);
            double ve = approx_cor_sparse(u, v, r);

            EvalResult er;
            er.u = u; er.v = v; er.r = r;
            er.vt = vt; er.ve = ve;
            er.abs_err = abs(ve - vt);
            er.rel_bound = abs(vt) * pow(2.0, -2.0*r);
            er.valid = check_valid(ve, vt, u, v, r);
            er.score = er.valid ? compute_score(ve, r) : -1e9;

            if (er.valid) valid_count++;
            results.push_back(er);
        }
        cout << endl;

        // 输出结果
        cout << endl << "=== 批量测试结果 (r=" << r << ") ===" << endl;
        cout << "总测试数: " << results.size() << endl;
        cout << "有效估计值: " << valid_count << " (" << 100.0*valid_count/results.size() << "%)" << endl;
        cout << endl;

        // 输出有效估计值列表
        cout << "有效估计值列表:" << endl;
        cout << setw(6) << "序号" << setw(12) << "u" << setw(12) << "v";
        cout << setw(16) << "VT" << setw(16) << "VE";
        cout << setw(14) << "|VE-VT|" << setw(14) << "允许误差" << setw(10) << "得分" << endl;
        cout << string(100, '-') << endl;

        int idx = 0;
        for (auto& er : results) {
            if (er.valid) {
                idx++;
                cout << setw(4) << idx
                     << " 0x" << hex << setw(8) << setfill('0') << er.u
                     << " 0x" << setw(8) << setfill('0') << er.v << dec
                     << setw(16) << setprecision(10) << er.vt
                     << setw(16) << setprecision(10) << er.ve
                     << setw(14) << setprecision(4) << er.abs_err
                     << setw(14) << setprecision(4) << er.rel_bound
                     << setw(10) << setprecision(4) << er.score
                     << endl;
            }
        }

        // 保存到文件
        ofstream fout("batch_results_r" + to_string(r) + ".txt");
        fout << "=== 批量测试结果 (r=" << r << ") ===" << endl;
        fout << "总测试数: " << results.size() << endl;
        fout << "有效估计值: " << valid_count << endl;
        fout << endl;
        fout << "序号\tu\tv\tVT\tVE\t|VE-VT|\t允许误差\t得分" << endl;
        idx = 0;
        for (auto& er : results) {
            if (er.valid) {
                idx++;
                fout << idx << "\t0x" << hex << setw(8) << setfill('0') << er.u
                     << "\t0x" << setw(8) << setfill('0') << er.v << dec
                     << "\t" << setprecision(10) << er.vt
                     << "\t" << setprecision(10) << er.ve
                     << "\t" << er.abs_err
                     << "\t" << er.rel_bound
                     << "\t" << er.score << endl;
            }
        }
        fout.close();
        cout << endl << "结果已保存至 batch_results_r" << r << ".txt" << endl;

        return 0;
    }

    print_usage();
    return 0;
}
