// RiemannExplorer3Test - 黎曼猜想第三波數值探索（全 headless）
//
// 承接前兩波（零點定位/GRH/判據/GUE 間距/ξ/Σ1/ρ/矩/
// CLT/de Bruijn–Newman/Mertens/η/σ=1），本波補上：
//
//   配對關聯     — Montgomery–Dyson：所有零點對的
//                  正規化差分佈 vs GUE R₂(u)
//   γ₁₀₆ 高處    — 第 10⁶ 個零點 t≈600270.752，
//                  EM 推到 N≈2.5M（最高 t 驗證）
//   Báez–Duarte  — Nyman–Beurling：常數 1 到
//                  span{{k/t}} 的 L²(0,1) 距離 d_N
//                  應單調↓（→0 ⇔ RH，泛函分析形式）
//   S(T) 有界    — 計數殘差 |N(T)−θ(T)/π−1| 的有界性
//   Dedekind ζ   — ζ_{Q(√5)} 函數方程 |Λ(s)|=|Λ(1−s)|
//   Euler 乘積   — ∏(1−p^{−s})^{−1}→ζ(s)（σ>1）
//   Z′ 交錯      — 單零點 ⇒ 相鄰零點導數符號嚴格交替
//
// 數值驗證 ≠ 證明：每個檢查都可證偽，有限樣本只支持猜想。

#include "MathUtils/RiemannZeta.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using namespace Potato::RZ;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

namespace {

// NxN 高斯消去（部分選主元）——Báez–Duarte 最小二乘用
std::vector<double> Solve(std::vector<std::vector<double>> A,
                          std::vector<double> b) {
    const int n = static_cast<int>(A.size());
    for (int col = 0; col < n; ++col) {
        int piv = col;
        for (int r = col + 1; r < n; ++r)
            if (std::fabs(A[r][col]) >
                std::fabs(A[piv][col]))
                piv = r;
        std::swap(A[piv], A[col]);
        std::swap(b[piv], b[col]);
        const double d = A[col][col];
        for (int r = col + 1; r < n; ++r) {
            const double f = A[r][col] / d;
            for (int c = col; c < n; ++c)
                A[r][c] -= f * A[col][c];
            b[r] -= f * b[col];
        }
    }
    std::vector<double> x(static_cast<size_t>(n));
    for (int r = n - 1; r >= 0; --r) {
        double s = b[r];
        for (int c = r + 1; c < n; ++c) s -= A[r][c] * x[c];
        x[r] = s / A[r][r];
    }
    return x;
}

} // namespace

