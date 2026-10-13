#pragma once

/**
 * RiemannZeta — 黎曼 ζ/L 函數數值核心（headless，僅標準庫）。
 *
 * 從 RiemannExplorerTest 抽出的共用機器，供
 * RiemannExplorerTest / RiemannExplorer2Test 共用：
 *
 *   ZetaEMN/HurwitzEMN — Euler-Maclaurin 求和（N 參數化，
 *                        高 t 需 N≳4t 否則漸近發散）
 *   Gamma/Chi          — Lanczos Γ + 函數方程式因子 χ(s)
 *   RSTheta/RSZ        — Riemann-Siegel θ 與主和
 *   RotZeta(Hi)        — e^{iθ(t)}ζ(½+it) 實部 = 真 Z(t)
 *   BetaDirichlet      — Dirichlet β = L(s,χ₄)（Euler 變換）
 *   Chi5L/Chi5LAt1     — L(s,χ₅)（mod 5 實特徵,Hurwitz 分解）
 *   RefineMag/Bisect/ArgPrincipleCount — 尋根與輻角原理計數
 *   PsiChebyshev/PsiExplicit — 顯式公式雙側
 */

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace Potato {
namespace RZ {

using C = std::complex<double>;
inline constexpr double kPi = 3.14159265358979323846;

// n^{-s} = n^{-σ}·e^{-it ln n}（實底複指數，比 std::pow(complex) 快且穩）
inline C PowNeg(double n, const C& s) {
    const double ln = std::log(n);
    const double mag = std::exp(-s.real() * ln);
    const double ph = -s.imag() * ln;
    return C(mag * std::cos(ph), mag * std::sin(ph));
}

// Euler-Maclaurin 修正項係數：B_{2k}/(2k)! 預先折合
//   B2=1/6 B4=-1/30 B6=1/42 B8=-1/30 B10=5/66 B12=-691/2730
//   B14=7/6 B16=-3617/510（ZetaEMN / HurwitzEMN 共用）
inline constexpr int kEmM = 8;
inline const double kEmCoef[kEmM] = {
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
inline C ZetaEMN(const C& s, int N) {
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
inline C HurwitzEMN(const C& s, double a, int N) {
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

inline C ZetaEM(const C& s) { return ZetaEMN(s, 200); }

// 複數 Γ：Lanczos（g=7, n=9）+ 反射公式（Re z<0.5 時
//   Γ(z)Γ(1-z)=π/sin πz）。全平面 ~1e-14 精度
inline C Gamma(const C& z) {
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
inline C Chi(const C& s) {
    return std::pow(C(2.0), s) * std::pow(C(kPi), s - C(1.0)) *
           std::sin(kPi * 0.5 * s) * Gamma(C(1.0) - s);
}

// Riemann-Siegel theta：θ(t) 漸近展開（t≳10 精度 ~1e-6 rad）
inline double RSTheta(double t) {
    return t * 0.5 * std::log(t / (2.0 * kPi)) - t * 0.5 - kPi / 8.0
           + 1.0 / (48.0 * t) + 7.0 / (5760.0 * t * t * t);
}

// Riemann-Siegel Z(t) 主和：2 Σ_{n≤√(t/2π)} cos(θ(t)-t ln n)/√n
inline double RSZ(double t) {
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
inline double RotZetaHi(double t, int bigN) {
    const C e(std::cos(RSTheta(t)), std::sin(RSTheta(t)));
    return (e * ZetaEMN(C(0.5, t), bigN)).real();
}

// 旋轉 ζ：e^{iθ(t)}·ζ(0.5+it) 的實部 = Z(t)（EM 精度版）；
// 虛部應 ~0——本身即「此 t 在線上」的檢查
inline double RotZeta(double t) {
    const C eitheta(std::cos(RSTheta(t)), std::sin(RSTheta(t)));
    return (eitheta * ZetaEM(C(0.5, t))).real();
}

// Dirichlet β(s) = L(s,χ₄) = Σ_{n≥0} (-1)^n (2n+1)^{-s}
// σ>0 交替級數直接收斂；Euler 變換
//   Σ(-1)^n a_n = Σ_{k≥0} (Δ^k a_0)/2^{k+1}
// 把 ~1/√n 衰減加速到 ~2^-K 精度（K=50 → ~1e-15 級）
inline C BetaDirichlet(const C& s) {
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
inline const int kChi5Res[5] = {0, 1, -1, -1, 1};

// Dirichlet L(s,χ₅) = Σ χ₅(n)n^{-s} = 5^{-s}Σ_r χ₅(r)ζ(s,r/5)
// （Hurwitz 分解——χ 週期加權的 Dirichlet 級數恆可拆成
// q 個平移 Hurwitz）。不用交替化+Euler 變換：χ₅(n) 本身是
// 變號週期序列,變換前提（正項平滑）不成立會發散——
// Hurwitz EM 是已驗證的同一套機器,且本分解對任意原特徵通用
inline C Chi5L(const C& s) {
    const int N = 200;
    return PowNeg(5.0, s) *
           (HurwitzEMN(s, 0.2, N) - HurwitzEMN(s, 0.4, N) -
            HurwitzEMN(s, 0.6, N) + HurwitzEMN(s, 0.8, N));
}

// mod 7 實特徵 χ₋₇（Kronecker (n|7)）：二次剩餘 {1,2,4},
// 非剩餘 {3,5,6}——對應虛二次體 Q(√−7)（類數 h=1）
inline const int kChi7Res[8] = {0, 1, 1, -1, 1, -1, -1, 0};

// L(s,χ₋₇) = 7^{−s}Σ_r χ(r)ζ(s,r/7)——同 χ₅ 的 Hurwitz 分解
inline C Chi7L(const C& s) {
    const int N = 200;
    return PowNeg(7.0, s) *
           (HurwitzEMN(s, 1.0 / 7, N) +
            HurwitzEMN(s, 2.0 / 7, N) -
            HurwitzEMN(s, 3.0 / 7, N) +
            HurwitzEMN(s, 4.0 / 7, N) -
            HurwitzEMN(s, 5.0 / 7, N) -
            HurwitzEMN(s, 6.0 / 7, N));
}

// digamma ψ(x)：遞迴平移 ψ(x+1)=ψ(x)+1/x 到 x≥8,
// 再漸近 ln x − 1/(2x) − Σ B_{2k}/(2k·x^{2k})
inline double Digamma(double x) {
    double r = 0.0;
    while (x < 8.0) { r -= 1.0 / x; x += 1.0; }
    const double ix = 1.0 / x, ix2 = ix * ix;
    return r + std::log(x) - 0.5 * ix -
           ix2 * (1.0 / 12 - ix2 * (1.0 / 120 - ix2 / 252.0));
}

// L(1,χ₅)：s=1 是每個 Hurwitz ζ 的單極點——ζ(s,a)=
//   1/(s-1) − ψ(a) + O(s-1),χ₅ 加權和 Σχ₅(r)=0 使極點
//   抵消,留下 L(1,χ₅) = −Σ_r χ₅(r)·ψ(r/5)/5
inline double Chi5LAt1() {
    return -(Digamma(0.2) - Digamma(0.4) - Digamma(0.6) +
             Digamma(0.8)) / 5.0;
}

// L(1,χ₋₇) = −Σ_r χ(r)ψ(r/7)/7（極點扣除,同 χ₅ 推導）
// 類數公式：L(1,χ₋₇)=π/√7（Q(√−7) h=1,w=2）
inline double Chi7LAt1() {
    return -(Digamma(1.0 / 7) + Digamma(2.0 / 7) -
             Digamma(3.0 / 7) + Digamma(4.0 / 7) -
             Digamma(5.0 / 7) - Digamma(6.0 / 7)) /
           7.0;
}

// 密集取樣精修 |f| 極小點：以 c±halfW 21 點取 argmin，
// 每輪窗寬縮 5 倍——比 golden section 耐多峰（真零點的
// V 谷可能僅 ~0.001 寬，golden 初始探針會直接跳過）
template <class F>
inline double RefineMag(F f, double c, double halfW, int rounds) {
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
inline int ArgPrincipleCount(F f, double sLo, double sHi,
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
inline double Bisect(F f, double a, double b, double tol) {
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
inline double PsiChebyshev(const double x) {
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
inline double PsiExplicit(const double x, const std::vector<double>& gammas,
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

// ---- ξ(s) = ½s(s−1)π^{−s/2}Γ(s/2)ζ(s)：整函數，
//      零點恰為非平凡零點；線上實值（Im≈0 自檢）----
inline C Xi(const C& s, int N = 200) {
    return C(0.5) * s * (s - C(1.0)) *
           std::pow(C(kPi), -s * C(0.5)) * Gamma(s * C(0.5)) *
           ZetaEMN(s, N);
}

// ---- de Bruijn–Newman 熱流核 ----
// Φ(u) = Σ_{n≥1} (2π²n⁴e^{9u} − 3πn²e^{5u}) e^{−πn²e^{4u}}（偶函數）
// H_t(z) = ∫_{-∞}^{∞} e^{tu²}Φ(u)cos(zu)du。
// H_0 的零點 = 2γ（ξ 的對角線）；t>Λ 時全部零點為實。
// RH ⇔ Λ≤0；Rodgers–Tao 證 Λ≥0 → RH ⇔ Λ=0。
// Polymath15 證 Λ≤0.22——t=0.2 處零點應仍全實。
//
// 注意：u<0 時 e^{4u}→0 使 Gaussian→1，級數尾部
// ~−3πe^{5u}Σn² 假性發散——負側必須走對稱性。
inline double PhiBN(double u) {
    if (u < 0.0) u = -u;
    const double e4 = std::exp(4.0 * u);
    const double e5 = std::exp(5.0 * u);
    const double e9 = e5 * e4;
    double s = 0.0;
    for (int n = 1; n <= 30; ++n) {
        const double n2 = static_cast<double>(n) * n;
        const double damp = std::exp(-kPi * n2 * e4);
        if (damp < 1e-30) break; // 雙指數衰減——提早截斷
        s += (2.0 * kPi * kPi * n2 * n2 * e9 -
              3.0 * kPi * n2 * e5) * damp;
    }
    return s;
}

// H_t(x) 積分：Φ 是整函數 → 全實軸梯形法則譜精度。
// |u|>2.5 時 e^{4u}>e^{10} → Φ≈0 可截斷。
// 注意：H_0(x) 振幅 ~x^{7/4}e^{−πx/8}——x≳90 時掉到
// ~1e-14 以下進入浮點噪底,掃描範圍必須受此限制
inline double HtFlow(double x, double t) {
    const double h = 0.015, uMax = 2.5;
    const int N = static_cast<int>(uMax / h);
    double s = 0.0;
    for (int i = -N; i <= N; ++i) {
        const double u = i * h;
        s += PhiBN(u) * std::exp(t * u * u) * std::cos(x * u);
    }
    return s * h;
}

// 實函數變號掃描：f 在 [lo,hi] 以 step 掃描,
// 每個變號區間二分求精 → 根列表
template <class F>
inline std::vector<double> ScanRoots(F f, double lo,
                                     double hi, double step) {
    std::vector<double> roots;
    double xp = lo, fp = f(lo);
    for (double x = lo + step; x <= hi; x += step) {
        const double fx = f(x);
        if (fp * fx < 0.0)
            roots.push_back(Bisect(f, xp, x, step * 1e-3));
        xp = x;
        fp = fx;
    }
    return roots;
}

// Dirichlet η(s) = Σ_{n≥1} (−1)^{n−1} n^{−s}——獨立求值器
// （Euler 變換作用於 f(n)=n^{−s}，交替結構外置）。
// η(s)=(1−2^{1−s})ζ(s) 提供雙路徑交叉驗證。
inline C EtaEuler(const C& s) {
    const int K = 120;
    std::vector<C> a(static_cast<size_t>(K) + 1);
    for (int n = 1; n <= K + 1; ++n)
        a[static_cast<size_t>(n - 1)] = PowNeg(n, s);
    C sum = 0.0;
    double denom = 2.0;
    for (int k = 0; k <= K; ++k) {
        sum += a[0] / denom;
        denom *= 2.0;
        for (int i = 0; i < K - k; ++i)
            a[static_cast<size_t>(i)] =
                a[static_cast<size_t>(i)] -
                a[static_cast<size_t>(i + 1)]; // Δa_i = a_i−a_{i+1}
    }
    return sum;
}

} // namespace RZ
} // namespace Potato
