/**
 * 调试程序: 验证单轮相关度计算
 */
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <vector>
#include <cmath>
using namespace std;

typedef unsigned int uint;

int Sbox[16] = {0xC, 0x6, 0x9, 0x0, 0x1, 0xA, 0x2, 0xB, 0x3, 0x8, 0x5, 0xD, 0x4, 0xE, 0x7, 0xF};

inline int nibble_dot(int a, int b) {
    int z = a & b;
    z ^= z >> 1;
    z ^= z >> 2;
    return z & 1;
}

inline int dot(uint u, uint y) {
    uint z = u & y;
    z ^= z >> 1; z ^= z >> 2; z ^= z >> 4;
    z ^= z >> 8; z ^= z >> 16;
    return z & 1;
}

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

void compute_LAT(int LAT[16][16]) {
    for (int a = 0; a < 16; ++a)
        for (int b = 0; b < 16; ++b) {
            int cnt = 0;
            for (int x = 0; x < 16; ++x)
                if (nibble_dot(a, x) == nibble_dot(b, Sbox[x])) cnt++;
            LAT[a][b] = cnt - 8;
        }
}

void mask_backward(const int b[8], int c[8]) {
    c[0] = b[0] ^ b[1] ^ b[3];
    c[1] = b[6];
    c[2] = b[0] ^ b[2] ^ b[3];
    c[3] = b[4];
    c[4] = b[4] ^ b[5] ^ b[7];
    c[5] = b[2];
    c[6] = b[4] ^ b[6] ^ b[7];
    c[7] = b[0];
}

void mask_forward(const int c[8], int b[8]) {
    int mc[8];
    mc[0] = c[0] ^ c[2] ^ c[3];
    mc[1] = c[0];
    mc[2] = c[1] ^ c[2];
    mc[3] = c[0] ^ c[2];
    mc[4] = c[4] ^ c[6] ^ c[7];
    mc[5] = c[4];
    mc[6] = c[5] ^ c[6];
    mc[7] = c[4] ^ c[6];
    b[0] = mc[0]; b[1] = mc[5]; b[2] = mc[2]; b[3] = mc[7];
    b[4] = mc[4]; b[5] = mc[1]; b[6] = mc[6]; b[7] = mc[3];
}

uint perm(uint x, int R) {
    int state[8];
    uint_to_nibbles(x, state);
    for (int r = 0; r < R; ++r) {
        for (int i = 0; i < 8; ++i) state[i] = Sbox[state[i]];
        int t0=state[0],t1=state[5],t2=state[2],t3=state[7];
        int t4=state[4],t5=state[1],t6=state[6],t7=state[3];
        state[0]=t0^t2^t3; state[1]=t0; state[2]=t1^t2; state[3]=t0^t2;
        state[4]=t4^t6^t7; state[5]=t4; state[6]=t5^t6; state[7]=t4^t6;
    }
    return nibbles_to_uint(state);
}

int main() {
    int LAT[16][16];
    compute_LAT(LAT);

    cout << "=== LAT table (decimal) ===" << endl;
    for (int a = 0; a < 16; ++a) {
        cout << setw(2) << hex << uppercase << a << ": ";
        for (int b = 0; b < 16; ++b)
            cout << setw(4) << dec << LAT[a][b];
        cout << endl;
    }
    cout << endl;

    // Test: u = 0x00000001, v = 0x10010000 (our theoretical prediction)
    uint u = 0x00000001;
    uint v = 0x10010000;

    cout << "=== Manual computation for u=0x" << hex << setw(8) << setfill('0') << u;
    cout << " v=0x" << setw(8) << setfill('0') << v << dec << " ===" << endl;

    int nu[8], nv[8], nc[8];
    uint_to_nibbles(u, nu);
    uint_to_nibbles(v, nv);

    cout << "u nibbles: ";
    for (int i=0;i<8;i++) cout << setw(2) << hex << nu[i] << " ";
    cout << dec << endl;

    cout << "v nibbles: ";
    for (int i=0;i<8;i++) cout << setw(2) << hex << nv[i] << " ";
    cout << dec << endl;

    mask_backward(nv, nc);
    cout << "c nibbles (backward): ";
    for (int i=0;i<8;i++) cout << setw(2) << hex << nc[i] << " ";
    cout << dec << endl;

    // Verify forward matches
    int nb_check[8];
    mask_forward(nc, nb_check);
    cout << "b check (forward of backward): ";
    for (int i=0;i<8;i++) cout << setw(2) << hex << nb_check[i] << " ";
    cout << "(should match v)" << endl;

    double corr = 1.0;
    for (int j=0;j<8;j++) {
        double cj = LAT[nu[j]][nc[j]] / 16.0;
        cout << "  LAT[" << hex << nu[j] << "][" << nc[j] << "] = " << dec << LAT[nu[j]][nc[j]] << "/16 = " << cj << endl;
        corr *= cj;
    }
    cout << "Predicted single-round correlation = " << corr << endl;

    // Now compute exact for small sample to verify
    cout << "\n=== Sampling verification (2^20 samples) ===" << endl;
    long long cnt = 0;
    const uint64_t N = 1ULL << 20;
    for (uint64_t x = 0; x < N; ++x) {
        uint y = perm((uint)x, 1);
        if (dot(u, (uint)x) == dot(v, y)) cnt++;
        else cnt--;
    }
    cout << "Sampled correlation (~2^20): " << (double)cnt / (double)N << endl;
    cout << "Predicted:                  " << corr << endl;

    // Also test the brute force exact computation for a specific x
    cout << "\n=== Single input trace ===" << endl;
    for (uint test_x = 0; test_x < 5; test_x++) {
        uint y = perm(test_x, 1);
        cout << "x=0x" << hex << setw(8) << setfill('0') << test_x;
        cout << " y=0x" << setw(8) << setfill('0') << y;
        cout << " u·x=" << dot(u, test_x) << " v·y=" << dot(v, y) << dec << endl;
    }

    return 0;
}
