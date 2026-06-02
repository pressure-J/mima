// =============================================================================
// main.cpp — 方式2逼近算法CLI
// =============================================================================
// 赛题三: 矩阵连乘元素的逼近
// 第十一届(2026)全国高校密码数学挑战赛
//
// 用法:
//   approx_cor estimate <u> <v> <r> [--beam N] [--mc N]
//       对指定(u,v,r)计算估计值VE
//
//   approx_cor exact <u> <v> <r>
//       精确稀疏DP (仅当状态数可控时可用)
//
//   approx_cor batch-input <u> <max_r> [--beam N] [--output dir]
//       对单个输入掩码, 搜索r=1..max_r的所有正分估计
//
//   approx_cor batch-all <max_r> [--beam N] [--output dir]
//       对所有120个单活跃输入掩码执行批量搜索
//
//   approx_cor batch-position <max_r> <pos> [--beam N] [--output dir]
//       对指定位置(0-7)的所有15个单活跃输入掩码执行批量搜索
//
//   approx_cor verify <u> <v> <r> [--mc N]
//       使用蒙特卡洛交叉验证估计值
//
//   approx_cor brute <u> <v> <r>
//       暴力枚举方式1 (仅用于验证, 需 2^32 次计算)
//
//   approx_cor info
//       打印S盒和LAT信息
// =============================================================================

#include "hs_cipher.hpp"
#include "approx_engine.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace hs {
// 全局LAT单例定义
LatTable g_lat;
}  // namespace hs

using namespace hs;

// ---------------------------------------------------------------------------
// 计时器
// ---------------------------------------------------------------------------
struct Timer {
    using clock = std::chrono::high_resolution_clock;
    clock::time_point start = clock::now();
    double elapsed() const {
        return std::chrono::duration<double>(clock::now() - start).count();
    }
};

// ---------------------------------------------------------------------------
// 打印用法
// ---------------------------------------------------------------------------
void print_usage() {
    std::cout << R"(Usage:
  approx_cor estimate <u> <v> <r> [--beam N] [--mc N]
      Estimate M(r)[v,u] using Way-2 approximation

  approx_cor exact <u> <v> <r>
      Exact sparse DP (only when state space is manageable)

  approx_cor batch-input <u> <max_r> [--beam N] [--output dir]
      Find all positive-score estimates for a single input mask

  approx_cor batch-all <max_r> [--beam N] [--output dir]
      Find all positive-score estimates for all 120 single-active inputs

  approx_cor batch-position <max_r> <pos> [--beam N] [--output dir]
      Find all positive-score estimates for one nibble position (0-7)

  approx_cor verify <u> <v> <r> [--mc N]
      Cross-verify estimate using Monte Carlo

  approx_cor brute <u> <v> <r>
      Brute-force Way-1 computation (2^32 ops, for verification only)

  approx_cor info
      Print S-box and LAT information

Options:
  --beam N     Beam width for beam search (default: 200000)
  --mc N       Monte Carlo sample count (default: 1000000)
  --output dir Output directory (default: ../results)

Examples:
  approx_cor estimate 0x20000000 0x00000888 2
  approx_cor batch-all 3 --beam 100000 --output ./results
  approx_cor batch-position 4 0 --output ./results/pos0
)";
}

