#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

using u32 = uint32_t;
using u64 = uint64_t;

namespace {

constexpr array<int, 16> SBOX = {
    0xC, 0x6, 0x9, 0x0, 0x1, 0xA, 0x2, 0xB,
    0x3, 0x8, 0x5, 0xD, 0x4, 0xE, 0x7, 0xF,
};

struct Transition {
    u32 mask;
    double corr;
};

struct Entry {
    int rounds;
    u32 u;
    u32 v;
    double ve;
    double vt;
    double score;
};

int LAT[16][16];
double LAT_NORM[16][16];
vector<pair<int, double>> NONZERO_LAT[16];
unordered_map<u32, vector<Transition>> ONE_ROUND_CACHE;

inline int active_nibbles(u32 x) {
    int count = 0;
    for (int i = 0; i < 8; ++i) {
        count += ((x >> (4 * i)) & 0xFU) != 0U;
    }
    return count;
}

inline int parity32(u32 x) {
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return static_cast<int>(x & 1U);
}

inline array<int, 8> split_nibbles(u32 x) {
    array<int, 8> out{};
    for (int i = 0; i < 8; ++i) {
        out[i] = static_cast<int>((x >> (28 - 4 * i)) & 0xFU);
    }
    return out;
}

inline u32 pack_nibbles(const array<int, 8>& xs) {
    u32 out = 0;
    for (int i = 0; i < 8; ++i) {
        out |= static_cast<u32>(xs[i]) << (28 - 4 * i);
    }
    return out;
}

inline array<int, 8> linear_mask_forward(const array<int, 8>& c) {
    return {
        c[7],
        c[0] ^ c[2] ^ c[5],
        c[5],
        c[2] ^ c[5] ^ c[7],
        c[3],
        c[1] ^ c[4] ^ c[6],
        c[1],
        c[1] ^ c[3] ^ c[6],
    };
}

void compute_lat() {
    for (int a = 0; a < 16; ++a) {
        NONZERO_LAT[a].clear();
        for (int b = 0; b < 16; ++b) {
            int count = 0;
            for (int x = 0; x < 16; ++x) {
                const int lhs = parity32(static_cast<u32>(a & x));
                const int rhs = parity32(static_cast<u32>(b & SBOX[x]));
                if (lhs == rhs) {
                    ++count;
                }
            }
            LAT[a][b] = count - 8;
            LAT_NORM[a][b] = static_cast<double>(LAT[a][b]) / 8.0;
            if (LAT[a][b] != 0) {
                NONZERO_LAT[a].push_back({b, LAT_NORM[a][b]});
            }
        }
    }
}

vector<Transition> build_one_round(u32 input_mask) {
    const array<int, 8> in = split_nibbles(input_mask);
    array<int, 8> sbox_out{};
    unordered_map<u32, double> merged;

    function<void(int, double)> dfs = [&](int pos, double corr) {
        if (pos == 8) {
            const u32 next_mask = pack_nibbles(linear_mask_forward(sbox_out));
            if (next_mask != 0U) {
                merged[next_mask] += corr;
            }
            return;
        }

        const int nib = in[pos];
        if (nib == 0) {
            sbox_out[pos] = 0;
            dfs(pos + 1, corr);
            return;
        }

        for (const auto& [out_mask, sbox_corr] : NONZERO_LAT[nib]) {
            sbox_out[pos] = out_mask;
            dfs(pos + 1, corr * sbox_corr);
        }
    };

    dfs(0, 1.0);

    vector<Transition> transitions;
    transitions.reserve(merged.size());
    for (const auto& [mask, corr] : merged) {
        if (fabs(corr) > 1e-18) {
            transitions.push_back({mask, corr});
        }
    }
    sort(transitions.begin(), transitions.end(), [](const Transition& a, const Transition& b) {
        if (fabs(a.corr) != fabs(b.corr)) {
            return fabs(a.corr) > fabs(b.corr);
        }
        return a.mask < b.mask;
    });
    return transitions;
}

const vector<Transition>& enumerate_one_round(u32 input_mask) {
    auto cached = ONE_ROUND_CACHE.find(input_mask);
    if (cached != ONE_ROUND_CACHE.end()) {
        return cached->second;
    }

    return ONE_ROUND_CACHE.emplace(input_mask, build_one_round(input_mask)).first->second;
}

unordered_map<u32, double> exact_distribution(u32 u, int rounds) {
    unordered_map<u32, double> current;
    current.reserve(1024);
    current[u] = 1.0;

    for (int r = 0; r < rounds; ++r) {
        unordered_map<u32, double> next;
        next.reserve(current.size() * 8);

        for (const auto& [mask, corr] : current) {
            if (active_nibbles(mask) <= 2) {
                const auto& transitions = enumerate_one_round(mask);
                for (const auto& transition : transitions) {
                    next[transition.mask] += corr * transition.corr;
                }
            } else {
                const auto transitions = build_one_round(mask);
                for (const auto& transition : transitions) {
                    next[transition.mask] += corr * transition.corr;
                }
            }
        }

        for (auto it = next.begin(); it != next.end();) {
            if (fabs(it->second) <= 1e-18) {
                it = next.erase(it);
            } else {
                ++it;
            }
        }

        current = move(next);
    }

    return current;
}

double exact_value(u32 u, u32 v, int rounds) {
    if (rounds == 0) {
        return u == v ? 1.0 : 0.0;
    }
    const auto distribution = exact_distribution(u, rounds);
    auto it = distribution.find(v);
    return it == distribution.end() ? 0.0 : it->second;
}

void round_function(array<int, 8>& state) {
    for (int i = 0; i < 8; ++i) {
        state[i] = SBOX[state[i]];
    }

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

u32 permute(u32 x, int rounds) {
    auto state = split_nibbles(x);
    for (int r = 0; r < rounds; ++r) {
        round_function(state);
    }
    return pack_nibbles(state);
}

double brute_force_value(u32 u, u32 v, int rounds) {
    long long count = 0;
    constexpr u64 total = 1ULL << 32;
    for (u64 x = 0; x < total; ++x) {
        const u32 y = permute(static_cast<u32>(x), rounds);
        count += (parity32(u & static_cast<u32>(x)) == parity32(v & y)) ? 1 : -1;
    }
    return static_cast<double>(count) / static_cast<double>(total);
}

double compute_score(double ve, int rounds) {
    return log2(pow(4.0, rounds) * fabs(ve));
}

vector<u32> single_active_inputs() {
    vector<u32> masks;
    masks.reserve(8 * 15);
    for (int pos = 0; pos < 8; ++pos) {
        for (int nib = 1; nib < 16; ++nib) {
            array<int, 8> xs{};
            xs[pos] = nib;
            masks.push_back(pack_nibbles(xs));
        }
    }
    return masks;
}

vector<Entry> search_positive_score_entries(int max_rounds) {
    const auto inputs = single_active_inputs();
    vector<Entry> entries;

    for (u32 u : inputs) {
        ONE_ROUND_CACHE.clear();
        unordered_map<u32, double> current;
        current.reserve(1024);
        current[u] = 1.0;

        for (int rounds = 1; rounds <= max_rounds; ++rounds) {
            unordered_map<u32, double> next;
            next.reserve(current.size() * 8);
            for (const auto& [mask, corr] : current) {
                if (active_nibbles(mask) <= 2) {
                    const auto& transitions = enumerate_one_round(mask);
                    for (const auto& transition : transitions) {
                        next[transition.mask] += corr * transition.corr;
                    }
                } else {
                    const auto transitions = build_one_round(mask);
                    for (const auto& transition : transitions) {
                        next[transition.mask] += corr * transition.corr;
                    }
                }
            }

            for (auto it = next.begin(); it != next.end();) {
                if (fabs(it->second) <= 1e-18) {
                    it = next.erase(it);
                } else {
                    ++it;
                }
            }

            const double min_abs = 1.0 / pow(4.0, rounds);
            for (const auto& [v, corr] : next) {
                if (u == 0U || v == 0U) {
                    continue;
                }
                if (fabs(corr) <= min_abs) {
                    continue;
                }
                entries.push_back({rounds, u, v, corr, corr, compute_score(corr, rounds)});
            }
            current = move(next);
        }
    }

    sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.rounds != b.rounds) {
            return a.rounds < b.rounds;
        }
        if (a.score != b.score) {
            return a.score > b.score;
        }
        if (a.u != b.u) {
            return a.u < b.u;
        }
        return a.v < b.v;
    });
    return entries;
}

