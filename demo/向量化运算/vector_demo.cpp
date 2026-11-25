// generate by deepseek and run at mac
/*
*
* g++ -O3 -std=c++17 vector_demo.cpp -o vector_demo_neon
* ./vector_demo
*/
// vector_demo_neon.cpp  —  Apple Silicon (ARM64) 版本
// 编译: clang++ -O3 -std=c++17 vector_demo_neon.cpp -o vector_demo_neon
// 运行: ./vector_demo_neon

#include <arm_neon.h>      // NEON intrinsics
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include <algorithm>

using Clock = std::chrono::high_resolution_clock;

// ---------------- 模拟数据生成 ----------------
static void gen_data(std::vector<float>& v, size_t N, unsigned seed) {
    v.resize(N);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (size_t i = 0; i < N; ++i) v[i] = dist(rng);
}

// ---------------- 非向量化（标量）实现 ----------------
static void saxpy_scalar(float a, const float* x, float* y, size_t N) {
    for (size_t i = 0; i < N; ++i) y[i] = a * x[i] + y[i];
}

static float dot_scalar(const float* x, const float* y, size_t N) {
    float s = 0.0f;
    for (size_t i = 0; i < N; ++i) s += x[i] * y[i];
    return s;
}

// ---------------- 向量化（NEON, 128-bit, 4x float） ----------------
static void saxpy_neon(float a, const float* x, float* y, size_t N) {
    float32x4_t va = vdupq_n_f32(a);
    size_t i = 0;
    for (; i + 4 <= N; i += 4) {
        float32x4_t vx = vld1q_f32(x + i);
        float32x4_t vy = vld1q_f32(y + i);
        float32x4_t r  = vmlaq_f32(vy, va, vx);   // y + a*x
        vst1q_f32(y + i, r);
    }
    for (; i < N; ++i) y[i] = a * x[i] + y[i];
}

static float dot_neon(const float* x, const float* y, size_t N) {
    float32x4_t acc = vdupq_n_f32(0.0f);
    size_t i = 0;
    for (; i + 4 <= N; i += 4) {
        float32x4_t vx = vld1q_f32(x + i);
        float32x4_t vy = vld1q_f32(y + i);
        acc = vmlaq_f32(acc, vx, vy);
    }
    float sum = vgetq_lane_f32(acc, 0) + vgetq_lane_f32(acc, 1) +
                vgetq_lane_f32(acc, 2) + vgetq_lane_f32(acc, 3);
    for (; i < N; ++i) sum += x[i] * y[i];
    return sum;
}

int main() {
    const size_t N = 64 * 1024 * 1024;   // 64M floats = 256MB / array
    const int ITERS = 10;

    printf("N = %zu floats (%.1f MB per array)\n\n",
           N, N * sizeof(float) / 1024.0 / 1024.0);

    // ---- 生成测试数据 ----
    std::vector<float> x, y0;
    gen_data(x,  N, 12345);
    gen_data(y0, N, 67890);
    std::vector<float> y(N);

    // ---- SAXPY: y = a*x + y ----
    printf("=== SAXPY: y = a*x + y ===\n");
    const float a = 2.5f;

    auto run_saxpy_scalar = [&] {
        std::copy(y0.begin(), y0.end(), y.begin());
        auto s = Clock::now();
        saxpy_scalar(a, x.data(), y.data(), N);
        return std::chrono::duration<double>(Clock::now() - s).count();
    };
    auto run_saxpy_neon = [&] {
        std::copy(y0.begin(), y0.end(), y.begin());
        auto s = Clock::now();
        saxpy_neon(a, x.data(), y.data(), N);
        return std::chrono::duration<double>(Clock::now() - s).count();
    };

    run_saxpy_scalar(); run_saxpy_neon();   // 预热

    double ts = 0, tv = 0;
    for (int k = 0; k < ITERS; ++k) { ts += run_saxpy_scalar(); tv += run_saxpy_neon(); }
    ts /= ITERS; tv /= ITERS;

    double bytes = 3.0 * N * sizeof(float);
    printf("scalar: %.4f s  (%.2f GB/s)\n", ts, bytes / ts / 1e9);
    printf("neon  : %.4f s  (%.2f GB/s)\n", tv, bytes / tv / 1e9);
    printf("speedup: %.2fx\n\n", ts / tv);

    // ---- DOT ----
    printf("=== DOT: sum = x . y ===\n");
    volatile float sink;

    double ds = 0, dv = 0;
    for (int k = 0; k < ITERS; ++k) {
        auto s0 = Clock::now();
        sink = dot_scalar(x.data(), y0.data(), N);
        ds += std::chrono::duration<double>(Clock::now() - s0).count();

        auto s1 = Clock::now();
        sink = dot_neon(x.data(), y0.data(), N);
        dv += std::chrono::duration<double>(Clock::now() - s1).count();
    }
    ds /= ITERS; dv /= ITERS;

    double dbytes = 2.0 * N * sizeof(float);
    printf("scalar: %.4f s  (%.2f GB/s)\n", ds, dbytes / ds / 1e9);
    printf("neon  : %.4f s  (%.2f GB/s)\n", dv, dbytes / dv / 1e9);
    printf("speedup: %.2fx\n", ds / dv);

    return 0;
}