// ---------------------------------------------------------------------------
// info 命令: 打印S盒和LAT摘要
// ---------------------------------------------------------------------------
void cmd_info() {
    std::cout << "S-box: ";
    for (int i = 0; i < 16; ++i) {
        std::cout << std::hex << std::uppercase << SBOX[static_cast<size_t>(i)] << " ";
    }
    std::cout << std::dec << "\n\n";

    std::cout << "LAT: max absolute value = "
              << [&]() {
                     int mx = 0;
                     for (int a = 0; a < 16; ++a)
                         for (int b = 0; b < 16; ++b)
                             mx = std::max(mx, std::abs(g_lat.raw[a][b]));
                     return mx;
                 }()
              << " (corr = " << [&]() {
                     int mx = 0;
                     for (int a = 0; a < 16; ++a)
                         for (int b = 0; b < 16; ++b)
                             mx = std::max(mx, std::abs(g_lat.raw[a][b]));
                     return static_cast<double>(mx) / 8.0;
                 }()
              << ")\n";

    int nonzero_count = 0;
    int high_corr_count = 0;
    for (int a = 0; a < 16; ++a) {
        for (int b = 0; b < 16; ++b) {
            if (g_lat.raw[a][b] != 0) {
                ++nonzero_count;
                if (std::abs(g_lat.raw[a][b]) >= 4) ++high_corr_count;
            }
        }
    }
    std::cout << "Non-zero LAT entries: " << nonzero_count << "/256\n";
    std::cout << "Entries with |corr| >= 0.5: " << high_corr_count << "\n";

    // 每个输入掩码的fan-out
    std::cout << "\nFan-out per input mask:\n";
    for (int a = 1; a < 16; ++a) {
        std::cout << "  a=0x" << std::hex << a << std::dec
                  << ": " << g_lat.nonzero[a].size() << " outputs, max|corr|=";
        double mx = 0;
        for (const auto& [b, c] : g_lat.nonzero[a]) {
            mx = std::max(mx, std::fabs(c));
        }
        std::cout << mx << "\n";
    }
}

// ---------------------------------------------------------------------------
// estimate 命令
// ---------------------------------------------------------------------------
void cmd_estimate(int argc, char* argv[]) {
    if (argc < 5) { print_usage(); return; }

    u32 u = static_cast<u32>(std::stoul(argv[2], nullptr, 16));
    u32 v = static_cast<u32>(std::stoul(argv[3], nullptr, 16));
    int rounds = std::stoi(argv[4]);

    size_t beam_width = 200000;
    int mc_samples = 0;

    for (int i = 5; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--beam" && i + 1 < argc) {
            beam_width = std::stoul(argv[++i]);
        } else if (arg == "--mc" && i + 1 < argc) {
            mc_samples = std::stoi(argv[++i]);
        }
    }

    std::cout << "=== Way-2 Approximation ===\n";
    std::cout << "u  = " << mask_to_hex(u) << " (active nibbles: "
              << active_nibble_count(u) << ")\n";
    std::cout << "v  = " << mask_to_hex(v) << "\n";
    std::cout << "r  = " << rounds << "\n";
    std::cout << "beam_width = " << beam_width << "\n\n";

    Timer timer;
    auto result = estimate_correlation(u, v, rounds, beam_width);
    double elapsed = timer.elapsed();

    std::cout << std::fixed << std::setprecision(15);
    std::cout << "VE  = " << result.ve << "\n";
    std::cout << "Error bound = " << result.error_bound << "\n";
    std::cout << "Method: " << result.method << "\n";
    std::cout << "Max states: " << result.max_states << "\n";
    std::cout << "Is exact: " << (result.is_exact ? "YES" : "NO (beam pruning applied)")
              << "\n";
    std::cout << "Time: " << elapsed << "s\n";

    if (result.ve != 0.0) {
        double score = compute_score(result.ve, rounds);
        std::cout << "Score = " << std::setprecision(4) << score << "\n";

        // 检查有效性
        bool valid = (result.ve != 0.0 && u != 0U && v != 0U);
        if (valid) {
            double threshold = std::fabs(result.ve) * std::pow(2.0, -2.0 * rounds);
            std::cout << "Threshold |VE|*2^(-2r) = " << threshold << "\n";
            std::cout << "Error bound <= threshold? "
                      << (result.error_bound <= threshold ? "YES" : "NO (increase beam)")
                      << "\n";
        }
    }

    // 可选蒙特卡洛验证
    if (mc_samples > 0) {
        std::cout << "\n--- Monte Carlo Verification (" << mc_samples
                  << " samples) ---\n";
        Timer mc_timer;
        auto mc = monte_carlo_estimate(u, v, rounds, mc_samples);
        std::cout << "MC estimate = " << mc.estimate << "\n";
        std::cout << "Std error   = " << mc.std_error << "\n";
        std::cout << "|VE - MC|   = " << std::fabs(result.ve - mc.estimate) << "\n";
        std::cout << "Time: " << mc_timer.elapsed() << "s\n";
    }
}