filesystem::path results_dir() {
    return filesystem::current_path().parent_path() / "results";
}

void write_estimates(const vector<Entry>& entries) {
    const auto dir = results_dir();
    filesystem::create_directories(dir);

    ofstream txt(dir / "valid_estimates.txt");
    ofstream report(dir / "score_report.txt");
    ofstream summary(dir / "scores.txt");

    txt << "# Valid estimates generated by the repaired exact sparse-composition solver\n";
    txt << "# Format required by the problem statement: @(r, u, v, VE, VT)\n";

    report << fixed << setprecision(12);
    report << "PDF-compliant scoring report\n";
    report << "Rule: valid iff VE in [VT-|VT|*2^(-2r), VT+|VT|*2^(-2r)], VE!=0, u!=0, v!=0\n";
    report << "This solver computes VE and VT by exact sparse correlation composition, so VE = VT.\n\n";

    summary << fixed << setprecision(4);

    double total_score = 0.0;
    int current_round = -1;
    vector<double> round_scores;

    auto flush_round = [&](int rounds) {
        if (round_scores.empty()) {
            return;
        }
        const double round_sum = accumulate(round_scores.begin(), round_scores.end(), 0.0);
        const double round_avg = round_sum / static_cast<double>(round_scores.size());
        const double round_max = *max_element(round_scores.begin(), round_scores.end());
        const double round_min = *min_element(round_scores.begin(), round_scores.end());
        report << "r=" << rounds
               << ": count=" << round_scores.size()
               << ", sum=" << round_sum
               << ", max=" << round_max
               << ", min=" << round_min
               << ", avg=" << round_avg << "\n\n";
        summary << "r=" << rounds
                << ": count=" << round_scores.size()
                << ", sum=" << round_sum
                << ", max=" << round_max
                << ", min=" << round_min
                << ", avg=" << round_avg << "\n";
        round_scores.clear();
    };

    for (const auto& entry : entries) {
        if (entry.rounds != current_round) {
            flush_round(current_round);
            current_round = entry.rounds;
            report << "=== r = " << current_round << " ===\n";
            report << "No  u           v           VE                 VT                 Score\n";
        }

        txt << "@(" << entry.rounds
            << ", 0x" << hex << setw(8) << setfill('0') << uppercase << entry.u
            << ", 0x" << setw(8) << entry.v
            << dec << nouppercase << setfill(' ')
            << ", " << setprecision(15) << entry.ve
            << ", " << setprecision(15) << entry.vt
            << ")\n";

        report << setw(2) << round_scores.size() + 1
               << "  0x" << hex << setw(8) << setfill('0') << uppercase << entry.u
               << "  0x" << setw(8) << entry.v
               << dec << nouppercase << setfill(' ')
               << "  " << setw(18) << setprecision(12) << entry.ve
               << "  " << setw(18) << setprecision(12) << entry.vt
               << "  " << setw(8) << setprecision(4) << entry.score << "\n";

        total_score += entry.score;
        round_scores.push_back(entry.score);
    }

    flush_round(current_round);
    summary << "\nTotal valid estimates: " << entries.size() << "\n";
    summary << "Total score: " << total_score << "\n";
    report << "Total valid estimates: " << entries.size() << "\n";
    report << "Total score: " << total_score << "\n";

    cout << "Wrote " << entries.size() << " entries to " << (dir / "valid_estimates.txt").string() << "\n";
    cout << "Total score: " << fixed << setprecision(4) << total_score << "\n";
}

