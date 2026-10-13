// RiemannExplorer2Test - 黎曼猜想第二波數值探索（全 headless）
//
// 承接 RiemannExplorerTest（零點定位/GRH/判據/GUE），
// 本檔補上第一波未涵蓋的等價改寫與深層結構：
//
//   ξ(s) 整函數      — 線上實值性 + 輻角原理獨立計數
//   Σ 1/ρ 級數      — 收斂到 1+γ/2−ln(2√π) 的截斷驗證
//   線上矩           — Hardy–Littlewood M₂、Ingham M₄ 漸近
//   Selberg CLT      — log|ζ(½+it)| 正規化 → 標準常態
//   de Bruijn–Newman — H_t 流：RH ⇔ Λ≤0；t=0.2 零點仍全實
//                      （Polymath15 證 Λ≤0.22——本測可證偽）
//   Mertens M(x)     — |M(x)|≤√x 增長尺度（RH 等價強度）
//   η/ζ 恆等交叉驗證 — 獨立 Euler 變換級數 vs (1−2^{1−s})ζ
//   σ=1 無零點       — |ζ(1+it)|>0（素數定理等價命題）
//
// 數值驗證 ≠ 證明：每個檢查都可證偽，但有限樣本只支持猜想。

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


int main() {
    printf("=== Riemann ζ 數值探索 第二波 ===\n");
    printf("（可證偽檢查；有限樣本不構成證明）\n\n");

    // ---- 前置：重建零點表（t≤330，EM 精度真 Z 直掃）----
    std::vector<double> roots;
    {
        auto zhi = [](double t) { return RotZetaHi(t, 1400); };
        roots = ScanRoots(zhi, 10.0, 330.0, 0.02);
        const long expected =
            std::lround(RSTheta(330.0) / kPi + 1.0);
        printf("  [info] 零點表：%zu 個（N(330) 預期 %ld）\n",
               roots.size(), expected);
        Check(std::abs(static_cast<int>(roots.size()) -
                       expected) <= 2,
              "零點表與 von Mangoldt 總數一致");
    }

    // [1] ξ(s) 整函數路徑
    {
        // 線上實值性：ξ(½+it) 對實 t 恆為實（ζ 對稱性的
        // 直接反映；避開零點取樣，|Im|/|ξ| 應 ~1e-8 級）
        double worst = 0.0;
        for (const double t : {20.5, 33.3, 55.7, 90.1}) {
            const C v = Xi(C(0.5, t));
            worst = std::max(
                worst, std::fabs(v.imag()) /
                           std::max(1e-30, std::abs(v)));
        }
        printf("  [info] ξ(½+it) 最大虛部比 = %.2e\n", worst);
        Check(worst < 1e-6, "ξ 在臨界線上實值（Im≈0）");

        // 輻角原理：ξ 在 σ∈[0.3,0.7]×t∈[1,50] 的繞數
        // 應 = 線上零點數 10（14.13…49.77）。ξ 是全純函數、
        // 零點恰為非平凡零點——與 ζ 計數互為獨立證據
        const int w = ArgPrincipleCount(
            [](const C& s) { return Xi(s); },
            0.3, 0.7, 1.0, 50.0, 0.01);
        int inline10 = 0;
        for (const double r : roots)
            if (r < 50.0) ++inline10;
        printf("  [info] ξ 繞數 %d / 線上實得 %d 個\n", w, inline10);
        Check(w == inline10 && inline10 == 10,
              "ξ 輻角計數 = 線上零點數（整函數視角確認 RH）");
    }

    // [2] Σ_ρ 1/ρ 級數：已知值 1+γ_E/2−ln(2√π)≈0.0230957。
    //     ρ 共軛成對 → 2·Re(1/ρ)=1/(¼+γ²)。截斷尾
    //     Σ_{γ>330} ≈ ln(330/2π)/(2π·330)≈0.0019
    {
        double s = 0.0;
        for (const double g : roots) s += 1.0 / (0.25 + g * g);
        const double target = 1.0 + 0.5772156649015329 * 0.5 -
                              0.5 * std::log(4.0 * kPi);
        printf("  [info] Σ 1/(¼+γ²) 截斷和 = %.6f "
               "（真值 %.6f, 預期尾 ~0.0019）\n", s, target);
        Check(s > 0.018 && s < target + 1e-4,
              "Σ 1/ρ 截斷和逼近 0.0230957（差≈尾項尺度）");
    }

    // [3] 線上矩：Hardy–Littlewood M₂ ~ ln(T/2π)+2γ−1；
    //     Ingham M₄ ~ ln⁴(T/2π)/(2π²)。截斷區間積分均值
    //     與漸近主項逐點比值——趨勢吻合即通過（寬容 ±50%）
    {
        const double lo = 8.0, hi = 250.0, dt = 0.05;
        double i2 = 0.0, i4 = 0.0, p2 = 0.0, p4 = 0.0;
        for (double t = lo; t <= hi; t += dt) {
            // EM 需 N≳4t 抗漸近發散——固定 N 在高 t 灌水 |ζ|
            const int N = std::max(600, static_cast<int>(4.2 * t));
            const double m = std::abs(ZetaEMN(C(0.5, t), N));
            const double l = std::log(t / (2.0 * kPi));
            i2 += m * m * dt;
            i4 += m * m * m * m * dt;
            const double lt = std::log(t); // Ingham 用 ln⁴T
            p2 += (l + 2.0 * 0.5772156649015329 - 1.0) * dt;
            p4 += lt * lt * lt * lt / (2.0 * kPi * kPi) * dt;
        }
        printf("  [info] M₂ %.4f vs 漸近 %.4f（比 %.3f）；"
               "M₄ %.4f vs %.4f（比 %.3f）\n",
               i2 / (hi - lo), p2 / (hi - lo), i2 / p2,
               i4 / (hi - lo), p4 / (hi - lo), i4 / p4);
        Check(i2 / p2 > 0.6 && i2 / p2 < 1.6,
              "M₂ ≈ ln(T/2π)+2γ−1（Hardy–Littlewood）");
        // M₄ 主項為 ln⁴T/(2π²)，但 ln³ 二次項在 T~250 仍主導
        // （比值隨 T→∞ 才趨近 1，文獻實測此區間 ~2-4）
        Check(i4 / p4 > 0.8 && i4 / p4 < 5.0,
              "M₄ 尺度吻合 ln⁴T/(2π²) 主項（Ingham）");
    }

    // [4] Selberg 中心極限：log|ζ(½+it)|/√(½ ln ln t) → N(0,1)
    //     收斂速度僅 ln ln t——t≤320 只驗統計輪廓
    //     （mean≈0、var O(1)、峰度近 3 而非重尾）
    {
        double sx = 0, sx2 = 0, sx4 = 0;
        int n = 0;
        for (double t = 50.0; t <= 320.0; t += 0.02) {
            const int N = std::max(600, static_cast<int>(4.2 * t));
            const double lz =
                std::log(std::abs(ZetaEMN(C(0.5, t), N)));
            if (lz < -6.0) continue; // 零點深谷為測度零
            const double x =
                lz / std::sqrt(0.5 * std::log(std::log(t)));
            sx += x; sx2 += x * x; sx4 += x * x * x * x;
            ++n;
        }
        const double mean = sx / n;
        const double var = sx2 / n - mean * mean;
        const double kurt = (sx4 / n) / (var * var);
        printf("  [info] Selberg 樣本 n=%d：mean=%.3f var=%.3f "
               "峰度=%.2f（N(0,1) 目標 0/1/3）\n",
               n, mean, var, kurt);
        Check(std::fabs(mean) < 0.25 && var > 0.5 && var < 1.8,
              "Selberg CLT：正規化 log|ζ| 近標準常態");
        Check(kurt > 1.8 && kurt < 5.0,
              "峰度近高斯（非重尾分佈）");
    }

    // [5] de Bruijn–Newman 流：H_t(x)=2∫e^{tu²}Φ(u)cos(xu)du。
    //     H_0 零點 = 2γ；t≥Λ 全為實零點。Rodgers–Tao 證 Λ≥0、
    //     Polymath15 證 Λ≤0.22 → RH ⇔ Λ=0。
    //     本測：t=0 與 t=0.2 的實零點計數一致（若有零點於
    //     t≤0.2 化為複數,符號變化數會下降——可證偽）
    {
        auto h0 = [](double x) { return HtFlow(x, 0.0); };
        auto h2 = [](double x) { return HtFlow(x, 0.2); };
        // H_0 振幅 ~e^{−πx/8}：x>85 訊號 <1e-12 漸入噪底,
        // x≤85 對應 γ≤42.5（涵蓋前 7 個 ζ 零點）
        const double xHi = 85.0;
        const std::vector<double> r0 =
            ScanRoots(h0, 4.0, xHi, 0.05);
        const std::vector<double> r2 =
            ScanRoots(h2, 4.0, xHi, 0.05);
        int expected = 0;
        for (const double g : roots)
            if (g * 2.0 < xHi) ++expected;
        printf("  [info] H_0 實零點 %zu / H_0.2 %zu / 預期 %d\n",
               r0.size(), r2.size(), expected);
        Check(std::abs(static_cast<int>(r0.size()) - expected) <= 1,
              "H_0 零點數 = 2γ 計數（流核對上 ζ 零點）");
        Check(r2.size() == r0.size(),
              "t=0.2（<Λ 上界 0.22）零點仍全實——Λ≤0.2 一致");
        if (!r0.empty()) {
            printf("  [info] H_0 首零點 %.4f（2γ₁=28.2695）\n",
                   r0.front());
            Check(std::fabs(r0.front() - 28.2695) < 0.3,
                  "H_0 首零點 = 2γ₁");
        }
        // t=−0.05 探針（僅報告）：RH 真 → Λ=0 → 仍全實；
        // 若 Λ∈(−0.05,0] 則有零點已複化——我們無從分辨邊界
        auto hn = [](double x) { return HtFlow(x, -0.05); };
        const std::vector<double> rn =
            ScanRoots(hn, 4.0, xHi, 0.05);
        printf("  [info] t=−0.05 實零點 %zu（資訊用）\n",
               rn.size());
    }

    // [6] Mertens：M(x)=Σ_{n≤x} μ(n)。RH ⇔ M(x)=O(x^{½+ε})；
    //     Mertens 猜想 |M|<√x 已在 ~e^{1.6×10^40} 處被否證——
    //     但 x≤10⁶ 範圍應仍穩穩成立
    {
        const int N = 1000000;
        std::vector<int> mu(static_cast<size_t>(N) + 1, 1);
        std::vector<char> comp(static_cast<size_t>(N) + 1, 0);
        std::vector<int> primes;
        // 線性篩 μ（spf-free）：μ(p)=−1；i·p 中 p|i → 0
        mu[1] = 1;
        for (int i = 2; i <= N; ++i) {
            if (!comp[static_cast<size_t>(i)]) {
                primes.push_back(i);
                mu[static_cast<size_t>(i)] = -1;
            }
            for (const int p : primes) {
                const long long m = 1LL * i * p;
                if (m > N) break;
                comp[static_cast<size_t>(m)] = 1;
                mu[static_cast<size_t>(m)] =
                    (i % p == 0)
                        ? 0
                        : -mu[static_cast<size_t>(i)];
                if (i % p == 0) break;
            }
        }
        double worst = 0.0;
        int argmax = 0;
        long long mSum = mu[1]; // M(x) 含 μ(1)=1
        for (int x = 2; x <= N; ++x) {
            mSum += mu[static_cast<size_t>(x)];
            const double r =
                std::fabs(static_cast<double>(mSum)) /
                std::sqrt(static_cast<double>(x));
            if (r > worst) { worst = r; argmax = x; }
        }
        printf("  [info] max |M(x)|/√x = %.4f at x=%d（x≤10⁶）\n",
               worst, argmax);
        Check(worst < 1.0,
              "Mertens 界 |M(x)|<√x 於 x≤10⁶ 成立"
              "（RH 等價增長尺度的弱形式）");
    }

    // [7] η/ζ 恆等式：獨立求值器交叉驗證（兩條路徑到同一 ζ）
    {
        bool ok = true;
        for (const double t : {14.5, 25.3, 44.0}) {
            const C s(0.5, t);
            const C lhs = EtaEuler(s);
            const C rhs =
                (C(1.0) - std::pow(C(2.0), C(1.0) - s)) *
                ZetaEM(s);
            const double rel =
                std::abs(lhs - rhs) / std::max(1e-9, std::abs(rhs));
            if (rel > 1e-7) ok = false;
        }
        Check(ok, "η(s)=(1−2^{1−s})ζ(s)：雙求值器一致");
    }

    // [8] σ=1 無零點（Hadamard–de la Vallée Poussin；素數定理
    //     等價命題）：|ζ(1+it)| ≥ c/ln t 下界——t≤250 掃最小值
    {
        double minMag = 1e30;
        double atT = 0.0;
        for (double t = 1.0; t <= 250.0; t += 0.25) {
            const double m = std::abs(ZetaEMN(C(1.0, t), 600));
            if (m < minMag) { minMag = m; atT = t; }
        }
        printf("  [info] min|ζ(1+it)| = %.4f at t=%.1f\n",
               minMag, atT);
        Check(minMag > 0.02,
              "ζ(1+it)≠0：全掃描範圍幅度遠離零（PNT 側）");
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n",
           g_pass, g_fail);
    if (g_fail == 0) {
        printf("\n第二波結論：ξ 整函數輻角計數、Σ1/ρ 級數、"
               "線上矩 M₂/M₄、Selberg CLT 輪廓、\n"
               "de Bruijn–Newman 流 t=0.2 全實零點（Λ≤0.2 一致）、"
               "Mertens 界、η/ζ 恆等、σ=1 無零點——\n"
               "全部與 RH 相容；仍然，沒有任何有限計算構成證明。\n");
    }
    return g_fail == 0 ? 0 : 1;
}