// ---------------------------------------------------------------------------
// exact 命令: 精确稀疏DP
// ---------------------------------------------------------------------------
void cmd_exact(int argc, char* argv[]) {
    if (argc < 5) { print_usage(); return; }

    u32 u = static_cast<u32>(std::stoul(argv[2], nullptr, 16));
    u32 v = static_cast<u32>(std::stoul(argv[3], nullptr, 16));
    int rounds = std::stoi(argv[4]);

    std::cout << "=== Exact Sparse DP ===\n";
    std::cout << "u  = " << mask_to_hex(u) << "\n";
    std::cout << "v  = " << mask_to_hex(v) << "\n";
    std::cout << "r  = " << rounds << "\n\n";

    Timer timer;
    auto exact_res = exact_sparse_dp(u, rounds, 500000);
    double elapsed = timer.elapsed();

    std::cout << "Completed: " << (exact_res.completed ? "YES" : "NO (state explosion)") << "\n";
    std::cout << "Max states: " << exact_res.max_state_count << "\n";
    std::cout << "Time: " << elapsed << "s\n\n";

    if (exact_res.completed) {
        auto it = exact_res.distribution.find(v);
        double vt = (it != exact_res.distribution.end()) ? it->second : 0.0;
        std::cout << std::fixed << std::setprecision(15);
        std::cout << "VT = " << vt << "\n";
        if (vt != 0.0) {
            std::cout << "Score = " << std::setprecision(4) << compute_score(vt, rounds) << "\n";
        }
        std::cout << "Distribution size: " << exact_res.distribution.size() << "\n";
    }
}

// ---------------------------------------------------------------------------
// batch-input 命令
// ---------------------------------------------------------------------------
void cmd_batch_input(int argc, char* argv[]) {
    if (argc < 4) { print_usage(); return; }

    u32 u = static_cast<u32>(std::stoul(argv[2], nullptr, 16));
    int max_rounds = std::stoi(argv[3]);

    size_t beam_width = 200000;
    std::string output_dir;

    for (int i = 4; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--beam" && i + 1 < argc) {
            beam_width = std::stoul(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_dir = argv[++i];
        }
    }

    if (output_dir.empty()) output_dir = "../results";

    std::cout << "=== Batch Search ===\n";
    std::cout << "u  = " << mask_to_hex(u) << "\n";
    std::cout << "max_r = " << max_rounds << "\n";
    std::cout << "beam_width = " << beam_width << "\n";
    std::cout << "output = " << output_dir << "\n\n";

    Timer timer;
    auto entries = batch_search_input(u, max_rounds, beam_width, true);
    double elapsed = timer.elapsed();

    std::cout << "Found " << entries.size() << " valid estimates in "
              << elapsed << "s\n";

    print_summary(entries);

    // 写入文件
    std::filesystem::create_directories(output_dir);
    std::string filename = output_dir + "/" + mask_to_hex(u).substr(2) + "_r"
                           + std::to_string(max_rounds) + ".txt";
    write_entries_to_file(entries, filename);
}