string to_hex(u32 value) {
    stringstream ss;
    ss << "0x" << hex << setw(8) << setfill('0') << uppercase << value;
    return ss.str();
}

void print_usage() {
    cout << "Usage:\n";
    cout << "  approx_cor exact <u> <v> <r>\n";
    cout << "  approx_cor brute <u> <v> <r>\n";
    cout << "  approx_cor dump <u> <r>\n";
    cout << "  approx_cor dump-single-active <r> [output_path]\n";
    cout << "  approx_cor dump-single-active-pos <r> <pos> [output_path]\n";
    cout << "  approx_cor dump-single-active-mask <r> <pos> <nib> [output_path]\n";
    cout << "  approx_cor search <max_rounds>\n";
    cout << "\n";
    cout << "Notes:\n";
    cout << "  exact : exact sparse correlation composition (PDF-compliant, fast)\n";
    cout << "  brute : 2^32 plaintext enumeration reference\n";
    cout << "  dump  : dump all positive-score exact entries for one input mask\n";
    cout << "  dump-single-active : dump all positive-score exact entries for all single-active inputs\n";
    cout << "  dump-single-active-pos : dump all positive-score exact entries for one active position\n";
    cout << "  dump-single-active-mask : dump all positive-score exact entries for one active position and nibble\n";
    cout << "  search: enumerate all positive-score entries for single-active input masks\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    compute_lat();

    if (argc < 2) {
        print_usage();
        return 0;
    }

    const string mode = argv[1];

    if (mode == "exact" && argc >= 5) {
        const u32 u = static_cast<u32>(stoul(argv[2], nullptr, 16));
        const u32 v = static_cast<u32>(stoul(argv[3], nullptr, 16));
        const int rounds = stoi(argv[4]);
        const double vt = exact_value(u, v, rounds);
        cout << "u  = " << to_hex(u) << "\n";
        cout << "v  = " << to_hex(v) << "\n";
        cout << "r  = " << rounds << "\n";
        cout << setprecision(15);
        cout << "VT = " << vt << "\n";
        if (vt != 0.0) {
            cout << "score = " << compute_score(vt, rounds) << "\n";
        }
        return 0;
    }

    if (mode == "brute" && argc >= 5) {
        const u32 u = static_cast<u32>(stoul(argv[2], nullptr, 16));
        const u32 v = static_cast<u32>(stoul(argv[3], nullptr, 16));
        const int rounds = stoi(argv[4]);
        const double vt = brute_force_value(u, v, rounds);
        cout << "u  = " << to_hex(u) << "\n";
        cout << "v  = " << to_hex(v) << "\n";
        cout << "r  = " << rounds << "\n";
        cout << setprecision(15);
        cout << "VT = " << vt << "\n";
        if (vt != 0.0) {
            cout << "score = " << compute_score(vt, rounds) << "\n";
        }
        return 0;
    }

    if (mode == "search" && argc >= 3) {
        const int max_rounds = stoi(argv[2]);
        const auto entries = search_positive_score_entries(max_rounds);
        write_estimates(entries);
        return 0;
    }

    if (mode == "dump" && argc >= 4) {
        const u32 u = static_cast<u32>(stoul(argv[2], nullptr, 16));
        const int rounds = stoi(argv[3]);
        const double min_abs = 1.0 / pow(4.0, rounds);
        const auto distribution = exact_distribution(u, rounds);
        vector<Entry> entries;
        for (const auto& [v, corr] : distribution) {
            if (u == 0U || v == 0U) {
                continue;
            }
            if (fabs(corr) <= min_abs) {
                continue;
            }
            entries.push_back({rounds, u, v, corr, corr, compute_score(corr, rounds)});
        }
        sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            if (a.score != b.score) {
                return a.score > b.score;
            }
            return a.v < b.v;
        });
        cout << fixed << setprecision(15);
        for (const auto& entry : entries) {
            cout << "@(" << entry.rounds
                 << ", 0x" << hex << setw(8) << setfill('0') << uppercase << entry.u
                 << ", 0x" << setw(8) << entry.v
                 << dec << nouppercase << setfill(' ')
                 << ", " << entry.ve
                 << ", " << entry.vt
                 << ")\n";
        }
        return 0;
    }

    if (mode == "dump-single-active" && argc >= 3) {
        const int rounds = stoi(argv[2]);
        ostream* out = &cout;
        ofstream fout;
        if (argc >= 4) {
            fout.open(argv[3], ios::out | ios::trunc);
            out = &fout;
        }

        const auto inputs = single_active_inputs();
        bool write_header = argc >= 4;
        if (write_header) {
            *out << "# Valid estimates generated by exact sparse composition for all single-active inputs, r="
                 << rounds << "\n";
            *out << "# Format: @(r, u, v, VE, VT)\n";
        }

        size_t total = 0;
        for (u32 u : inputs) {
            ONE_ROUND_CACHE.clear();
            const double min_abs = 1.0 / pow(4.0, rounds);
            const auto distribution = exact_distribution(u, rounds);
            vector<Entry> entries;
            for (const auto& [v, corr] : distribution) {
                if (u == 0U || v == 0U) {
                    continue;
                }
                if (fabs(corr) <= min_abs) {
                    continue;
                }
                entries.push_back({rounds, u, v, corr, corr, compute_score(corr, rounds)});
            }
            sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
                if (a.score != b.score) {
                    return a.score > b.score;
                }
                return a.v < b.v;
            });
            *out << fixed << setprecision(15);
            for (const auto& entry : entries) {
                *out << "@(" << entry.rounds
                     << ", 0x" << hex << setw(8) << setfill('0') << uppercase << entry.u
                     << ", 0x" << setw(8) << entry.v
                     << dec << nouppercase << setfill(' ')
                     << ", " << entry.ve
                     << ", " << entry.vt
                     << ")\n";
                ++total;
            }
        }

        if (argc < 4) {
            cerr << "total_entries=" << total << "\n";
        }
        return 0;
    }

    if (mode == "dump-single-active-pos" && argc >= 4) {
        const int rounds = stoi(argv[2]);
        const int pos = stoi(argv[3]);
        if (pos < 0 || pos >= 8) {
            cerr << "pos must be in [0,7]\n";
            return 1;
        }

        ostream* out = &cout;
        ofstream fout;
        if (argc >= 5) {
            fout.open(argv[4], ios::out | ios::trunc);
            out = &fout;
        }

        for (int nib = 1; nib <= 15; ++nib) {
            const u32 u = static_cast<u32>(nib) << (28 - 4 * pos);
            ONE_ROUND_CACHE.clear();
            const double min_abs = 1.0 / pow(4.0, rounds);
            const auto distribution = exact_distribution(u, rounds);
            vector<Entry> entries;
            for (const auto& [v, corr] : distribution) {
                if (u == 0U || v == 0U) {
                    continue;
                }
                if (fabs(corr) <= min_abs) {
                    continue;
                }
                entries.push_back({rounds, u, v, corr, corr, compute_score(corr, rounds)});
            }
            sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
                if (a.score != b.score) {
                    return a.score > b.score;
                }
                return a.v < b.v;
            });
            *out << fixed << setprecision(15);
            for (const auto& entry : entries) {
                *out << "@(" << entry.rounds
                     << ", 0x" << hex << setw(8) << setfill('0') << uppercase << entry.u
                     << ", 0x" << setw(8) << entry.v
                     << dec << nouppercase << setfill(' ')
                     << ", " << entry.ve
                     << ", " << entry.vt
                     << ")\n";
            }
        }
        return 0;
    }

    if (mode == "dump-single-active-mask" && argc >= 5) {
        const int rounds = stoi(argv[2]);
        const int pos = stoi(argv[3]);
        const int nib = stoi(argv[4]);
        if (pos < 0 || pos >= 8) {
            cerr << "pos must be in [0,7]\n";
            return 1;
        }
        if (nib < 1 || nib > 15) {
            cerr << "nib must be in [1,15]\n";
            return 1;
        }

        ostream* out = &cout;
        ofstream fout;
        if (argc >= 6) {
            fout.open(argv[5], ios::out | ios::trunc);
            out = &fout;
        }

        const u32 u = static_cast<u32>(nib) << (28 - 4 * pos);
        ONE_ROUND_CACHE.clear();
        const double min_abs = 1.0 / pow(4.0, rounds);
        const auto distribution = exact_distribution(u, rounds);
        vector<Entry> entries;
        for (const auto& [v, corr] : distribution) {
            if (u == 0U || v == 0U) {
                continue;
            }
            if (fabs(corr) <= min_abs) {
                continue;
            }
            entries.push_back({rounds, u, v, corr, corr, compute_score(corr, rounds)});
        }
        sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            if (a.score != b.score) {
                return a.score > b.score;
            }
            return a.v < b.v;
        });
        *out << fixed << setprecision(15);
        for (const auto& entry : entries) {
            *out << "@(" << entry.rounds
                 << ", 0x" << hex << setw(8) << setfill('0') << uppercase << entry.u
                 << ", 0x" << setw(8) << entry.v
                 << dec << nouppercase << setfill(' ')
                 << ", " << entry.ve
                 << ", " << entry.vt
                 << ")\n";
        }
        return 0;
    }

    print_usage();
    return 1;
}
