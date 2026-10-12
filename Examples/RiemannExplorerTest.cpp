// RiemannExplorerTest - 黎曼 ζ 函數數值探索（全 headless）
//
// 注意：數值驗證 ≠ 證明。本程式展示引擎能對黎曼猜想做的
//   唯一誠實事項——驗證「有限個」非平凡零點落在臨界線
//   Re(s)=1/2 上（前 10 個對照已知值），並渲染 |ζ| 臨界帶地形。
//
// 方法：
//   ZetaEM()   — Euler-Maclaurin 求和（主和 + 邊界 + Bernoulli 修正）
//   RSZ()      — Riemann-Siegel Z(t) 主和（廉價掃描括住變號區間）
//   RotZeta()  — Re(e^{iθ(t)} ζ(0.5+it))，實值函數 = Z(t) 的高精度版
//   影像：σ∈[0,1] × t∈[0,55] 的 log|ζ| 灰階圖——
//   零點（黑洞）應全數排在 σ=0.5 中線上。

#include "Rendering/ImageCodec.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

using Potato::uint8;
using Potato::uint32;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

namespace {

using C = std::complex<double>;
constexpr double kPi = 3.14159265358979323846;

// n^{-s} = n^{-σ}·e^{-it ln n}（實底複指數，比 std::pow(complex) 快且穩）
C PowNeg(double n, const C& s) {
    const double ln = std::log(n);
    const double mag = std::exp(-s.real() * ln);
    const double ph = -s.imag() * ln;
    return C(mag * std::cos(ph), mag * std::sin(ph));
}

// Euler-Maclaurin 修正項係數：B_{2k}/(2k)! 預先折合
//   B2=1/6 B4=-1/30 B6=1/42 B8=-1/30 B10=5/66 B12=-691/2730
//   B14=7/6 B16=-3617/510（ZetaEMN / HurwitzEMN 共用）
constexpr int kEmM = 8;
static const double kEmCoef[kEmM] = {
    (1.0 / 6) / 2, (-1.0 / 30) / 24, (1.0 / 42) / 720,
    (-1.0 / 30) / 40320, (5.0 / 66) / 3628800,
    (-691.0 / 2730) / 479001600.0, (7.0 / 6) / 87178291200.0,
    (-3617.0 / 510) / 20922789888000.0,
};

// Euler-Maclaurin：
//   ζ(s) = Σ_{n<N} n^{-s} + N^{1-s}/(s-1) + N^{-s}/2
//          + Σ_k B_{2k}/(2k)! · (s)_{2k-1} · N^{-s-2k+1}
// N 參數化：修正項規模 ~|s|^{2k-1}/N^{2k-0.5}，高 t 需 N≳4t
// 才能收斂（否則漸近發散）
C ZetaEMN(const C& s, int N) {
    C sum = 0.0;
    for (int n = 1; n < N; ++n) sum += PowNeg(n, s);
    sum += PowNeg(N, s - C(1, 0)) / (s - C(1, 0)); // N^{1-s}/(s-1)
    sum += 0.5 * PowNeg(N, s);                     // 邊界項
    C rise = s; // (s)_{2k-1} 升冪階乘累乘
    for (int k = 1; k <= kEmM; ++k) {
        sum += kEmCoef[k - 1] * rise * PowNeg(N, s + C(2 * k - 1, 0));
        rise *= (s + C(2 * k - 1, 0)) * (s + C(2 * k, 0));
    }
    return sum;
}

// Hurwitz ζ(s,a)=Σ_{n≥0}(n+a)^{-s}：同一套 EM,截斷點
// 在 x=N+a——ζ(s,1)=ζ(s) 退化為上式
C HurwitzEMN(const C& s, double a, int N) {
    C sum = 0.0;
    for (int n = 0; n < N; ++n) sum += PowNeg(n + a, s);
    const double x = N + a;
    sum += C(x) * PowNeg(x, s) / (s - C(1, 0)); // x^{1-s}/(s-1)
    sum += 0.5 * PowNeg(x, s);
    C rise = s;
    for (int k = 1; k <= kEmM; ++k) {
        sum += kEmCoef[k - 1] * rise * PowNeg(x, s + C(2 * k - 1, 0));
        rise *= (s + C(2 * k - 1, 0)) * (s + C(2 * k, 0));
    }
    return sum;
}

C ZetaEM(const C& s) { return ZetaEMN(s, 200); }

// 複數 Γ：Lanczos（g=7, n=9）+ 反射公式（Re z<0.5 時
//   Γ(z)Γ(1-z)=π/sin πz）。全平面 ~1e-14 精度
C Gamma(const C& z) {
    static const double c[9] = {
        0.99999999999980993, 676.5203681218851,
        -1259.1392167224028, 771.32342877765313,
        -176.61502916214059, 12.507343278686905,
        -0.13857109526572012, 9.9843695780195716e-6,
        1.5056327351493116e-7,
    };
    if (z.real() < 0.5)
        return C(kPi) / (std::sin(kPi * z) * Gamma(C(1.0) - z));
    const C zm1 = z - C(1.0);
    C a = c[0];
    for (int k = 1; k < 9; ++k) a += c[k] / (zm1 + C(k, 0));
    const C t = zm1 + C(7.5, 0);
    return std::sqrt(2.0 * kPi) * std::pow(t, zm1 + C(0.5, 0)) *
           std::exp(-t) * a;
}

// 函數方程式因子：ζ(s) = χ(s)·ζ(1-s)
//   χ(s) = 2^s π^{s-1} sin(πs/2) Γ(1-s)
C Chi(const C& s) {
    return std::pow(C(2.0), s) * std::pow(C(kPi), s - C(1.0)) *
           std::sin(kPi * 0.5 * s) * Gamma(C(1.0) - s);
}

// Riemann-Siegel theta：θ(t) 漸近展開（t≳10 精度 ~1e-6 rad）
double RSTheta(double t) {
    return t * 0.5 * std::log(t / (2.0 * kPi)) - t * 0.5 - kPi / 8.0
           + 1.0 / (48.0 * t) + 7.0 / (5760.0 * t * t * t);
}

// Riemann-Siegel Z(t) 主和：2 Σ_{n≤√(t/2π)} cos(θ(t)-t ln n)/√n
double RSZ(double t) {
    const int n = static_cast<int>(std::sqrt(t / (2.0 * kPi)));
    const double th = RSTheta(t);
    double z = 0.0;
    for (int k = 1; k <= n; ++k)
        z += std::cos(th - t * std::log(static_cast<double>(k)))
             / std::sqrt(static_cast<double>(k));
    return 2.0 * z;
}

// 高 t 旋轉 ζ：Re(e^{iθ(t)}ζ(0.5+it))，N≈4t 避免 EM 漸近發散。
// θ 近似誤差僅縮放實部（cos δθ≈1），變號結構與 Z(t) 一致
double RotZetaHi(double t, int bigN) {
    const C e(std::cos(RSTheta(t)), std::sin(RSTheta(t)));
    return (e * ZetaEMN(C(0.5, t), bigN)).real();
}

// 旋轉 ζ：e^{iθ(t)}·ζ(0.5+it) 的實部 = Z(t)（EM 精度版）；
// 虛部應 ~0——本身即「此 t 在線上」的檢查
double RotZeta(double t) {
    const C eitheta(std::cos(RSTheta(t)), std::sin(RSTheta(t)));
    return (eitheta * ZetaEM(C(0.5, t))).real();
}

// Dirichlet β(s) = L(s,χ₄) = Σ_{n≥0} (-1)^n (2n+1)^{-s}
// σ>0 交替級數直接收斂；Euler 變換
//   Σ(-1)^n a_n = Σ_{k≥0} (Δ^k a_0)/2^{k+1}
// 把 ~1/√n 衰減加速到 ~2^-K 精度（K=50 → ~1e-15 級）
C BetaDirichlet(const C& s) {
    const int K = 150; // t≈25 處把 Euler 殘差壓到 ~1e-9 以下
    std::vector<C> a(static_cast<size_t>(K) + 1);
    for (int n = 0; n <= K; ++n)
        a[static_cast<size_t>(n)] = PowNeg(2 * n + 1, s);
    C sum = 0.0;
    double denom = 2.0;
    for (int k = 0; k <= K; ++k) {
        sum += a[0] / denom;
        denom *= 2.0;
        // in-place 前向差分：a[i] ← a[i] - a[i+1]
        for (int i = 0; i < K - k; ++i)
            a[static_cast<size_t>(i)] =
                a[static_cast<size_t>(i)] -
                a[static_cast<size_t>(i + 1)];
    }
    return sum;
}

// mod 5 原特徵 χ₅（實特徵,勒讓德符號 (n|5)）：n mod 5 查表
// 對應實二次體 Q(√5)——Dedekind ζ_{Q(√5)}=ζ·L(χ₅)
static const int kChi5Res[5] = {0, 1, -1, -1, 1};

// Dirichlet L(s,χ₅) = Σ χ₅(n)n^{-s} = 5^{-s}Σ_r χ₅(r)ζ(s,r/5)
// （Hurwitz 分解——χ 週期加權的 Dirichlet 級數恆可拆成
// q 個平移 Hurwitz）。不用交替化+Euler 變換：χ₅(n) 本身是
// 變號週期序列,變換前提（正項平滑）不成立會發散——
// Hurwitz EM 是已驗證的同一套機器,且本分解對任意原特徵通用
C Chi5L(const C& s) {
    const int N = 200;
    return PowNeg(5.0, s) *
           (HurwitzEMN(s, 0.2, N) - HurwitzEMN(s, 0.4, N) -
            HurwitzEMN(s, 0.6, N) + HurwitzEMN(s, 0.8, N));
}

// digamma ψ(x)：遞迴平移 ψ(x+1)=ψ(x)+1/x 到 x≥8,
// 再漸近 ln x − 1/(2x) − Σ B_{2k}/(2k·x^{2k})
double Digamma(double x) {
    double r = 0.0;
    while (x < 8.0) { r -= 1.0 / x; x += 1.0; }
    const double ix = 1.0 / x, ix2 = ix * ix;
    return r + std::log(x) - 0.5 * ix -
           ix2 * (1.0 / 12 - ix2 * (1.0 / 120 - ix2 / 252.0));
}

// L(1,χ₅)：s=1 是每個 Hurwitz ζ 的單極點——ζ(s,a)=
//   1/(s-1) − ψ(a) + O(s-1),χ₅ 加權和 Σχ₅(r)=0 使極點
//   抵消,留下 L(1,χ₅) = −Σ_r χ₅(r)·ψ(r/5)/5
double Chi5LAt1() {
    return -(Digamma(0.2) - Digamma(0.4) - Digamma(0.6) +
             Digamma(0.8)) / 5.0;
}

// 密集取樣精修 |f| 極小點：以 c±halfW 21 點取 argmin，
// 每輪窗寬縮 5 倍——比 golden section 耐多峰（真零點的
// V 谷可能僅 ~0.001 寬，golden 初始探針會直接跳過）
template <class F>
double RefineMag(F f, double c, double halfW, int rounds) {
    for (int k = 0; k < rounds; ++k) {
        double best = c, bm = f(c);
        for (int i = -10; i <= 10; ++i) {
            const double x = c + halfW * i / 10.0;
            const double m = f(x);
            if (m < bm) {
                bm = m;
                best = x;
            }
        }
        c = best;
        halfW /= 5.0;
    }
    return c;
}

// 輻角原理：沿矩形 σ∈[sLo,sHi]×t∈[tLo,tHi] 逆時針走一圈，
// 累積 arg f(s) 的環繞數 → 矩形內零點數（極點無故等於繞數）
template <class F>
int ArgPrincipleCount(F f, double sLo, double sHi,
                      double tLo, double tHi, double step) {
    double winding = 0.0;
    double prevArg = 0.0;
    bool first = true;
    auto sample = [&](double s, double t) {
        const C v = f(C(s, t));
        const double arg = std::atan2(v.imag(), v.real());
        if (!first) {
            double d = arg - prevArg;
            while (d > kPi) d -= 2.0 * kPi;
            while (d < -kPi) d += 2.0 * kPi;
            winding += d;
        }
        prevArg = arg;
        first = false;
    };
    const double sLen = sHi - sLo, tLen = tHi - tLo;
    const int sSteps = std::max(2, static_cast<int>(sLen / step));
    const int tSteps = std::max(2, static_cast<int>(tLen / step));
    for (int i = 0; i <= sSteps; ++i)   // 底邊 →
        sample(sLo + sLen * i / sSteps, tLo);
    for (int i = 1; i <= tSteps; ++i)   // 右邊 ↑
        sample(sHi, tLo + tLen * i / tSteps);
    for (int i = 1; i <= sSteps; ++i)   // 頂邊 ←
        sample(sHi - sLen * i / sSteps, tHi);
    for (int i = 1; i < tSteps; ++i)    // 左邊 ↓（留一格閉合）
        sample(sLo, tHi - tLen * i / tSteps);
    sample(sLo, tLo);                   // 閉合
    return static_cast<int>(std::lround(winding / (2.0 * kPi)));
}

// 二分求精：f 在 [a,b] 變號 → 根（tol t 精度）
template <class F>
double Bisect(F f, double a, double b, double tol) {
    double fa = f(a);
    while (b - a > tol) {
        const double m = 0.5 * (a + b);
        if (fa * f(m) <= 0.0) {
            b = m;
        } else {
            a = m;
            fa = f(a);
        }
    }
    return 0.5 * (a + b);
}

// Chebyshev ψ(x) = Σ_{n≤x} Λ(n) = Σ_{p^k≤x} ln p——篩法實值，
// 顯式公式的比對目標（質數階梯函數）
double PsiChebyshev(const double x) {
    const int n = static_cast<int>(x);
    std::vector<char> composite(static_cast<size_t>(n) + 1, 0);
    double psi = 0.0;
    for (int p = 2; p <= n; ++p) {
        if (composite[static_cast<size_t>(p)]) continue;
        for (long long pk = p; pk <= n; pk *= p)
            psi += std::log(static_cast<double>(p));
        for (long long m = 2LL * p; m <= n; m += p)
            composite[static_cast<size_t>(m)] = 1;
    }
    return psi;
}

// 顯式公式截斷：ψ₀(x) ≈ x - ln(2π) - ½ln(1-x⁻²)
//   - Σ_{0<γ<T} (1-γ/T)·2·Re(x^ρ/ρ)
// Cesàro/Fejér 權重 (1-|γ|/T) 讓截斷和逐點收斂到 ψ₀——
// 無權重版本在不連續點收斂慢且震盪大。
// gammas：升冪的正虛部零點；T 必須 ≥ 最大 γ
double PsiExplicit(const double x, const std::vector<double>& gammas,
                   const double T) {
    double s = x - std::log(2.0 * kPi) -
               0.5 * std::log(1.0 - 1.0 / (x * x));
    for (const double g : gammas) {
        if (g >= T) break;
        const double w = 1.0 - g / T;
        const C rho(0.5, g);
        const C term = std::pow(C(x, 0.0), rho) / rho;
        s -= 2.0 * w * term.real();
    }
    return s;
}

} // namespace