// ---------------------------------------------------------------------------
// batch-all 命令
// ---------------------------------------------------------------------------
void cmd_batch_all(int argc, char* argv[]) {
    if (argc < 3) { print_usage(); return; }

    int max_rounds = std::stoi(argv[2]);
    size_t beam_width = 200000;
    std::string output_dir = "../results";

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--beam" && i + 1 < argc) {
            beam_width = std::stoul(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_dir = argv[++i];
        }
    }

    std::filesystem::create_directories(output_dir);

    auto inputs = all_single_active_inputs();
    std::cout << "=== Batch All ===\n";
    std::cout << "max_r = " << max_rounds << "\n";
    std::cout << "beam_width = " << beam_width << "\n";
    std::cout << "output = " << output_dir << "\n";
    std::cout << "total_inputs = " << inputs.size() << "\n\n";

    Timer total_timer;
    std::vector<EstimateEntry> all_entries;
    int completed = 0;

    for (u32 u : inputs) {
        ++completed;
        std::cout << "[" << completed << "/" << inputs.size() << "] "
                  << mask_to_hex(u) << " ... " << std::flush;

        Timer t;
        auto entries = batch_search_input(u, max_rounds, beam_width, false);
        double et = t.elapsed();

        std::cout << entries.size() << " entries, " << et << "s\n";
        all_entries.insert(all_entries.end(), entries.begin(), entries.end());
    }

    double total_elapsed = total_timer.elapsed();

    std::cout << "\n=== Done ===\n";
    std::cout << "Total time: " << total_elapsed << "s\n";

    print_summary(all_entries);

    // 写入合并文件
    std::string combined = output_dir + "/valid_estimates.txt";
    write_entries_to_file(all_entries, combined);

    std::string report = output_dir + "/score_report.txt";
    write_score_report(all_entries, report);
}

// ---------------------------------------------------------------------------
// batch-position 命令
// ---------------------------------------------------------------------------
void cmd_batch_position(int argc, char* argv[]) {
    if (argc < 4) { print_usage(); return; }

    int max_rounds = std::stoi(argv[2]);
    int pos = std::stoi(argv[3]);

    if (pos < 0 || pos >= 8) {
        std::cerr << "pos must be in [0,7]\n";
        return;
    }

    size_t beam_width = 200000;
    std::string output_dir = "../results";

    for (int i = 4; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--beam" && i + 1 < argc) {
            beam_width = std::stoul(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_dir = argv[++i];
        }
    }

    std::filesystem::create_directories(output_dir);

    std::cout << "=== Batch Position " << pos << " ===\n";
    std::cout << "max_r = " << max_rounds << "\n";
    std::cout << "pos = " << pos << "\n";
    std::cout << "beam_width = " << beam_width << "\n\n";

    Timer total_timer;
    std::vector<EstimateEntry> all_entries;

    for (int nib = 1; nib <= 15; ++nib) {
        std::array<int, 8> xs{};
        xs[pos] = nib;
        u32 u = pack_nibbles(xs);

        std::cout << "  nib=0x" << std::hex << nib << std::dec
                  << " u=" << mask_to_hex(u) << " ... " << std::flush;

        Timer t;
        auto entries = batch_search_input(u, max_rounds, beam_width, false);
        double et = t.elapsed();

        std::cout << entries.size() << " entries, " << et << "s\n";
        all_entries.insert(all_entries.end(), entries.begin(), entries.end());
    }

    double total_elapsed = total_timer.elapsed();

    std::cout << "\nTotal: " << all_entries.size() << " entries, "
              << total_elapsed << "s\n";

    print_summary(all_entries);

    std::string filename = output_dir + "/pos" + std::to_string(pos)
                           + "_r" + std::to_string(max_rounds) + ".txt";
    write_entries_to_file(all_entries, filename);
}