int main() {
    printf("=== Riemann ζ 數值探索 第三波 ===\n");
    printf("（可證偽檢查；有限樣本不構成證明）\n\n");

    // ---- 前置：零點表（t≤330，同前兩波）----
    std::vector<double> roots;
    {
        auto zhi = [](double t) { return RotZetaHi(t, 1400); };
        roots = ScanRoots(zhi, 10.0, 330.0, 0.02);
        printf("  [info] 零點表：%zu 個\n", roots.size());
        Check(roots.size() >= 150,
              "零點表重建（t≤330 ≥150 個）");
    }

    // [1] Montgomery 配對關聯：把每個 γ_i 正規化到平均
    //     間距 1 的刻度 x_i=θ(γ_i)/π+1，統計所有對差
    //     d=x_j−x_i 的分佈。GUE 預測 R₂(u)=1−(sinπu/πu)²
    //     ——u→0 時 ~π²u²/3 強排斥；Poisson 則恆 1。
    //     ~12k 對足以分辨小 u 排斥 vs Poisson。
    {
        std::vector<double> xs;
        xs.reserve(roots.size());
        for (const double g : roots)
            xs.push_back(RSTheta(g) / kPi + 1.0);
        int bins[6] = {0};       // d∈[0,0.5)..[2.5,3)
        double r2bins[6] = {0};  // GUE R₂ 預測（bin 中點）
        int pairs = 0;
        for (size_t i = 0; i < xs.size(); ++i) {
            for (size_t j = i + 1; j < xs.size(); ++j) {
                const double d = xs[j] - xs[i];
                if (d >= 3.0) continue;
                ++pairs;
                ++bins[static_cast<int>(d / 0.5)];
            }
        }
        for (int b = 0; b < 6; ++b) {
            const double u = 0.5 * b + 0.25;
            const double s = std::sin(kPi * u) / (kPi * u);
            r2bins[b] = 1.0 - s * s;
        }
        printf("  [info] 配對關聯 bin 密度 vs GUE R₂：\n");
        bool repulsion = true;
        for (int b = 0; b < 6; ++b) {
            const double density =
                bins[b] * 2.0 /
                static_cast<double>(xs.size()); // 正規化
            printf("    u∈[%.1f,%.1f): 密度 %.3f "
                   "GUE %.3f Poisson 1.0\n",
                   0.5 * b, 0.5 * (b + 1), density,
                   r2bins[b]);
        }
        // 關鍵鑑別：u<0.5 bin——GUE R₂(0.25)≈0.16
        // vs Poisson 1.0；實測密度應遠低於 1
        const double d0 =
            bins[0] * 2.0 / static_cast<double>(xs.size());
        Check(d0 < 0.45,
              "配對關聯 u<0.5 強排斥（GUE 特徵,Poisson=1）");
        printf("  [info] d<3 配對數 %d\n", pairs);
        Check(pairs > 300, "配對樣本數充足");
        (void)repulsion;
    }

    // [2] γ₁₀₆：第 10⁶ 個零點 t≈600270.752（Odlyzko 表值）。
    //     t 達 6×10⁵——EM 主和 N≈2.5M 才能抗漸近發散,
    //     是引擎目前最高的單點零點驗證。
    {
        const int n6 = 2600000;
        auto z6 = [n6](double t) { return RotZetaHi(t, n6); };
        const double zA = z6(600270.65), zB = z6(600270.85);
        printf("  [info] Z(600270.65)=%.4f Z(600270.85)=%.4f\n",
               zA, zB);
        Check(zA * zB < 0.0,
              "t≈600270.75 存在變號（γ₁₀₆ 括住）");
        if (zA * zB < 0.0) {
            const double r = Bisect(z6, 600270.65, 600270.85,
                                    1e-7);
            const double mag =
                std::abs(ZetaEMN(C(0.5, r), n6));
            printf("  [info] 根 t=%.5f（表值 600270.752）"
                   " |ζ|=%.2e\n", r, mag);
            Check(std::fabs(r - 600270.752) < 0.05,
                  "γ₁₀₆ 對上 Odlyzko 表值");
            Check(mag < 2e-2, "|ζ(½+iγ₁₀₆)|≈0——確為零點");
        }
    }

    // [3] Báez–Duarte/Nyman–Beurling：ρ_k(t)={k/t},
    //     t∈(0,1)。RH ⇔ 常數 1 可用 {ρ_k} 的線性組合
    //     任意逼近——即距離 d_N→0。Mellin 連結：
    //     ∫{u}u^{−s−1}du 分子含 ζ(s)——零點決定可逼近性。
    //     數值：梯形積分 Gram 矩陣 + 最小二乘。
    //     {k/t} 在 t→0 震盪——截斷 t≥0.002 的 L² 誤差
    //     ≤√0.002≈4%（|ρ|≤1）,趨勢判定足夠。
    {
        const int nF = 20;         // 基函數 ρ_1..ρ_20
        const int nP = 400000;     // 梯形點數
        const double tMin = 0.002;
        const double h = (1.0 - tMin) / nP;
        std::vector<std::vector<double>> rho(
            static_cast<size_t>(nF),
            std::vector<double>(static_cast<size_t>(nP) + 1));
        for (int k = 0; k < nF; ++k) {
            auto& row = rho[static_cast<size_t>(k)];
            for (int i = 0; i <= nP; ++i) {
                const double t = tMin + h * i;
                const double u = (k + 1) / t;
                row[static_cast<size_t>(i)] =
                    u - std::floor(u);
            }
        }
        auto dot = [&](int a, int b) {
            double s = 0.0;
            const auto& ra = rho[static_cast<size_t>(a)];
            const auto& rb = rho[static_cast<size_t>(b)];
            for (int i = 0; i <= nP; ++i) {
                const double w =
                    (i == 0 || i == nP) ? 0.5 : 1.0;
                s += w * ra[static_cast<size_t>(i)] *
                     rb[static_cast<size_t>(i)];
            }
            return s * h;
        };
        auto integ = [&](int a) {
            double s = 0.0;
            const auto& ra = rho[static_cast<size_t>(a)];
            for (int i = 0; i <= nP; ++i) {
                const double w =
                    (i == 0 || i == nP) ? 0.5 : 1.0;
                s += w * ra[static_cast<size_t>(i)];
            }
            return s * h;
        };
        // 對 N=5,10,15,20 逐一最小二乘：d² = 1−bᵀG⁻¹b
        double prevD = 2.0;
        bool monotone = true, inRange = true;
        for (const int N : {5, 10, 15, 20}) {
            std::vector<std::vector<double>> G(
                static_cast<size_t>(N),
                std::vector<double>(static_cast<size_t>(N)));
            std::vector<double> bv(static_cast<size_t>(N));
            for (int i = 0; i < N; ++i) {
                bv[static_cast<size_t>(i)] = integ(i);
                for (int j = 0; j < N; ++j)
                    G[i][j] = dot(i, j);
            }
            // 解 G·a=b → d²=1−aᵀb（常數 1 的 norm²=1）
            const std::vector<double> a = Solve(G, bv);
            double proj = 0.0;
            for (int i = 0; i < N; ++i)
                proj += a[static_cast<size_t>(i)] *
                        bv[static_cast<size_t>(i)];
            const double d2 =
                std::max(0.0, 1.0 - proj);
            const double d = std::sqrt(d2);
            printf("  [info] N=%2d  d_N=%.4f（d²=%.5f）\n",
                   N, d, d2);
            if (d > prevD + 1e-4) monotone = false;
            if (d <= 0.01 || d >= 1.0) inRange = false;
            prevD = d;
        }
        Check(monotone,
              "Báez–Duarte d_N 單調遞減（→0 ⇔ RH）");
        Check(inRange,
              "d_N 位於合理收斂區間（0,1）");
    }

    // [4] S(T)：N(T)=θ(T)/π+1+S(T)。實測 |S(T)| 應
    //     有界在小範圍（已知 |S|≤~2.3 於 t≤300）——
    //     逐點計數 vs von Mangoldt 主項的殘差
    {
        double maxS = 0.0, atT = 0.0;
        for (double T = 15.0; T <= 330.0; T += 0.25) {
            int cnt = 0;
            for (const double r : roots)
                if (r < T) ++cnt;
            const double s =
                cnt - RSTheta(T) / kPi - 1.0;
            if (std::fabs(s) > maxS) {
                maxS = std::fabs(s);
                atT = T;
            }
        }
        printf("  [info] max|S(T)|=%.3f at T=%.1f\n",
               maxS, atT);
        Check(maxS < 2.5,
              "S(T) 有界（|N−θ/π−1|≤2.5 on t≤330）");
    }

    // [5] Dedekind ζ_{Q(√5)} 函數方程：
    //     Λ_K(s)=5^{s/2}π^{−s}Γ(s/2)²ζ_K(s) 滿足
    //     Λ(s)=Λ(1−s)（判別式 5,r1=2,r2=0）
    {
        auto lambda = [](const C& s) {
            return std::pow(C(5.0), s * C(0.5)) *
                   std::pow(C(kPi), -s) *
                   Gamma(s * C(0.5)) * Gamma(s * C(0.5)) *
                   ZetaEM(s) * Chi5L(s);
        };
        const C probes[3] = {C(0.3, 14), C(0.4, 21),
                             C(0.6, 30)};
        bool fe = true;
        for (const C& s : probes) {
            const double lhs = std::abs(lambda(s));
            const double rhs =
                std::abs(lambda(C(1.0) - s));
            printf("  [info] s=%.1f+%.0fi |Λ(s)|=%.4e "
                   "|Λ(1−s)|=%.4e\n",
                   s.real(), s.imag(), lhs, rhs);
            const double rel =
                std::fabs(lhs - rhs) /
                std::max(1e-30, lhs);
            if (rel > 1e-4) fe = false;
        }
        Check(fe, "Dedekind Λ_K(s)=Λ_K(1−s)（Q(√5) FE）");
    }

    // [6] Euler 乘積：σ>1 時 ζ(s)=∏_p(1−p^{−s})^{−1}。
    //     截斷尾誤差 ~Σ_{n>P}n^{−σ}
    {
        const int P = 2000;
        std::vector<int> primes;
        std::vector<char> comp(static_cast<size_t>(P) + 1);
        for (int i = 2; i <= P; ++i) {
            if (!comp[static_cast<size_t>(i)])
                primes.push_back(i);
            for (const int pr : primes) {
                if (1LL * i * pr > P) break;
                comp[static_cast<size_t>(i * pr)] = 1;
            }
        }
        const double z2 = kPi * kPi / 6.0;
        double prod2 = 1.0, prod3 = 1.0;
        for (const int pr : primes) {
            const double p2 =
                static_cast<double>(pr) * pr;
            const double p3 = p2 * pr;
            prod2 /= (1.0 - 1.0 / p2);
            prod3 /= (1.0 - 1.0 / p3);
        }
        printf("  [info] ∏_{p≤%d}(1−p^{−2})^{−1}=%.6f "
               "ζ(2)=%.6f\n", P, prod2, z2);
        Check(std::fabs(prod2 - z2) < 6e-3,
              "Euler 乘積 ζ(2)=π²/6（P=2000 尾差~1/P）");
        Check(std::fabs(prod3 - ZetaEM(C(3, 0)).real()) <
                  6e-4,
              "Euler 乘積 ζ(3)（P=2000）");
    }

    // [7] Z′ 交錯：全部零點為單零點 ⇒ Z′(γ_i) 符號
    //     嚴格交替（Z 在相鄰零點間單調反轉）。
    //     導數用真 Z 中心差分 h=1e-4
    {
        auto zhi = [](double t) { return RotZetaHi(t, 1400); };
        auto zp = [&zhi](double t) {
            const double h = 1e-4;
            return (zhi(t + h) - zhi(t - h)) / (2 * h);
        };
        int bad = 0;
        double prevSign = 0.0;
        for (size_t i = 0; i < roots.size(); ++i) {
            const double d = zp(roots[i]);
            const double s = d > 0 ? 1.0 : -1.0;
            if (i > 0 && s * prevSign >= 0.0) ++bad;
            prevSign = s;
        }
        printf("  [info] %zu 零點中 Z′ 符號異常交替 %d 處\n",
               roots.size(), bad);
        Check(bad == 0,
              "Z′ 在全部 157 零點嚴格交錯（單零點+全線上）");
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n",
           g_pass, g_fail);
    if (g_fail == 0) {
        printf("\n第三波結論：配對關聯呈 GUE 排斥、γ₁₀₆ 對上表值、"
               "Báez–Duarte d_N 單調↓、\nS(T) 有界、Dedekind "
               "函數方程、Euler 乘積、Z′ 全交錯——\n"
               "新增三種等價形式（泛函/配對/高處）皆與 RH 相容；"
               "有限計算仍不構成證明。\n");
    }
    return g_fail == 0 ? 0 : 1;
}