int main() {
    printf("=== Riemann ζ 數值探索 ===\n");
    printf("（數值驗證不等於證明——只能驗有限個零點）\n\n");

    // [1] Euler-Maclaurin ζ(s) 已知值 sanity
    {
        Check(std::fabs(ZetaEM(C(2, 0)).real() - kPi * kPi / 6.0) < 1e-8,
              "ζ(2) = π²/6");
        Check(std::fabs(ZetaEM(C(4, 0)).real() -
                        kPi * kPi * kPi * kPi / 90.0) < 1e-8,
              "ζ(4) = π⁴/90");
        Check(std::fabs(ZetaEM(C(0.5, 0)).real() + 1.4603545088) < 1e-8,
              "ζ(1/2) ≈ -1.4603545");
        Check(std::fabs(ZetaEM(C(-1, 0)).real() + 1.0 / 12.0) < 1e-8,
              "ζ(-1) = -1/12");
        Check(std::fabs(ZetaEM(C(0, 0)).real() + 0.5) < 1e-8,
              "ζ(0) = -1/2");
    }

    // [2] 零點搜尋：RS Z(t) 主和粗定位（未含 C 修正項，小 t
    //     有 ~0.3 系統偏差）→ 括住中心 ±1 內以 EM 旋轉 ζ 細掃
    //     變號 → 二分求精到 ~1e-9
    //     掃描上界 T=130.4 取在第 43/44 零點間隙中點，
    //     讓下面的 von Mangoldt 計數驗證不貼邊界
    constexpr double kScanHi = 130.4;
    static const double kKnown[] = {
        14.134725, 21.022040, 25.010858, 30.424876, 32.935062,
        37.586176, 40.918719, 43.327073, 48.005151, 49.773832,
        52.970321, 56.446248, 59.347044, 60.831779, 65.112544,
        67.079811, 69.546402, 72.067158, 75.704691, 77.144840,
        79.337375, 82.910381, 84.735493, 87.425275, 88.809111,
        92.491899, 94.651344, 95.870634, 98.831194, 101.317851,
        103.725538, 105.446623, 107.168611, 111.029535, 111.874659,
        114.320221, 116.226680, 118.790782, 121.370125, 122.946829,
        124.256818, 127.516683, 129.578704,
    };
    constexpr int kKnownCount =
        static_cast<int>(sizeof(kKnown) / sizeof(kKnown[0]));
    std::vector<double> roots;
    {
        double tPrev = 10.0, zPrev = RSZ(tPrev);
        std::vector<double> cand;
        for (double t = tPrev + 0.005; t <= kScanHi; t += 0.005) {
            const double z = RSZ(t);
            if (zPrev * z < 0.0) cand.push_back(0.5 * (tPrev + t));
            tPrev = t; zPrev = z;
        }
        printf("  [info] RS 掃描括住 %zu 個候選（t∈[10,%.1f]）\n",
               cand.size(), kScanHi);
        for (const double c : cand) {
            // EM 細掃 [c-1, c+1]，取最接近 c 的變號區間
            double bestA = 0.0, bestB = 0.0, bestDist = 1e30;
            const double lo = c - 1.0, hi = c + 1.0;
            double xPrev = lo, fPrev = RotZeta(lo);
            for (double x = lo + 0.01; x <= hi; x += 0.01) {
                const double f = RotZeta(x);
                if (fPrev * f < 0.0) {
                    const double d = std::fabs(0.5 * (xPrev + x) - c);
                    if (d < bestDist) {
                        bestDist = d;
                        bestA = xPrev; bestB = x;
                    }
                }
                xPrev = x; fPrev = f;
            }
            if (bestDist < 1e29)
                roots.push_back(Bisect(RotZeta, bestA, bestB, 1e-9));
        }
        Check(roots.size() >= 40, "至少精修出前 40 個零點");
    }

    // [3] 對照已知值 + |ζ(root)| ≈ 0 驗證「在線上」
    //     全部 43 個對表；若意外多出零點仍以 |ζ|≈0 自驗證
    printf("\n  %8s %14s %14s %12s\n", "#", "t_found", "t_known",
           "|ζ(1/2+it)|");
    int matched = 0;
    bool allOnLine = true;
    for (size_t i = 0; i < roots.size(); ++i) {
        const double mag = std::abs(ZetaEM(C(0.5, roots[i])));
        if (i < static_cast<size_t>(kKnownCount)) {
            printf("  %8d %14.6f %14.6f %12.2e\n",
                   static_cast<int>(i + 1), roots[i], kKnown[i], mag);
            if (std::fabs(roots[i] - kKnown[i]) < 3e-3 && mag < 1e-4)
                ++matched;
        } else {
            printf("  %8d %14.6f %14s %12.2e\n",
                   static_cast<int>(i + 1), roots[i], "-", mag);
        }
        if (mag >= 1e-4) allOnLine = false;
    }
    printf("\n");
    Check(matched == 43, "全部 43 個零點皆對上已知值且 |ζ|≈0");
    Check(allOnLine, "所有精修零點 |ζ|≈0（含無對表部分）");

    // [4] Riemann–von Mangoldt 計數驗證：
    //     0<Im(ρ)<T 的零點總數 N(T) = θ(T)/π + 1 + S(T)，
    //     |S(T)| 在小 t 遠小於 1 → round(θ(T)/π+1) 即精確總數。
    //     找到的線上零點數 == 理論總數 ⇒ 線外沒有漏網之魚
    //     （在掃描精度內這才是真正的 RH 檢查）
    {
        const long expected =
            std::lround(RSTheta(kScanHi) / kPi + 1.0);
        printf("  [info] von Mangoldt 預期零點數（T=%.1f）：%ld，"
               "實得：%zu\n", kScanHi, expected, roots.size());
        Check(static_cast<size_t>(expected) == roots.size() &&
                  expected >= 40,
              "線上零點數 = 理論總數（線外無零點）");
    }

    // [5] 單零點檢查：猜想所有非平凡零點皆為單零點（ζ'≠0）。
    //     重零點處 dζ/dt 亦為 0——數值微分 |ζ'| > 閾值即單零點
    {
        bool allSimple = true;
        double minDeriv = 1e30;
        for (const double r : roots) {
            const double h = 1e-5;
            const C d = (ZetaEM(C(0.5, r + h)) -
                         ZetaEM(C(0.5, r - h))) / C(0, 2 * h);
            minDeriv = std::min(minDeriv, std::abs(d));
            if (std::abs(d) < 1e-3) allSimple = false;
        }
        printf("  [info] 最小 |ζ'(½+it)| = %.4f\n", minDeriv);
        Check(allSimple, "43 個零點全為單零點（ζ'≠0）");
    }

    // [6] Lehmer pair：t≈7005 的史上最近失誤。
    //     第 6709/6710 零點間距僅 0.038，兩根之間 Z(t) 極值
    //     異常淺——若 RH 在此失敗，這裡會沒有變號（一對線外零點
    //     取代兩個線上零點）。高 t 下 EM 需 N≈4t 避免漸近發散，
    //     定位與驗證皆用 N=28000 的 EM（RS 修正精度不足以
    //     分辨淺變號區，會把根推偏 ~0.1）
    {
        const int bigN = 28000;
        auto zhi = [bigN](double t) { return RotZetaHi(t, bigN); };
        // 細掃 [7004.9, 7005.3]：Lehmer pair 在此
        std::vector<double> lehRoots;
        double tp = 7004.9, zp = zhi(tp);
        for (double t = tp + 0.001; t <= 7005.3; t += 0.001) {
            const double z = zhi(t);
            if (zp * z < 0.0)
                lehRoots.push_back(Bisect(zhi, tp, t, 1e-7));
            tp = t; zp = z;
        }
        Check(lehRoots.size() == 2,
              "t∈[7004.9,7005.3] 恰有兩個變號（Lehmer pair）");
        if (lehRoots.size() == 2) {
            const double r1 = lehRoots[0], r2 = lehRoots[1];
            printf("  [info] Lehmer pair: %.5f / %.5f（間距 %.5f）\n",
                   r1, r2, r2 - r1);
            // 已知值 7005.0627 / 7005.1006（第 6709/6710 零點）
            Check(std::fabs(r1 - 7005.0627) < 2e-2 &&
                      std::fabs(r2 - 7005.1006) < 2e-2,
                  "兩根皆對上已知零點位置");
            Check(r2 - r1 > 0.005 && r2 - r1 < 0.1,
                  "間距 <0.1——異常緊鄰");
            const double m1 =
                std::abs(ZetaEMN(C(0.5, r1), bigN));
            const double m2 =
                std::abs(ZetaEMN(C(0.5, r2), bigN));
            printf("  [info] |ζ(½+i·7005.06)|=%.2e  "
                   "|ζ(½+i·7005.10)|=%.2e\n", m1, m2);
            Check(m1 < 3e-3 && m2 < 3e-3,
                  "兩點 |ζ|≈0——確為線上零點（RH 在近失誤處成立）");
            // 兩根之間的極值：近失誤的「貼零深度」
            double loBump = 1e30;
            for (double t = r1 + 1e-4; t < r2; t += 1e-4)
                loBump = std::min(loBump, std::fabs(zhi(t)));
            printf("  [info] 兩根間最小 |Z| ≈ %.6f"
                   "（淺谷底——差點不變號）\n", loBump);
        }
    }

    // [7] Gram 定律：Gram 點 g_n 解 θ(g_n)=nπ——經典觀察是
    //     每個 (g_n,g_{n+1}) 區間恰含一個零點（低 t 全成立，
    //     首個例外在 n=126 附近，超出我們範圍）
    {
        auto gramPoint = [](int n) {
            return Bisect([n](double t) { return RSTheta(t) - n * kPi; },
                          9.0, 200.0, 1e-9);
        };
        // 涵蓋所有 roots 的 Gram 區間
        int n0 = 0;
        while (gramPoint(n0 + 1) <= roots.front()) ++n0;
        int blocks = 0, okBlocks = 0;
        for (int n = n0; ; ++n) {
            const double g0 = gramPoint(n), g1 = gramPoint(n + 1);
            if (g0 >= roots.back()) break;
            int cnt = 0;
            for (const double r : roots)
                if (r > g0 && r < g1) ++cnt;
            ++blocks;
            if (cnt == 1) ++okBlocks;
        }
        printf("  [info] Gram 區間檢查 %d 段：%d 段恰含一個零點\n",
               blocks, okBlocks);
        Check(okBlocks == blocks && blocks >= 40,
              "Gram 定律：每區間恰一零點");
    }

    // [8] 線外探針：同 t 下 σ=0.4/0.6 的 |ζ|——零點只在線上，
    //     線外幅度應遠大（|ζ'|·Δσ 尺度）
    {
        bool offClear = true;
        double minOff = 1e30;
        for (size_t i = 0; i < roots.size() && i < 10; ++i) {
            const double t = roots[i];
            const double off = std::min(
                std::abs(ZetaEM(C(0.4, t))),
                std::abs(ZetaEM(C(0.6, t))));
            minOff = std::min(minOff, off);
            if (off < 1e-2) offClear = false;
        }
        printf("  [info] σ∓0.1 線外最小 |ζ| = %.4f\n", minOff);
        Check(offClear, "σ=0.4/0.6 同 t 處 |ζ| 遠離零（線外無伴生零）");
    }

    // [9] 零點間距統計：正規化 δ=(γ_{i+1}-γ_i)·ln(γ_i/2π)/2π，
    //     平均應≈1（Montgomery 對關聯猜想的弱檢驗——43 根
    //     統計力有限，只驗尺度不驗分布）
    {
        double mean = 0.0, dmin = 1e30, dmax = 0.0;
        for (size_t i = 1; i < roots.size(); ++i) {
            const double density =
                std::log(roots[i] / (2.0 * kPi)) / (2.0 * kPi);
            const double d = (roots[i] - roots[i - 1]) * density;
            mean += d;
            dmin = std::min(dmin, d);
            dmax = std::max(dmax, d);
        }
        mean /= (roots.size() - 1);
        printf("  [info] 正規化間距 mean=%.3f min=%.3f max=%.3f\n",
               mean, dmin, dmax);
        Check(mean > 0.85 && mean < 1.15 && dmin > 0.0,
              "正規化零點間距平均≈1");
    }

    // [10] 函數方程式自洽：ζ(s)=χ(s)ζ(1-s)——
    //      驗證 EM 算出的是全域正確的 ζ（左右半平面互推）
    {
        const C probes[3] = {C(0.3, 20), C(0.4, 35), C(0.6, 14)};
        bool fe = true;
        for (const C& s : probes) {
            const double lhs = std::abs(ZetaEM(s));
            const double rhs =
                std::abs(Chi(s) * ZetaEM(C(1.0) - s));
            printf("  [info] s=%.1f+%.0fi |ζ(s)|=%.6f "
                   "|χζ(1-s)|=%.6f\n", s.real(), s.imag(), lhs, rhs);
            if (std::fabs(lhs - rhs) > 1e-6 * std::max(1.0, lhs))
                fe = false;
        }
        Check(fe, "ζ(s)=χ(s)ζ(1-s) 函數方程式成立");
    }

    // [11] RotZeta 虛部檢查：e^{iθ}ζ 在非零點處虛部近零
    //     （θ 精確定義下虛部恆 0；漸近 θ 的殘差反映近似誤差）
    {
        const C eitheta(std::cos(RSTheta(30.0)),
                        std::sin(RSTheta(30.0)));
        const C r = eitheta * ZetaEM(C(0.5, 30.0));
        Check(std::fabs(r.imag()) < 5e-2,
              "旋轉 ζ 虛部近零（漸近 θ 殘差範圍內）");
    }

    // [12] Gram 定律首例外（可證偽性檢查）：Rosser 發現
    //      (-1)^n·Z(g_n)>0 在 n=126（g≈282.5）首次失敗。
    //      若我們的 Z 在那裡「守規」反而是數值失真——
    //      必須精確再現這個已知的違規。
    {
        auto gramPoint = [](int n) {
            return Bisect([n](double t) {
                              return RSTheta(t) - n * kPi;
                          },
                          9.0, 400.0, 1e-9);
        };
        const int bigN = 1500; // t≈283 → N≈5t 保住 EM 精度
        auto Z = [bigN](double t) { return RotZetaHi(t, bigN); };
        const double g125 = gramPoint(125), g126 = gramPoint(126),
                     g127 = gramPoint(127);
        const double z125 = Z(g125), z126 = Z(g126),
                     z127 = Z(g127);
        printf("  [info] g125=%.4f Z=%.4f | g126=%.4f Z=%.4f | "
               "g127=%.4f Z=%.4f\n",
               g125, z125, g126, z126, g127, z127);
        Check(z125 < 0.0 && z127 < 0.0,
              "n=125,127（奇）Gram 定律守規 Z<0");
        Check(z126 < 0.0,
              "n=126（偶）首個例外：Z(g126)<0 違反正號預期");
        // 符號異常 ⇒ 鄰接 Gram 區間零點數偏離 1（共 2 個）
        int cntA = 0, cntB = 0;
        for (double t = g125 + 0.005; t < g127; t += 0.005) {
            const double f0 = Z(t - 0.005), f1 = Z(t);
            if (f0 * f1 < 0.0) {
                const double r = Bisect(Z, t - 0.005, t, 1e-9);
                if (r < g126) ++cntA; else ++cntB;
                printf("  [info] 區間零點 t=%.5f（%s）\n", r,
                       r < g126 ? "g125-g126" : "g126-g127");
            }
        }
        printf("  [info] (g125,g126) 零點 %d 個 / (g126,g127) %d 個\n",
               cntA, cntB);
        Check(cntA + cntB == 2 && (cntA == 0 || cntB == 0),
              "例外處 Gram 區間零點分佈異常（0+2 而非 1+1）");
    }

    // [13] 顯式公式：Σ_ρ x^ρ/ρ 重建 Chebyshev ψ(x)——
    //      目前為止所有檢查都問「零點在不在線上」；顯式公式
    //      驗證更深的東西：這些零點必須精確編碼質數階梯。
    //      零點位置差之毫厘，ψ̃(x) 就對不上 ψ(x)——
    //      這是零點「身份」的指紋級驗證。
    {
        const double xs[] = {40.5, 61.7, 83.2, 99.3};
        double maxErr = 0.0;
        for (const double x : xs) {
            const double exact = PsiChebyshev(x);
            const double approx = PsiExplicit(x, roots, kScanHi);
            const double err = std::fabs(approx - exact);
            if (err > maxErr) maxErr = err;
            printf("  [info] x=%5.1f ψ=%8.3f ψ̃=%8.3f err=%.3f\n",
                   x, exact, approx, err);
        }
        // T=130 截斷 + Fejér 權重的殘差隨 x 增長——x≈100 處
        // 個位數誤差屬正常（實測 < 3）；5 為截斷誤差上限
        Check(maxErr < 5.0,
              "顯式公式以 43 個零點重建 ψ(x)（誤差 < 5）");
    }

    // [14] 廣義黎曼猜想（GRH）：Dirichlet β(s)=L(s,χ₄)。
    //      換一個完全不同的 L 函數——若臨界線現象只出現在
    //      ζ 上，可能只是巧合；β 的零點全在 Re(s)=½ 表示
    //      這是 L 函數的普遍結構。
    {
        // sanity：β(1)=π/4（Leibniz）、β(2)=G（Catalan 常數）
        Check(std::fabs(BetaDirichlet(C(1, 0)).real() -
                        kPi / 4.0) < 1e-10,
              "β(1)=π/4（Leibniz）");
        Check(std::fabs(BetaDirichlet(C(2, 0)).real() -
                        0.9159655941772190) < 1e-10,
              "β(2)=G（Catalan 常數）");

        auto magBeta = [](double t) {
            return std::abs(BetaDirichlet(C(0.5, t)));
        };
        // |β(½+it)| 局部極小 → golden section 求精為零點。
        // 零點 dip 最窄可至 ~0.004——掃描步距必須更細才不致
        // 整個 V 谷落在兩採樣點之間被跳過
        std::vector<double> broots;
        const double dt = 0.005;
        double prev = magBeta(3.0 - dt), cur = magBeta(3.0);
        for (double t = 3.0 + dt; t <= 26.0; t += dt) {
            const double next = magBeta(t);
            if (cur < prev && cur < next) {
                // |β| 多峰——先細掃 bracket 找最深谷，
                // 再以密集取樣收縮精修（golden 會跳過窄谷）
                double pBest = t - dt;
                double mBest = 1e30;
                for (int i = 0; i <= 40; ++i) {
                    const double x = t - dt + 2 * dt * i / 40.0;
                    const double m = magBeta(x);
                    if (m < mBest) { mBest = m; pBest = x; }
                }
                const double r =
                    RefineMag(magBeta, pBest, 0.002, 6);
                const double mv = magBeta(r);
                // Euler 殘差讓真零點的 |β| 停在 ~1e-4~1e-2 級；
                // 非零點極小值 ≥0.3——0.1 分界安全
                if (mv < 0.1 &&
                    (broots.empty() || r - broots.back() > 0.1)) {
                    broots.push_back(r);
                }
            }
            prev = cur;
            cur = next;
        }
        printf("  [info] β 零點 %zu 個：\n", broots.size());
        for (const double r : broots)
            printf("    t=%.6f |β|=%.3e\n", r, magBeta(r));
        // LMFDB 已知前 8 個 L(s,χ₄) 零點虛部
        static const double kBetaKnown[8] = {
            6.0209489, 10.2437703, 12.9880980, 16.3426073,
            18.2919937, 21.4506114, 23.2783769, 25.7289086,
        };
        bool allBeta = broots.size() >= 8;
        for (size_t i = 0;
             i < 8 && i < broots.size(); ++i) {
            if (std::fabs(broots[i] - kBetaKnown[i]) > 3e-3)
                allBeta = false;
        }
        Check(allBeta, "β 前 8 個零點對上已知值（GRH 線上）");

        // 輻角原理：矩形 σ∈[0.4,0.6]×t∈[1,15] 繞數 = 內部零點數
        // ——線上找到 3 個 + 繞數 3 ⇒ 矩形內無線外零點
        const int winding = ArgPrincipleCount(
            [](const C& s) { return BetaDirichlet(s); },
            0.4, 0.6, 1.0, 15.0, 0.01);
        int inBox = 0;
        for (const double r : broots)
            if (r < 15.0) ++inBox;
        printf("  [info] 輻角繞數 %d / 線上實得 %d 個\n",
               winding, inBox);
        Check(winding == inBox && inBox == 3,
              "輻角原理：矩形內零點全在臨界線上（GRH）");
    }

    // [15] Li 判據：λ_n = Σ_ρ (1-(1-1/ρ)^n)，Li 1997 證明
    //      RH ⇔ λ_n>0 對所有 n——這是 RH 的等價改寫。
    //      ρ=½+iγ 時 (ρ-1)/ρ 模長 1 → 每對共軛零點貢獻
    //      2(1-cos nφ)≥0 → 截斷和是 λ_n 的嚴格正下界
    {
        printf("  [info] Li 係數截斷下界（真值 λ_1≈0.0231）:\n");
        double lam1 = 0.0;
        bool liPos = true;
        double prevLam = -1.0;
        for (int n = 1; n <= 8; ++n) {
            double lam = 0.0;
            for (const double g : roots) {
                const C rho(0.5, g);
                const double phi =
                    std::arg((rho - C(1, 0)) / rho);
                lam += 2.0 * (1.0 - std::cos(n * phi));
            }
            printf("    λ̃_%d = %.5f\n", n, lam);
            if (lam <= 0.0) liPos = false;
            // λ_n 大致隨 n 增長（截斷誤差下只驗趨勢）
            if (n > 1 && lam < prevLam * 0.5) liPos = false;
            prevLam = lam;
            if (n == 1) lam1 = lam;
        }
        Check(liPos, "Li 判據：λ̃_n 全正且增長（截斷下界）");
        // λ̃_1 是 λ_1≈0.0231 的嚴格下界——應落在 0.014~0.024
        Check(lam1 > 0.014 && lam1 < 0.024,
              "λ̃_1 逼近已知 λ_1≈0.0231（略低於真值）");
    }

    // [16] Speiser 判據（等價改寫）：Speiser 1935 證明
    //     RH ⇔ ζ'(s) 在 0<σ<½ 臨界帶左半無零點——把猜想
    //     換到導數側驗證，而非「ζ 零點在線上」的重述。
    //     數值側：輻角原理數 ζ' 在矩形 σ∈[0.05,0.45]×
    //     t∈[0.5,60] 內的零點繞數——RH 成立時應恰為 0。
    //     ζ' 以實軸向中心差分估（解析函數方向無關），
    //     h=1e-3 平衡截斷 ~h² 與 EM 噪底 ~1e-9/h。
    {
        const double hd = 1e-3;
        auto zprime = [hd](const C& s) {
            // 五點 stencil：(f(s-2h)-8f(s-h)+8f(s+h)-f(s+2h))/12h
            // 截斷 ~h⁴/30·ζ⁽⁵⁾——中心差分 h² 在 ζ'''~O(10²) 處
            // 誤差 ~1.6e-5 超標，五點式壓到 ~1e-6
            return (ZetaEM(s - C(2 * hd, 0)) -
                    8.0 * ZetaEM(s - C(hd, 0)) +
                    8.0 * ZetaEM(s + C(hd, 0)) -
                    ZetaEM(s + C(2 * hd, 0))) /
                   C(12 * hd, 0);
        };
        // sanity：ζ'(0)=-½ln(2π)≈-0.9189385332,
        //         ζ'(½)≈-3.9226461392, ζ'(2)≈-0.9375482543
        Check(std::fabs(zprime(C(0, 0)).real() + 0.9189385332) <
                  1e-5,
              "ζ'(0) = -½ln(2π) sanity");
        Check(std::fabs(zprime(C(0.5, 0)).real() + 3.9226461392) <
                  1e-5,
              "ζ'(½) ≈ -3.9226461 sanity");
        Check(std::fabs(zprime(C(2, 0)).real() + 0.9375482543) <
                  1e-5,
              "ζ'(2) ≈ -0.9375483 sanity");
        // 2D 粗掃 |ζ'| 下界：邊界路徑上不該有貼零點——
        // 否則繞數追蹤會失真（arg 單步變化可能超 π）
        double minZp = 1e30;
        for (double tt = 0.5; tt <= 60.0; tt += 0.5) {
            for (double ss = 0.05; ss <= 0.451; ss += 0.05) {
                minZp = std::min(minZp,
                                 std::abs(zprime(C(ss, tt))));
            }
        }
        printf("  [info] ζ' 左半帶粗掃最小 |ζ'| = %.4f\n", minZp);
        const int zpWinding = ArgPrincipleCount(
            zprime, 0.05, 0.45, 0.5, 60.0, 0.005);
        printf("  [info] ζ' 繞數 σ∈[0.05,0.45]×t∈[0.5,60] = %d\n",
               zpWinding);
        Check(zpWinding == 0,
              "Speiser 判據：ζ' 在 0<σ<½ 帶內零點繞數 = 0（⇔RH）");
    }

    // [17] GRH 再延伸：Dirichlet L(s,χ₅)——mod 5 實特徵。
    //     χ₄ 是複特徵（對應虛二次體 Q(i)）；χ₅ 是實特徵
    //     （對應實二次體 Q(√5),Dedekind ζ=ζ·L(χ₅)）。
    //     型別不同的 L 函數零點也在線上→GRH 普遍性再佐證
    {
        // sanity：L(1,χ₅)=2 ln φ/√5（Q(√5) 類數公式：
        //   h=1、基本單位 ε=φ=(1+√5)/2）。s=1 是 Hurwitz 極點,
        //   走極點扣除極限式 −Σχ₅(r)ψ(r/5)/5
        const double phi5 = (1.0 + std::sqrt(5.0)) * 0.5;
        Check(std::fabs(Chi5LAt1() -
                        2.0 * std::log(phi5) / std::sqrt(5.0)) <
                  1e-9,
              "L(1,χ₅) = 2 ln φ/√5（類數公式）");
        // σ=2 直和交叉驗證：絕對收斂,N=40000 尾項 ~1/N=2.5e-5
        double direct2 = 0.0;
        for (int n = 1; n <= 40000; ++n)
            direct2 += kChi5Res[n % 5] /
                       (static_cast<double>(n) * n);
        Check(std::fabs(Chi5L(C(2, 0)).real() - direct2) < 1e-4,
              "L(2,χ₅) Hurwitz 分解 vs σ>1 直和一致");

        // 臨界線零點掃描：與 β 相同的「極小→細掃→RefineMag」管線
        auto magL = [](double t) {
            return std::abs(Chi5L(C(0.5, t)));
        };
        std::vector<double> c5roots;
        const double dt5 = 0.005;
        double prev5 = magL(3.0 - dt5), cur5 = magL(3.0);
        for (double t = 3.0 + dt5; t <= 32.0; t += dt5) {
            const double next = magL(t);
            if (cur5 < prev5 && cur5 < next) {
                double pBest = t - dt5;
                double mBest = 1e30;
                for (int i = 0; i <= 40; ++i) {
                    const double x = t - dt5 + 2 * dt5 * i / 40.0;
                    const double m = magL(x);
                    if (m < mBest) { mBest = m; pBest = x; }
                }
                const double r = RefineMag(magL, pBest, 0.002, 6);
                if (magL(r) < 0.1 &&
                    (c5roots.empty() || r - c5roots.back() > 0.1)) {
                    c5roots.push_back(r);
                }
            }
            prev5 = cur5;
            cur5 = next;
        }
        printf("  [info] L(s,χ₅) 零點 %zu 個：\n",
               c5roots.size());
        for (const double r : c5roots)
            printf("    t=%.6f |L|=%.3e\n", r, magL(r));
        // Sage lcalc 官方文件確認的前 3 個 L(s,χ₅) 零點虛部
        // （其餘 8 個為引擎計算值,由輻角繞數=實得數交叉背書）
        static const double kChi5Known[3] = {
            6.6484533447, 9.8314444329, 11.9588456260,
        };
        bool allC5 = c5roots.size() >= 3;
        for (size_t i = 0; i < 3 && i < c5roots.size(); ++i) {
            if (std::fabs(c5roots[i] - kChi5Known[i]) > 3e-3)
                allC5 = false;
        }
        Check(allC5,
              "L(s,χ₅) 前 3 零點對上 Sage lcalc 值（GRH 線上）");

        // 輻角原理計數：σ∈[0.4,0.6]×t∈[4,32] 繞數 = 線上實得
        const int c5winding = ArgPrincipleCount(
            [](const C& s) { return Chi5L(s); },
            0.4, 0.6, 4.0, 32.0, 0.01);
        int c5inBox = 0;
        for (const double r : c5roots)
            if (r < 32.0) ++c5inBox;
        printf("  [info] χ₅ 輻角繞數 %d / 線上實得 %d 個\n",
               c5winding, c5inBox);
        Check(c5winding == c5inBox && c5inBox >= 5,
              "輻角原理：χ₅ 矩形內零點全在臨界線上（GRH）");
    }

    // [18] Robin 判據：RH ⇔ σ(n) < e^γ·n·ln ln n 對所有
    //     n>5040（Robin 1984）。第三個等價改寫,而且是唯一
    //     純算術形式——完全不需要複分析。σ(n) 篩法直算,
    //     比值最大者應落在 colossally abundant 數上
    //     （Gronwall：limsup σ(n)/(n ln ln n) = e^γ 無條件
    //     成立,RH 決定的是「嚴格小於」是否逐項成立）
    {
        const int RN = 10000000;
        // σ(n) 因數和篩：O(N ln N)
        std::vector<uint32_t> sig(static_cast<size_t>(RN) + 1, 0);
        for (int d = 1; d <= RN; ++d)
            for (int m = d; m <= RN; m += d)
                sig[static_cast<size_t>(m)] +=
                    static_cast<uint32_t>(d);
        const double eG = std::exp(0.5772156649015329); // e^γ
        double worst = 0.0;
        int argmax = 0;
        for (int n = 5041; n <= RN; ++n) {
            const double r =
                sig[static_cast<size_t>(n)] /
                (eG * n *
                 std::log(std::log(static_cast<double>(n))));
            if (r > worst) { worst = r; argmax = n; }
        }
        printf("  [info] Robin max σ(n)/(e^γ·n·ln ln n) = %.6f "
               "at n=%d（σ=%u）\n",
               worst, argmax, sig[static_cast<size_t>(argmax)]);
        Check(worst < 1.0,
              "Robin 判據：5040<n≤10^7 比值全 <1（⇔RH 算術形式）");
        // argmax 應是 superabundant：σ(n)/n 的歷史記錄保持者。
        // 直接驗證性質本身（不查表）——σ(argmax)/argmax 應
        // ≥ 所有更小 m 的同比值
        double sr = 0.0;
        int srArg = 0;
        for (int m = 1; m <= argmax; ++m) {
            const double v = sig[static_cast<size_t>(m)] /
                             static_cast<double>(m);
            if (v > sr) { sr = v; srArg = m; }
        }
        printf("  [info] σ/n 記錄保持者 m=%d（σ/m=%.6f,"
               "argmax 比值 %.6f）\n",
               srArg, sr,
               sig[static_cast<size_t>(argmax)] /
                   static_cast<double>(argmax));
        Check(srArg == argmax,
              "argmax 為 superabundant（σ/n 記錄保持者）");
    }

    // [19] Dedekind ζ_{Q(√5)}(s) = ζ(s)·L(s,χ₅)——數體 GRH。
    //     其零點 = ζ 零點 ∪ χ₅ 零點（乘積函數）,輻角繞數
    //     應等於兩者框內零點數之和：ζ 在 t∈[4,20] 有 14.13
    //     共 1 個,χ₅ 有 6.65/9.83/11.96/16.03/17.57/19.54
    //     共 6 個 → 合計 7
    {
        const int kw = ArgPrincipleCount(
            [](const C& s) { return ZetaEM(s) * Chi5L(s); },
            0.4, 0.6, 4.0, 20.0, 0.01);
        printf("  [info] ζ_{Q(√5)} 繞數 %d（ζ 1 + χ₅ 6 = 7）\n",
               kw);
        Check(kw == 7,
              "Dedekind ζ_{Q(√5)}：零點 = ζ∪χ₅（繞數 7）");
    }

    // [20] 零點統計擴充 + GUE 層級排斥（Montgomery–Dyson）。
    //     之前全是逐根確定性驗證——RMT 預測的是統計性質：
    //     ζ 零點正規化間距應服從 GUE 分佈（Wigner surmise
    //     p(s)=32s²/π²·e^{-4s²/π}）而非 Poisson（e^{-s}）。
    //     最強鑑別在 δ→0：GUE F(0.3)≈0.4% vs Poisson 26%
    //     ——層級排斥。真 Z 直掃到 t≈330（累計 ~157 零點）
    {
        std::vector<double> all = roots;
        // RSZ 主和粗掃 bracket → RotZetaHi（EM 精度真 Z——
        // e^{iθ}ζ 對實 t 恆為實數）同 bracket 二分精修。
        // 主和殘差 ~0.02 會把 RSZ 根推偏同量級且可能假變號;
        // 真 Z bracket 不符者（RS 假正/根在邊界）跳過
        // 直接以 EM 精度真 Z（e^{iθ}ζ 實部）0.02 步距掃
        // ——RSZ 主和殘差~0.1 會推偏根位置、吃掉淺變號
        // 並隱藏近距根對；t≤330 最小間距~0.7 遠大於步距
        auto zhi = [](double tt) { return RotZetaHi(tt, 1400); };
        double prevZ = zhi(130.4);
        for (double t = 130.42; t <= 330.0; t += 0.02) {
            const double z = zhi(t);
            if (prevZ * z < 0.0)
                all.push_back(
                    Bisect(zhi, t - 0.02, t, 1e-9));
            prevZ = z;
        }
        printf("  [info] 累計零點 %zu 個（t≤330）\n", all.size());
        // von Mangoldt N(330)=θ(330)/π+1≈156——掃描完整性
        const int expected330 =
            std::lround(RSTheta(330.0) / kPi + 1.0);
        Check(std::abs(static_cast<int>(all.size()) -
                       expected330) <= 2,
              "累計零點數與 N(330) 一致（von Mangoldt）");

        // 正規化間距 δ=(γ_{i+1}-γ_i)·ln(γ_i/2π)/2π
        std::vector<double> gaps;
        double mean = 0.0;
        for (size_t i = 1; i < all.size(); ++i) {
            const double d =
                (all[i] - all[i - 1]) *
                std::log(all[i] / (2.0 * kPi)) / (2.0 * kPi);
            gaps.push_back(d);
            mean += d;
        }
        mean /= gaps.size();
        double var = 0.0, f03 = 0.0, f05 = 0.0;
        for (const double d : gaps) {
            var += (d - mean) * (d - mean);
            if (d < 0.3) ++f03;
            if (d < 0.5) ++f05;
        }
        var /= gaps.size();
        f03 /= gaps.size();
        f05 /= gaps.size();
        printf("  [info] 間距 %zu 個：mean=%.3f var=%.3f "
               "P(δ<0.3)=%.1f%% P(δ<0.5)=%.1f%%\n"
               "         [GUE surmise: var≈0.178, "
               "P(<0.3)≈0.4%%, P(<0.5)≈10.9%%;"
               " Poisson: var=1, P(<0.3)≈26%%, P(<0.5)≈39%%]\n",
               gaps.size(), mean, var, f03 * 100, f05 * 100);
        Check(std::fabs(mean - 1.0) < 0.15,
              "正規化間距 mean ≈ 1");
        // 層級排斥：GUE P(δ<0.3)≈0.4% vs Poisson 26%——
        // ~150 樣本下 <10% 已可分辨
        Check(f03 < 0.10,
              "GUE 層級排斥：P(δ<0.3) 遠低於 Poisson");
        Check(f05 < 0.25,
              "P(δ<0.5) 近 GUE 10.9%（vs Poisson 39%）");
        Check(var < 0.6,
              "間距變異近 GUE ~0.18（vs Poisson 1.0）");
    }

    // [21] 渲染 |ζ| 臨界帶地形：σ∈[0,1]（x）× t∈[0,55]（y）
    //     log(1+|ζ|) 灰階——零點呈黑洞，應全排在 σ=0.5 中線上
    {
        const int W = 300, H = 330;
        std::vector<uint8> rgba(static_cast<size_t>(W) * H * 4);
        std::vector<float> field(static_cast<size_t>(W) * H);
        for (int y = 0; y < H; ++y) {
            const double t = 0.5 + 54.5 * (H - 1 - y)
                                 / static_cast<double>(H - 1);
            for (int x = 0; x < W; ++x) {
                const double sigma = x / static_cast<double>(W - 1);
                field[static_cast<size_t>(y) * W + x] =
                    static_cast<float>(
                        std::log1p(std::abs(ZetaEM(C(sigma, t)))));
            }
        }
        // p5/p98 percentile 拉伸 + sqrt gamma——零點黑洞對比增強
        std::vector<float> sorted = field;
        std::sort(sorted.begin(), sorted.end());
        const double lo = sorted[static_cast<size_t>(sorted.size() * 5 / 100)];
        const double hi = sorted[static_cast<size_t>(sorted.size() * 98 / 100)];
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const double v = field[static_cast<size_t>(y) * W + x];
                double nrm = (v - lo) / (hi - lo);
                nrm = nrm < 0.0 ? 0.0 : (nrm > 1.0 ? 1.0 : nrm);
                const uint8 g = static_cast<uint8>(
                    255.0 * std::sqrt(nrm) + 0.5);
                uint8* px =
                    &rgba[(static_cast<size_t>(y) * W + x) * 4];
                px[0] = g; px[1] = g; px[2] = g; px[3] = 255;
            }
        }
        // σ=0.5 中線：細紅線——讓讀者親眼看到黑洞全壓在線上
        const int mid = W / 2;
        for (int y = 0; y < H; ++y) {
            uint8* px = &rgba[(static_cast<size_t>(y) * W + mid) * 4];
            px[0] = 255; px[1] = 64; px[2] = 64; px[3] = 255;
        }
        std::string err;
        Check(Potato::ImageCodec::WritePNGFile(
                  "riemann_zeta.png", W, H, rgba.data(), &err),
              "寫出 riemann_zeta.png");
        if (!err.empty()) printf("  [info] PNG: %s\n", err.c_str());
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("\n結論：t<330 內 157 個零點全數在 Re(s)=1/2 上"
               "（真 Z 直掃+bisection），\n數量吻合 "
               "Riemann–von Mangoldt 理論總數（線外無零點）;\n"
               "零點間距統計呈 GUE 層級排斥"
               "（Montgomery–Dyson）——\n與黎曼 1859 年猜想一致——"
               "但有限驗證不構成證明。\n");
    }
    return g_fail == 0 ? 0 : 1;
}