// ---------------------------------------------------------------------------
// verify 命令: 蒙特卡洛交叉验证
// ---------------------------------------------------------------------------
void cmd_verify(int argc, char* argv[]) {
    if (argc < 5) { print_usage(); return; }

    u32 u = static_cast<u32>(std::stoul(argv[2], nullptr, 16));
    u32 v = static_cast<u32>(std::stoul(argv[3], nullptr, 16));
    int rounds = std::stoi(argv[4]);

    int mc_samples = 1000000;
    size_t beam_width = 200000;

    for (int i = 5; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mc" && i + 1 < argc) {
            mc_samples = std::stoi(argv[++i]);
        } else if (arg == "--beam" && i + 1 < argc) {
            beam_width = std::stoul(argv[++i]);
        }
    }

    std::cout << "=== Cross Verification ===\n";
    std::cout << "u  = " << mask_to_hex(u) << "\n";
    std::cout << "v  = " << mask_to_hex(v) << "\n";
    std::cout << "r  = " << rounds << "\n";
    std::cout << "MC samples = " << mc_samples << "\n\n";

    // 波束搜索
    std::cout << "--- Beam Search ---\n";
    Timer t1;
    auto result = estimate_correlation(u, v, rounds, beam_width);
    std::cout << "VE = " << std::fixed << std::setprecision(15) << result.ve << "\n";
    std::cout << "Error bound = " << result.error_bound << "\n";
    std::cout << "Method: " << result.method << "\n";
    std::cout << "Time: " << t1.elapsed() << "s\n\n";

    // 蒙特卡洛
    std::cout << "--- Monte Carlo (" << mc_samples << " samples) ---\n";
    Timer t2;
    auto mc = monte_carlo_estimate(u, v, rounds, mc_samples);
    std::cout << "MC = " << mc.estimate << "\n";
    std::cout << "Std err = " << mc.std_error << "\n";
    std::cout << "Time: " << t2.elapsed() << "s\n\n";

    // 比较
    double diff = std::fabs(result.ve - mc.estimate);
    std::cout << "--- Comparison ---\n";
    std::cout << "|VE - MC| = " << diff << "\n";
    std::cout << "MC 95% CI: [" << (mc.estimate - 1.96 * mc.std_error)
              << ", " << (mc.estimate + 1.96 * mc.std_error) << "]\n";

    bool in_ci = (diff <= 1.96 * mc.std_error);
    std::cout << "VE in 95% CI: " << (in_ci ? "YES" : "NO") << "\n";
}

// ---------------------------------------------------------------------------
// brute 命令
// ---------------------------------------------------------------------------
void cmd_brute(int argc, char* argv[]) {
    if (argc < 5) { print_usage(); return; }

    u32 u = static_cast<u32>(std::stoul(argv[2], nullptr, 16));
    u32 v = static_cast<u32>(std::stoul(argv[3], nullptr, 16));
    int rounds = std::stoi(argv[4]);

    std::cout << "=== Brute Force (Way 1) ===\n";
    std::cout << "u  = " << mask_to_hex(u) << "\n";
    std::cout << "v  = " << mask_to_hex(v) << "\n";
    std::cout << "r  = " << rounds << "\n";
    std::cout << "This will enumerate 2^32 ≈ 4.3 billion inputs.\n";
    std::cout << "Estimated time: " << (rounds * 30) << " seconds.\n";
    std::cout << "Proceed? (y/N): " << std::flush;

    char c;
    std::cin >> c;
    if (c != 'y' && c != 'Y') {
        std::cout << "Aborted.\n";
        return;
    }

    std::cout << "Running...\n";
    Timer timer;
    double vt = brute_force_correlation(u, v, rounds);
    double elapsed = timer.elapsed();

    std::cout << std::fixed << std::setprecision(15);
    std::cout << "VT = " << vt << "\n";
    std::cout << "Time: " << elapsed << "s\n";
    if (vt != 0.0) {
        std::cout << "Score = " << std::setprecision(4)
                  << compute_score(vt, rounds) << "\n";
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 0;
    }

    std::string mode = argv[1];

    if (mode == "info") {
        cmd_info();
    } else if (mode == "estimate") {
        cmd_estimate(argc, argv);
    } else if (mode == "exact") {
        cmd_exact(argc, argv);
    } else if (mode == "batch-input") {
        cmd_batch_input(argc, argv);
    } else if (mode == "batch-all") {
        cmd_batch_all(argc, argv);
    } else if (mode == "batch-position") {
        cmd_batch_position(argc, argv);
    } else if (mode == "verify") {
        cmd_verify(argc, argv);
    } else if (mode == "brute") {
        cmd_brute(argc, argv);
    } else {
        std::cerr << "Unknown mode: " << mode << "\n\n";
        print_usage();
        return 1;
    }

    return 0;
}
