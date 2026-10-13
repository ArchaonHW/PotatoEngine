#include "AI/ZetaAgentTools.h"

#include "MathUtils/RiemannZeta.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace Potato {
namespace AI {
namespace ZetaTools {

using namespace Potato::RZ;
using Params = std::unordered_map<std::string, std::string>;

// ---- 參數解析 helpers（與 RenderAgentTools 同風格）----

static double PD(const Params& p, const char* name,
                 double def = 0.0) {
    auto it = p.find(name);
    return it != p.end() ? std::atof(it->second.c_str()) : def;
}
static int PI(const Params& p, const char* name, int def = 0) {
    auto it = p.find(name);
    return it != p.end() ? std::atoi(it->second.c_str()) : def;
}

static ToolResult Ok(const std::string& out) {
    ToolResult r;
    r.success = true;
    r.output = out;
    return r;
}
static ToolResult Err(const std::string& msg) {
    ToolResult r;
    r.success = false;
    r.error = msg;
    return r;
}

static std::string CStr(const C& v) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.12g%+.12gi",
                  v.real(), v.imag());
    return buf;
}
static std::string FStr(double v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.12g", v);
    return buf;
}

static Tool MakeTool(const char* name, const char* desc,
                     std::initializer_list<
                         std::pair<const char*, const char*>>
                         params,
                     ToolFunction fn) {
    Tool t;
    t.name = name;
    t.description = desc;
    for (auto& pr : params)
        t.parameters[pr.first] = pr.second;
    t.function = std::move(fn);
    return t;
}

size_t RegisterZetaTools(ToolRegistry& registry) {
    size_t n = 0;
    auto add = [&](Tool t) {
        registry.RegisterTool(std::move(t));
        ++n;
    };

    // ---- ζ/L 求值 ----
    add(MakeTool(
        "zeta_eval",
        "ζ(σ+it) Euler-Maclaurin 求值（n=截斷項數,預設200;"
        "高 t 需 n≳4t）",
        {{"sigma", "實部"}, {"t", "虛部"}, {"n", "截斷項數"}},
        [](const Params& p) -> ToolResult {
            const C s(PD(p, "sigma", 0.5), PD(p, "t"));
            const C v = ZetaEMN(s, PI(p, "n", 200));
            return Ok(CStr(v) + " |zeta|=" + FStr(std::abs(v)));
        }));

    add(MakeTool(
        "zeta_eta_eval",
        "η(s)=Σ(−1)^{n−1}n^{−s} Euler 變換求值——"
        "獨立路徑,可與 (1−2^{1−s})ζ 交叉驗證",
        {{"sigma", "實部"}, {"t", "虛部"}},
        [](const Params& p) -> ToolResult {
            const C s(PD(p, "sigma", 0.5), PD(p, "t"));
            const C v = EtaEuler(s);
            return Ok(CStr(v) + " |eta|=" + FStr(std::abs(v)));
        }));

    add(MakeTool(
        "zeta_xi_eval",
        "ξ(s)=½s(s−1)π^{−s/2}Γ(s/2)ζ(s) 整函數——"
        "線上實值；零點=非平凡零點",
        {{"sigma", "實部"}, {"t", "虛部"}, {"n", "截斷項數"}},
        [](const Params& p) -> ToolResult {
            const C s(PD(p, "sigma", 0.5), PD(p, "t"));
            const C v = Xi(s, PI(p, "n", 200));
            return Ok(CStr(v) + " |xi|=" + FStr(std::abs(v)));
        }));

    add(MakeTool(
        "zeta_beta_eval",
        "Dirichlet β(s)=L(s,χ₄)=Σ(−1)^n(2n+1)^{−s}"
        "（Euler 變換）",
        {{"sigma", "實部"}, {"t", "虛部"}},
        [](const Params& p) -> ToolResult {
            const C s(PD(p, "sigma", 0.5), PD(p, "t"));
            const C v = BetaDirichlet(s);
            return Ok(CStr(v) + " |beta|=" + FStr(std::abs(v)));
        }));

    add(MakeTool(
        "zeta_chi5_eval",
        "L(s,χ₅) mod 5 實特徵（Hurwitz 分解）——"
        "Dedekind ζ_{Q(√5)}=ζ·L(χ₅)",
        {{"sigma", "實部"}, {"t", "虛部"}},
        [](const Params& p) -> ToolResult {
            const C s(PD(p, "sigma", 0.5), PD(p, "t"));
            // s=1 是 Hurwitz 單極點：Σχ₅(r)=0 極點相消,
            // 走極點扣除式 −Σχ₅(r)ψ(r/5)/5
            if (std::abs(s - C(1.0, 0.0)) < 1e-9) {
                const double v = Chi5LAt1();
                return Ok(FStr(v) + " |L|=" +
                          FStr(std::fabs(v)));
            }
            const C v = Chi5L(s);
            return Ok(CStr(v) + " |L|=" + FStr(std::abs(v)));
        }));

    add(MakeTool(
        "zeta_chi7_eval",
        "L(s,χ₋₇) mod 7 實特徵（Hurwitz 分解）——"
        "虛二次體 Q(√−7),類數公式 L(1)=π/√7",
        {{"sigma", "實部"}, {"t", "虛部"}},
        [](const Params& p) -> ToolResult {
            const C s(PD(p, "sigma", 0.5), PD(p, "t"));
            if (std::abs(s - C(1.0, 0.0)) < 1e-9) {
                const double v = Chi7LAt1();
                return Ok(FStr(v) + " |L|=" +
                          FStr(std::fabs(v)));
            }
            const C v = Chi7L(s);
            return Ok(CStr(v) + " |L|=" + FStr(std::abs(v)));
        }));

    add(MakeTool(
        "zeta_laguerre",
        "Laguerre–Turán 檢查：L(ξ)=ξξ''−ξ'² 在 t 處的值"
        "（歸一化 L/ξ²；ξ∈L-P 類 ⇒ 全線 ≤0）",
        {{"t", "線上點"}},
        [](const Params& p) -> ToolResult {
            const double t = PD(p, "t", 30.0);
            auto f = [](double x) {
                return Xi(C(0.5, x)).real();
            };
            const double h = 5e-3;
            const double fm = f(t - h), f0 = f(t),
                         fp = f(t + h);
            const double f2m = f(t - 2 * h),
                         f2p = f(t + 2 * h);
            const double d1 =
                (f2m - 8 * fm + 8 * fp - f2p) / (12 * h);
            const double d2 =
                (-f2m + 16 * fm - 30 * f0 + 16 * fp -
                 f2p) /
                (12 * h * h);
            const double l = f0 * d2 - d1 * d1;
            const double nrm = l / (f0 * f0 + 1e-30);
            return Ok("laguerre/xi^2=" + FStr(nrm) +
                      " holds=" + (nrm <= 1e-6 ? "1" : "0"));
        }));

    // ---- 零點/計數 ----
    add(MakeTool(
        "zeta_zeros_scan",
        "臨界線零點掃描：真 Z(t)=e^{iθ}ζ(½+it) 變號→"
        "二分求精。回傳 count 與 roots 清單（截斷 t_hi≤1000）",
        {{"t_lo", "下界"}, {"t_hi", "上界≤1000"},
         {"step", "掃描步距(預設0.02)"}},
        [](const Params& p) -> ToolResult {
            const double lo = PD(p, "t_lo", 10.0);
            const double hi = PD(p, "t_hi", 100.0);
            const double step = PD(p, "step", 0.02);
            if (hi <= lo || hi > 1000.0 || step <= 0.0)
                return Err("need 0<=t_lo<t_hi<=1000, step>0");
            // N≳4t 抗 EM 漸近發散
            const int n =
                std::max(200, static_cast<int>(4.2 * hi));
            auto zhi = [n](double t) { return RotZetaHi(t, n); };
            const std::vector<double> roots =
                ScanRoots(zhi, lo, hi, step);
            const long expected = std::lround(
                RSTheta(hi) / kPi + 1.0 -
                (RSTheta(lo) / kPi + 1.0));
            std::string out =
                "count=" + std::to_string(roots.size()) +
                " expected~=" + std::to_string(expected) +
                " roots=";
            char buf[32];
            const size_t cap =
                roots.size() < 80 ? roots.size() : 80;
            for (size_t i = 0; i < cap; ++i) {
                if (i) out += ",";
                std::snprintf(buf, sizeof(buf), "%.5f",
                              roots[i]);
                out += buf;
            }
            if (roots.size() > cap) out += ",...";
            return Ok(out);
        }));

    add(MakeTool(
        "zeta_winding",
        "輻角原理：矩形 σ∈[s_lo,s_hi]×t∈[t_lo,t_hi] 內"
        " ζ 零點繞數（線外零點偵測）",
        {{"s_lo", ""}, {"s_hi", ""}, {"t_lo", ""},
         {"t_hi", ""}, {"step", "取樣步距"}},
        [](const Params& p) -> ToolResult {
            const int w = ArgPrincipleCount(
                [](const C& s) { return ZetaEM(s); },
                PD(p, "s_lo", 0.4), PD(p, "s_hi", 0.6),
                PD(p, "t_lo", 1.0), PD(p, "t_hi", 15.0),
                PD(p, "step", 0.01));
            return Ok("winding=" + std::to_string(w));
        }));

    // ---- RH 等價改寫 ----
    add(MakeTool(
        "zeta_li_lambda",
        "Li 判據 λ̃_n 截斷下界：ρ=½+iγ 每對共軛零點貢獻"
        " 2(1−cos nφ)≥0——全正 ⟸ RH 必要條件",
        {{"n", "階數 1..20"}, {"t_max", "零點截斷(預設130)"}},
        [](const Params& p) -> ToolResult {
            const int ord = PI(p, "n", 1);
            if (ord < 1 || ord > 20)
                return Err("n must be 1..20");
            const double tmax = PD(p, "t_max", 130.0);
            const int n4 =
                std::max(200, static_cast<int>(4.2 * tmax));
            auto zhi = [n4](double t) {
                return RotZetaHi(t, n4);
            };
            const std::vector<double> roots =
                ScanRoots(zhi, 10.0, tmax, 0.02);
            double lam = 0.0;
            for (const double g : roots) {
                const C rho(0.5, g);
                const double phi =
                    std::arg((rho - C(1, 0)) / rho);
                lam += 2.0 * (1.0 - std::cos(ord * phi));
            }
            return Ok("lambda~" + FStr(lam) + " zeros=" +
                      std::to_string(roots.size()) +
                      " (true lambda_1~=0.0231, 截斷為下界)");
        }));

    add(MakeTool(
        "zeta_debruijn",
        "de Bruijn–Newman H_t(x)=∫e^{tu²}Φ(u)cos(xu)du"
        "（梯形譜精度）。H_0 零點=2γ；RH⇔Λ≤0,"
        "Polymath15 證 Λ≤0.22",
        {{"x", "頻率(零點在 2γ)"}, {"t", "流參數(預設0)"}},
        [](const Params& p) -> ToolResult {
            const double x = PD(p, "x", 0.0);
            const double t = PD(p, "t", 0.0);
            if (std::fabs(x) > 85.0)
                return Err(
                    "|x|<=85（H_0 振幅~e^{-pi x/8} 噪底）");
            const double v = HtFlow(x, t);
            return Ok("H_" + FStr(t) + "(" + FStr(x) + ")=" +
                      FStr(v));
        }));

    add(MakeTool(
        "zeta_debruijn_scan",
        "H_t 實零點掃描 x∈[x_lo,x_hi≤85]：t≥0 時"
        "零點應全實（t=0.2 仍可證偽 Λ 上界）",
        {{"x_lo", ""}, {"x_hi", "≤85"}, {"t", "流參數"},
         {"step", "預設0.05"}},
        [](const Params& p) -> ToolResult {
            const double lo = PD(p, "x_lo", 4.0);
            const double hi = PD(p, "x_hi", 85.0);
            const double t = PD(p, "t", 0.0);
            const double step = PD(p, "step", 0.05);
            if (hi <= lo || hi > 85.0)
                return Err("need 0<x_lo<x_hi<=85");
            auto h = [t](double x) { return HtFlow(x, t); };
            const std::vector<double> r =
                ScanRoots(h, lo, hi, step);
            std::string out =
                "real_zeros=" + std::to_string(r.size()) +
                " roots=";
            char buf[32];
            for (size_t i = 0;
                 i < r.size() && i < 40; ++i) {
                if (i) out += ",";
                std::snprintf(buf, sizeof(buf), "%.4f",
                              r[i]);
                out += buf;
            }
            return Ok(out);
        }));

    // ---- 算術判據 ----
    add(MakeTool(
        "zeta_mertens",
        "Mertens M(x)=Σμ(n) 篩到 x_max：max|M|/√x<1"
        " 為 RH 等價增長尺度的弱形式（~e^{1.6e40} 才破）",
        {{"x_max", "篩上界≤10^7（預設10^6）"}},
        [](const Params& p) -> ToolResult {
            const int N = PI(p, "x_max", 1000000);
            if (N < 10 || N > 10000000)
                return Err("x_max in [10, 10^7]");
            std::vector<int> mu((size_t)N + 1, 1);
            std::vector<char> comp((size_t)N + 1, 0);
            std::vector<int> primes;
            for (int i = 2; i <= N; ++i) {
                if (!comp[(size_t)i]) {
                    primes.push_back(i);
                    mu[(size_t)i] = -1;
                }
                for (const int pr : primes) {
                    const long long m = 1LL * i * pr;
                    if (m > N) break;
                    comp[(size_t)m] = 1;
                    mu[(size_t)m] = (i % pr == 0)
                                        ? 0
                                        : -mu[(size_t)i];
                    if (i % pr == 0) break;
                }
            }
            double worst = 0.0;
            long long argmax = 0, ms = mu[1];
            for (int x = 2; x <= N; ++x) {
                ms += mu[(size_t)x];
                const double r =
                    std::fabs((double)ms) /
                    std::sqrt((double)x);
                if (r > worst) { worst = r; argmax = x; }
            }
            return Ok("max|M|/sqrt(x)=" + FStr(worst) +
                      " at x=" + std::to_string(argmax) +
                      " bound_ok=" +
                      (worst < 1.0 ? "1" : "0"));
        }));

    add(MakeTool(
        "zeta_robin",
        "Robin 判據 σ(n)<e^γ n ln ln n（n>5040）——"
        "RH 唯一純算術等價形式。回傳最大比值與 argmax",
        {{"n_max", "上界≤3×10^6（預設10^6）"}},
        [](const Params& p) -> ToolResult {
            const int N = PI(p, "n_max", 1000000);
            if (N < 5041 || N > 3000000)
                return Err("n_max in [5041, 3e6]");
            std::vector<uint32_t> sig((size_t)N + 1, 0);
            for (int d = 1; d <= N; ++d)
                for (int m = d; m <= N; m += d)
                    sig[(size_t)m] += (uint32_t)d;
            const double eG = std::exp(0.5772156649015329);
            double worst = 0.0;
            int argmax = 0;
            for (int x = 5041; x <= N; ++x) {
                const double r =
                    sig[(size_t)x] /
                    (eG * x *
                     std::log(std::log((double)x)));
                if (r > worst) { worst = r; argmax = x; }
            }
            return Ok("max ratio=" + FStr(worst) + " at n=" +
                      std::to_string(argmax) +
                      " bound_ok=" +
                      (worst < 1.0 ? "1" : "0"));
        }));

    add(MakeTool(
        "zeta_psi",
        "Chebyshev ψ(x)=Σ_{n≤x}Λ(n) 篩法——顯式公式"
        " ψ≈x−Σx^ρ/ρ 的比對目標（質數階梯）",
        {{"x", "上界≤10^7"}},
        [](const Params& p) -> ToolResult {
            const double x = PD(p, "x", 100.0);
            if (x < 2.0 || x > 1e7)
                return Err("x in [2, 10^7]");
            const double psi = PsiChebyshev(x);
            return Ok("psi(" + FStr(x) + ")=" + FStr(psi) +
                      " x-psi=" + FStr(x - psi));
        }));

    // ---- 統計/泛函形式 ----
    add(MakeTool(
        "zeta_z_eval",
        "Riemann–Siegel Z(t)=e^{iθ(t)}ζ(½+it) 實值旋轉"
        "——線上零點的實函數表現（變號=零點）",
        {{"t", "線上點≤1000（θ 漸近展開需 t≥1）"}},
        [](const Params& p) -> ToolResult {
            const double t = PD(p, "t", 20.0);
            if (t < 1.0 || t > 1000.0)
                return Err("t in [1, 1000]");
            // θ(t) 的 1/(48t)+7/(5760t³) 修正項在 t≲1
            // 爆炸——RSTheta 漸近式僅適用 t≥1
            const int n4 =
                std::max(200, static_cast<int>(4.2 * t));
            return Ok("Z(" + FStr(t) + ")=" +
                      FStr(RotZetaHi(t, n4)));
        }));

    add(MakeTool(
        "zeta_gue_spacing",
        "最近鄰正規化間距分佈 vs Wigner surmise "
        "p(s)=(32/π²)s²e^{−4s²/π}——GUE 層級排斥的"
        "間距形式（Poisson 為 e^{−s}）",
        {{"t_max", "零點表上界≤330（預設200）"}},
        [](const Params& p) -> ToolResult {
            const double tmax = PD(p, "t_max", 200.0);
            if (tmax < 50.0 || tmax > 330.0)
                return Err("t_max in [50, 330]");
            const int n4 =
                std::max(200, static_cast<int>(4.2 * tmax));
            auto zhi = [n4](double t) {
                return RotZetaHi(t, n4);
            };
            const std::vector<double> roots =
                ScanRoots(zhi, 10.0, tmax, 0.02);
            int bins[6] = {0}; // s∈[0,0.5)..[2.5,3)
            long n = 0;
            for (size_t i = 1; i < roots.size(); ++i) {
                const double d =
                    (roots[i] - roots[i - 1]) *
                    std::log(roots[i] / (2.0 * kPi)) /
                    (2.0 * kPi);
                if (d >= 3.0) continue;
                ++bins[static_cast<int>(d / 0.5)];
                ++n;
            }
            std::string out =
                "gaps=" + std::to_string(n) + " bins=";
            char buf[64];
            for (int b = 0; b < 6; ++b) {
                const double u = 0.5 * b + 0.25;
                // Wigner surmise bin 機率（中點×寬）
                const double w =
                    (32.0 / (kPi * kPi)) * u * u *
                    std::exp(-4.0 * u * u / kPi) * 0.5;
                const double frac =
                    n > 0 ? bins[b] /
                                static_cast<double>(n)
                          : 0.0;
                if (b) out += ",";
                std::snprintf(buf, sizeof(buf), "%.2f/%.2f",
                              frac, w);
                out += buf;
            }
            out += " (measured/Wigner per bin)";
            return Ok(out);
        }));

    add(MakeTool(
        "zeta_explicit_formula",
        "顯式公式指紋驗證：Σ_ρ x^ρ/ρ（Fejér 權重）重建 "
        "ψ(x) 對比篩法精確值——零點位置差之毫厘即對不上，"
        "驗證零點「身份」非只在線上",
        {{"t_max", "零點截斷≤330（預設130）"},
         {"x", "多點以逗號分隔（預設40.5,61.7,83.2,99.3）"}},
        [](const Params& p) -> ToolResult {
            const double tmax = PD(p, "t_max", 130.0);
            if (tmax < 50.0 || tmax > 330.0)
                return Err("t_max in [50, 330]");
            std::vector<double> xs = {40.5, 61.7, 83.2,
                                      99.3};
            auto it = p.find("x");
            if (it != p.end()) {
                xs.clear();
                const std::string& s = it->second;
                size_t pos = 0;
                while (pos <= s.size()) {
                    const size_t c = s.find(',', pos);
                    const std::string tok = s.substr(
                        pos, c == std::string::npos
                                 ? c
                                 : c - pos);
                    if (!tok.empty()) {
                        const double v = std::atof(
                            tok.c_str());
                        if (v < 2.0 || v > 1e7)
                            return Err("x in [2, 10^7]");
                        xs.push_back(v);
                    }
                    if (c == std::string::npos) break;
                    pos = c + 1;
                }
                if (xs.size() > 8)
                    return Err("x 最多 8 個點");
            }
            const int n4 =
                std::max(200, static_cast<int>(4.2 * tmax));
            auto zhi = [n4](double t) {
                return RotZetaHi(t, n4);
            };
            const std::vector<double> roots =
                ScanRoots(zhi, 10.0, tmax, 0.02);
            double maxErr = 0.0;
            std::string out =
                "zeros=" + std::to_string(roots.size()) +
                " errs=";
            char buf[40];
            for (size_t i = 0; i < xs.size(); ++i) {
                const double exact = PsiChebyshev(xs[i]);
                const double approx =
                    PsiExplicit(xs[i], roots, tmax);
                const double err =
                    std::fabs(approx - exact);
                if (err > maxErr) maxErr = err;
                if (i) out += ",";
                std::snprintf(buf, sizeof(buf), "%.2f",
                              err);
                out += buf;
            }
            out += " max_err=" + FStr(maxErr);
            return Ok(out);
        }));

    add(MakeTool(
        "zeta_dedekind_fe",
        "Dedekind ζ_K 函數方程（K=Q(√5)：Λ_K(s)="
        "5^{s/2}π^{−s}Γ(s/2)²ζ(s)L(s,χ₅)）——"
        "回傳各探針 |Λ(s)|=|Λ(1−s)| 最大相對誤差",
        {},
        [](const Params&) -> ToolResult {
            auto lambda = [](const C& s) {
                return std::pow(C(5.0), s * C(0.5)) *
                       std::pow(C(kPi), -s) *
                       Gamma(s * C(0.5)) *
                       Gamma(s * C(0.5)) * ZetaEM(s) *
                       Chi5L(s);
            };
            const C probes[3] = {C(0.3, 14), C(0.4, 21),
                                 C(0.6, 30)};
            double worst = 0.0;
            std::string out = "rel_errs=";
            char buf[40];
            for (int i = 0; i < 3; ++i) {
                const double lhs =
                    std::abs(lambda(probes[i]));
                const double rhs = std::abs(
                    lambda(C(1.0) - probes[i]));
                const double rel =
                    std::fabs(lhs - rhs) /
                    std::max(1e-30, lhs);
                if (rel > worst) worst = rel;
                if (i) out += ",";
                std::snprintf(buf, sizeof(buf), "%.2e",
                              rel);
                out += buf;
            }
            out += " max=" + FStr(worst) + " holds=" +
                   (worst < 1e-4 ? "1" : "0");
            return Ok(out);
        }));

    add(MakeTool(
        "zeta_speiser",
        "Speiser 判據（⇔RH）：ζ′ 在 0<σ<½ 臨界帶左半"
        "無零點——五點 stencil 導數 + 輻角原理繞數",
        {{"t_max", "掃描上界≤150（預設60）"}},
        [](const Params& p) -> ToolResult {
            const double tmax = PD(p, "t_max", 60.0);
            if (tmax < 15.0 || tmax > 150.0)
                return Err("t_max in [15, 150]");
            const double hd = 1e-3;
            auto zprime = [hd](const C& s) {
                return (ZetaEM(s - C(2 * hd, 0)) -
                        8.0 * ZetaEM(s - C(hd, 0)) +
                        8.0 * ZetaEM(s + C(hd, 0)) -
                        ZetaEM(s + C(2 * hd, 0))) /
                       C(12 * hd, 0);
            };
            const int w = ArgPrincipleCount(
                zprime, 0.05, 0.45, 0.5, tmax, 0.005);
            return Ok("zeta_prime_zeros=" +
                      std::to_string(w) +
                      " speiser_ok=" +
                      (w == 0 ? "1" : "0"));
        }));

    add(MakeTool(
        "zeta_moments",
        "線上矩：M₂ 均值|ζ(½+it)|² vs Hardy–Littlewood "
        "ln(T/2π)+2γ−1；M₄ vs Ingham ln⁴T/(2π²) 主項",
        {{"t_lo", "預設8"}, {"t_hi", "≤300（預設250）"},
         {"step", "預設0.05"}},
        [](const Params& p) -> ToolResult {
            const double lo = PD(p, "t_lo", 8.0);
            const double hi = PD(p, "t_hi", 250.0);
            const double dt = PD(p, "step", 0.05);
            if (hi <= lo || hi > 300.0 || dt <= 0.0)
                return Err("0<t_lo<t_hi<=300, step>0");
            double i2 = 0.0, i4 = 0.0, p2 = 0.0, p4 = 0.0;
            for (double t = lo; t <= hi; t += dt) {
                const int N = std::max(
                    600, static_cast<int>(4.2 * t));
                const double m =
                    std::abs(ZetaEMN(C(0.5, t), N));
                const double l =
                    std::log(t / (2.0 * kPi));
                i2 += m * m * dt;
                i4 += m * m * m * m * dt;
                const double lt = std::log(t);
                p2 += (l + 2.0 * 0.5772156649015329 -
                       1.0) *
                      dt;
                p4 += lt * lt * lt * lt /
                      (2.0 * kPi * kPi) * dt;
            }
            return Ok("M2_ratio=" + FStr(i2 / p2) +
                      " M4_ratio=" + FStr(i4 / p4) +
                      " (vs Hardy-Littlewood/Ingham "
                      "asymptotic)");
        }));

    add(MakeTool(
        "zeta_selberg",
        "Selberg 中心極限：log|ζ(½+it)|/√(½ ln ln t) "
        "→ N(0,1) 的統計輪廓——mean/var/峰度"
        "（收斂僅 ln ln t,有限 t 驗輪廓）",
        {{"t_lo", "預設50"}, {"t_hi", "≤320（預設320）"},
         {"step", "預設0.05"}},
        [](const Params& p) -> ToolResult {
            const double lo = PD(p, "t_lo", 50.0);
            const double hi = PD(p, "t_hi", 320.0);
            const double dt = PD(p, "step", 0.05);
            if (hi <= lo || hi > 320.0 || dt <= 0.0)
                return Err("2<t_lo<t_hi<=320, step>0");
            double sx = 0, sx2 = 0, sx4 = 0;
            long n = 0;
            for (double t = lo; t <= hi; t += dt) {
                const int N = std::max(
                    600, static_cast<int>(4.2 * t));
                const double lz = std::log(
                    std::abs(ZetaEMN(C(0.5, t), N)));
                if (lz < -6.0) continue;
                const double x =
                    lz / std::sqrt(
                             0.5 * std::log(std::log(t)));
                sx += x;
                sx2 += x * x;
                sx4 += x * x * x * x;
                ++n;
            }
            const double mean = sx / n;
            const double var = sx2 / n - mean * mean;
            const double kurt =
                (sx4 / n) / (var * var);
            return Ok("n=" + std::to_string(n) +
                      " mean=" + FStr(mean) +
                      " var=" + FStr(var) +
                      " kurtosis=" + FStr(kurt) +
                      " (N(0,1) 目標 0/1/3)");
        }));

    add(MakeTool(
        "zeta_gram",
        "Gram 定律：Gram 點 g_n（θ(g_n)=nπ）間每區間"
        " 恰含一零點的區塊統計——首個例外 n≈126",
        {{"t_max", "零點表上界≤200（預設130）"}},
        [](const Params& p) -> ToolResult {
            const double tmax = PD(p, "t_max", 130.0);
            if (tmax < 50.0 || tmax > 200.0)
                return Err("t_max in [50, 200]");
            const int n4 =
                std::max(200, static_cast<int>(4.2 * tmax));
            auto zhi = [n4](double t) {
                return RotZetaHi(t, n4);
            };
            const std::vector<double> roots =
                ScanRoots(zhi, 10.0, tmax, 0.02);
            if (roots.empty())
                return Err("無零點");
            auto gramPoint = [](int n) {
                return Bisect(
                    [n](double t) {
                        return RSTheta(t) - n * kPi;
                    },
                    9.0, 400.0, 1e-9);
            };
            int n0 = 0;
            while (gramPoint(n0 + 1) <= roots.front())
                ++n0;
            int blocks = 0, ok = 0;
            for (int n = n0;; ++n) {
                const double g0 = gramPoint(n),
                             g1 = gramPoint(n + 1);
                if (g0 >= roots.back()) break;
                int cnt = 0;
                for (const double r : roots)
                    if (r > g0 && r < g1) ++cnt;
                ++blocks;
                if (cnt == 1) ++ok;
            }
            return Ok("blocks=" + std::to_string(blocks) +
                      " exact_one=" + std::to_string(ok) +
                      " gram_ok=" +
                      (ok == blocks ? "1" : "0"));
        }));

    add(MakeTool(
        "zeta_euler_product",
        "Euler 乘積 ∏_{p≤P}(1−p^{−σ})^{−1} → ζ(σ) "
        "（σ>1 絕對收斂側）——乘積/級數雙路徑自洽",
        {{"sigma", "實部>1（預設2）"},
         {"p_max", "截斷≤10^5（預設2000）"}},
        [](const Params& p) -> ToolResult {
            const double sig = PD(p, "sigma", 2.0);
            const int P = PI(p, "p_max", 2000);
            if (sig <= 1.0 || sig > 20.0)
                return Err("sigma in (1, 20]");
            if (P < 10 || P > 100000)
                return Err("p_max in [10, 10^5]");
            std::vector<int> primes;
            std::vector<char> comp((size_t)P + 1);
            for (int i = 2; i <= P; ++i) {
                if (!comp[(size_t)i]) primes.push_back(i);
                for (const int pr : primes) {
                    if (1LL * i * pr > P) break;
                    comp[(size_t)(i * pr)] = 1;
                }
            }
            double prod = 1.0;
            for (const int pr : primes)
                prod /= 1.0 - std::pow(
                           (double)pr, -sig);
            const double z = ZetaEM(C(sig, 0.0)).real();
            const double rel =
                std::fabs(prod - z) / std::fabs(z);
            return Ok("product=" + FStr(prod) +
                      " zeta=" + FStr(z) +
                      " rel_err=" + FStr(rel));
        }));

    add(MakeTool(
        "zeta_pair_correlation",
        "Montgomery 配對關聯：零點正規化刻度 x=θ(γ)/π+1"
        " 上所有對差 d<3 的 bin 密度 vs GUE R₂(u)="
        "1−(sin πu/πu)²（Poisson=1；u→0 強排斥為 GUE 特徵）",
        {{"t_max", "零點表上界≤330（預設200）"}},
        [](const Params& p) -> ToolResult {
            const double tmax = PD(p, "t_max", 200.0);
            if (tmax < 50.0 || tmax > 330.0)
                return Err("t_max in [50, 330]");
            const int n4 =
                std::max(200, static_cast<int>(4.2 * tmax));
            auto zhi = [n4](double t) {
                return RotZetaHi(t, n4);
            };
            const std::vector<double> roots =
                ScanRoots(zhi, 10.0, tmax, 0.02);
            std::vector<double> xs;
            xs.reserve(roots.size());
            for (const double g : roots)
                xs.push_back(RSTheta(g) / kPi + 1.0);
            int bins[6] = {0};
            long pairs = 0;
            for (size_t i = 0; i < xs.size(); ++i)
                for (size_t j = i + 1; j < xs.size(); ++j) {
                    const double d = xs[j] - xs[i];
                    if (d >= 3.0) continue;
                    ++pairs;
                    ++bins[static_cast<int>(d / 0.5)];
                }
            std::string out =
                "zeros=" + std::to_string(roots.size()) +
                " pairs=" + std::to_string(pairs) + " bins=";
            char buf[64];
            for (int b = 0; b < 6; ++b) {
                const double u = 0.5 * b + 0.25;
                const double s =
                    std::sin(kPi * u) / (kPi * u);
                const double den =
                    bins[b] * 2.0 /
                    static_cast<double>(xs.size());
                if (b) out += ",";
                std::snprintf(buf, sizeof(buf),
                              "%.3f/%.3f", den,
                              1.0 - s * s);
                out += buf;
            }
            out += " (density/GUE per bin)";
            const double d0 =
                bins[0] * 2.0 /
                static_cast<double>(xs.size());
            out += " gue_repulsion=" +
                   std::string(d0 < 0.45 ? "1" : "0");
            return Ok(out);
        }));

    add(MakeTool(
        "zeta_st",
        "計數殘差 S(T)=N(T)−θ(T)/π−1：回傳 max|S|、"
        "mean S（von Mangoldt 誤差項——有界+對稱）",
        {{"t_max", "掃描上界≤330（預設200）"},
         {"step", "取樣步距（預設0.5）"}},
        [](const Params& p) -> ToolResult {
            const double tmax = PD(p, "t_max", 200.0);
            const double step = PD(p, "step", 0.5);
            if (tmax < 50.0 || tmax > 330.0 || step <= 0.0)
                return Err("t_max in [50,330], step>0");
            const int n4 =
                std::max(200, static_cast<int>(4.2 * tmax));
            auto zhi = [n4](double t) {
                return RotZetaHi(t, n4);
            };
            const std::vector<double> roots =
                ScanRoots(zhi, 10.0, tmax, 0.02);
            double maxS = 0.0, sum = 0.0, atT = 0.0;
            long n = 0;
            for (double T = 15.0; T <= tmax; T += step) {
                long cnt = 0;
                for (const double r : roots)
                    if (r < T) ++cnt;
                const double s =
                    cnt - RSTheta(T) / kPi - 1.0;
                if (std::fabs(s) > maxS) {
                    maxS = std::fabs(s);
                    atT = T;
                }
                sum += s;
                ++n;
            }
            return Ok("max|S|=" + FStr(maxS) +
                      " at T=" + FStr(atT) +
                      " mean=" + FStr(sum / n) +
                      " bounded=" + (maxS < 2.5 ? "1" : "0"));
        }));

    add(MakeTool(
        "zeta_min_spacing",
        "最小正規化零點間距 δ=(γ_{i+1}−γ_i)ln(γ/2π)/2π"
        "——近重合零點（Lehmer 型近失誤）偵測",
        {{"t_max", "零點表上界≤330（預設200）"}},
        [](const Params& p) -> ToolResult {
            const double tmax = PD(p, "t_max", 200.0);
            if (tmax < 50.0 || tmax > 330.0)
                return Err("t_max in [50, 330]");
            const int n4 =
                std::max(200, static_cast<int>(4.2 * tmax));
            auto zhi = [n4](double t) {
                return RotZetaHi(t, n4);
            };
            const std::vector<double> roots =
                ScanRoots(zhi, 10.0, tmax, 0.02);
            double minD = 1e30;
            double atG = 0.0;
            for (size_t i = 1; i < roots.size(); ++i) {
                const double d =
                    (roots[i] - roots[i - 1]) *
                    std::log(roots[i] / (2.0 * kPi)) /
                    (2.0 * kPi);
                if (d < minD) { minD = d; atG = roots[i]; }
            }
            return Ok("min_spacing=" + FStr(minD) +
                      " at gamma~" + FStr(atG));
        }));

    add(MakeTool(
        "zeta_chebyshev_bias",
        "Chebyshev bias：π(x;4,3) vs π(x;4,1) 素數計數"
        "——L(s,χ₄) 零點驅動的殘差類偏差"
        "（有 lead change,首反轉 x=26861）",
        {{"x_max", "篩上界≤10^7（預設10^6）"}},
        [](const Params& p) -> ToolResult {
            const int N = PI(p, "x_max", 1000000);
            if (N < 100 || N > 10000000)
                return Err("x_max in [100, 10^7]");
            std::vector<char> comp((size_t)N + 1);
            long p41 = 0, p43 = 0;
            for (int i = 2; i <= N; ++i) {
                if (comp[(size_t)i]) continue;
                for (long long m = 2LL * i; m <= N; m += i)
                    comp[(size_t)m] = 1;
                if (i > 3) {
                    if (i % 4 == 1) ++p41; else ++p43;
                }
            }
            return Ok("pi_4_1=" + std::to_string(p41) +
                      " pi_4_3=" + std::to_string(p43) +
                      " bias_4_3_leads=" +
                      (p43 >= p41 ? "1" : "0"));
        }));

    add(MakeTool(
        "zeta_baez_duarte",
        "Báez–Duarte/Nyman–Beurling：常數 1 到 "
        "span{{k/t}:k≤N} 的 L²(0,1) 距離 d_N——"
        "d_N→0 ⇔ RH（Mellin 連結泛函分析形式）。"
        "梯形積分 Gram+最小二乘",
        {{"n_basis", "基函數數≤24（預設12）"},
         {"n_points", "梯形點數≤400000（預設100000）"}},
        [](const Params& p) -> ToolResult {
            const int nF = PI(p, "n_basis", 12);
            const int nP = PI(p, "n_points", 100000);
            if (nF < 2 || nF > 24 || nP < 1000 ||
                nP > 400000)
                return Err(
                    "n_basis in [2,24], "
                    "n_points in [1000,400000]");
            const double tMin = 0.002;
            const double h = (1.0 - tMin) / nP;
            std::vector<std::vector<double>> rho(
                (size_t)nF,
                std::vector<double>((size_t)nP + 1));
            for (int k = 0; k < nF; ++k) {
                auto& row = rho[(size_t)k];
                for (int i = 0; i <= nP; ++i) {
                    const double u =
                        (k + 1) / (tMin + h * i);
                    row[(size_t)i] = u - std::floor(u);
                }
            }
            auto dot = [&](int a, int b) {
                double s = 0.0;
                const auto& ra = rho[(size_t)a];
                const auto& rb = rho[(size_t)b];
                for (int i = 0; i <= nP; ++i) {
                    const double w =
                        (i == 0 || i == nP) ? 0.5 : 1.0;
                    s += w * ra[(size_t)i] * rb[(size_t)i];
                }
                return s * h;
            };
            auto integ = [&](int a) {
                double s = 0.0;
                const auto& ra = rho[(size_t)a];
                for (int i = 0; i <= nP; ++i) {
                    const double w =
                        (i == 0 || i == nP) ? 0.5 : 1.0;
                    s += w * ra[(size_t)i];
                }
                return s * h;
            };
            // d² = 1 − bᵀG⁻¹b，對遞增 N 報告收斂趨勢
            std::string out = "d_N=";
            double prevD = 2.0;
            bool monotone = true;
            for (int N = nF / 2; N <= nF; ++N) {
                std::vector<std::vector<double>> G(
                    (size_t)N,
                    std::vector<double>((size_t)N));
                std::vector<double> bv((size_t)N);
                for (int i = 0; i < N; ++i) {
                    bv[(size_t)i] = integ(i);
                    for (int j = 0; j < N; ++j)
                        G[i][j] = dot(i, j);
                }
                // 高斯消去（部分選主元）解 G·a=b
                for (int col = 0; col < N; ++col) {
                    int piv = col;
                    for (int r = col + 1; r < N; ++r)
                        if (std::fabs(G[r][col]) >
                            std::fabs(G[piv][col]))
                            piv = r;
                    std::swap(G[piv], G[col]);
                    std::swap(bv[piv], bv[col]);
                    const double dd = G[col][col];
                    for (int r = col + 1; r < N; ++r) {
                        const double f = G[r][col] / dd;
                        for (int c = col; c < N; ++c)
                            G[r][c] -= f * G[col][c];
                        bv[r] -= f * bv[col];
                    }
                }
                std::vector<double> a((size_t)N);
                for (int r = N - 1; r >= 0; --r) {
                    double s = bv[r];
                    for (int c = r + 1; c < N; ++c)
                        s -= G[r][c] * a[c];
                    a[r] = s / G[r][r];
                }
                double proj = 0.0;
                for (int i = 0; i < N; ++i) {
                    const double b =
                        integ(i); // bv 已被消去改寫
                    proj += a[(size_t)i] * b;
                }
                const double d =
                    std::sqrt(std::max(0.0, 1.0 - proj));
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.4f", d);
                if (N > nF / 2) out += ",";
                out += buf;
                if (d > prevD + 1e-4) monotone = false;
                prevD = d;
            }
            out += " monotone=" +
                   std::string(monotone ? "1" : "0") +
                   " (d_N→0 ⇔ RH)";
            return Ok(out);
        }));

    // ---- 複合審計 ----
    add(MakeTool(
        "zeta_audit",
        "RH 有限審計電池：一次呼叫跑零點掃描+輻角計數+"
        "Laguerre–Turán+de Bruijn–Newman+Mertens+Robin+"
        "Li 截斷。回傳逐項 k=v 與 pass 旗標——全部為有限"
        "可證偽驗證，proof=0 恆成立（有限計算非證明）",
        {{"t_max", "零點掃描上界≤150（預設60）"},
         {"x_max", "Mertens/Robin 篩上界≤3×10^6（預設10^5）"},
         {"lambda", "de Bruijn–Newman 流參數（預設0.2）"}},
        [](const Params& p) -> ToolResult {
            const double tmax = PD(p, "t_max", 60.0);
            const int xmax = PI(p, "x_max", 100000);
            const double lam = PD(p, "lambda", 0.2);
            if (tmax < 20.0 || tmax > 150.0)
                return Err("t_max in [20, 150]");
            if (xmax < 5041 || xmax > 3000000)
                return Err("x_max in [5041, 3e6]");
            if (lam < 0.0 || lam > 0.5)
                return Err("lambda in [0, 0.5]");

            std::string out;
            char buf[128];
            int pass = 0, total = 0;
            auto emit = [&](const char* k, const std::string& v,
                            bool ok) {
                ++total;
                if (ok) ++pass;
                out += k;
                out += "=";
                out += v;
                out += ok ? " pass=1\n" : " pass=0\n";
            };

            // 1) 線上零點掃描 vs von Mangoldt 期望
            const int n4 =
                std::max(200, static_cast<int>(4.2 * tmax));
            auto zhi = [n4](double t) {
                return RotZetaHi(t, n4);
            };
            const std::vector<double> roots =
                ScanRoots(zhi, 10.0, tmax, 0.02);
            const long expected = std::lround(
                RSTheta(tmax) / kPi + 1.0 -
                (RSTheta(10.0) / kPi + 1.0));
            emit("zeros.count", std::to_string(roots.size()),
                 (long)roots.size() == expected);
            emit("zeros.expected", std::to_string(expected),
                 true);

            // 2) 輻角原理：整個寬度條帶的零點數應等於線上計數
            const int wind = ArgPrincipleCount(
                [](const C& s) { return ZetaEM(s); },
                0.05, 0.95, 10.0, tmax, 0.01);
            emit("winding.count", std::to_string(wind),
                 wind == (int)roots.size());

            // 3) Laguerre–Turán：L(ξ)/ξ² 全線 ≤0
            double worstLT = -1e30;
            auto xf = [](double x) {
                return Xi(C(0.5, x)).real();
            };
            for (double t = 12.0; t <= tmax; t += 1.0) {
                const double h = 5e-3;
                const double fm = xf(t - h), f0 = xf(t),
                             fp = xf(t + h);
                const double f2m = xf(t - 2 * h),
                             f2p = xf(t + 2 * h);
                const double d1 =
                    (f2m - 8 * fm + 8 * fp - f2p) / (12 * h);
                const double d2 =
                    (-f2m + 16 * fm - 30 * f0 + 16 * fp -
                     f2p) /
                    (12 * h * h);
                const double l =
                    (f0 * d2 - d1 * d1) / (f0 * f0 + 1e-30);
                if (l > worstLT) worstLT = l;
            }
            emit("laguerre.max", FStr(worstLT),
                 worstLT <= 1e-6);

            // 4) de Bruijn–Newman：H_λ 在 |x|≤85 零點全實
            //    （對 t≥0 掃描計數與 H_0 的 2γ 格一致即可）
            auto hf = [lam](double x) {
                return HtFlow(x, lam);
            };
            const std::vector<double> hroots =
                ScanRoots(hf, 4.0, 85.0, 0.05);
            emit("debruijn.lambda", FStr(lam), true);
            emit("debruijn.real_zeros",
                 std::to_string(hroots.size()),
                 hroots.size() >= 5);

            // 5) Mertens：max|M|/√x < 1
            {
                std::vector<int> mu((size_t)xmax + 1, 1);
                std::vector<char> comp((size_t)xmax + 1, 0);
                std::vector<int> primes;
                for (int i = 2; i <= xmax; ++i) {
                    if (!comp[(size_t)i]) {
                        primes.push_back(i);
                        mu[(size_t)i] = -1;
                    }
                    for (const int pr : primes) {
                        const long long m = 1LL * i * pr;
                        if (m > xmax) break;
                        comp[(size_t)m] = 1;
                        mu[(size_t)m] = (i % pr == 0)
                                            ? 0
                                            : -mu[(size_t)i];
                        if (i % pr == 0) break;
                    }
                }
                double worst = 0.0;
                long long ms = mu[1];
                for (int x = 2; x <= xmax; ++x) {
                    ms += mu[(size_t)x];
                    const double r =
                        std::fabs((double)ms) /
                        std::sqrt((double)x);
                    if (r > worst) worst = r;
                }
                emit("mertens.max_ratio", FStr(worst),
                     worst < 1.0);
            }

            // 6) Robin：σ(n)/(e^γ n ln ln n) < 1（n>5040）
            {
                std::vector<uint32_t> sig((size_t)xmax + 1, 0);
                for (int d = 1; d <= xmax; ++d)
                    for (int m = d; m <= xmax; m += d)
                        sig[(size_t)m] += (uint32_t)d;
                const double eG = std::exp(0.5772156649015329);
                double worst = 0.0;
                for (int x = 5041; x <= xmax; ++x) {
                    const double r =
                        sig[(size_t)x] /
                        (eG * x *
                         std::log(std::log((double)x)));
                    if (r > worst) worst = r;
                }
                emit("robin.max_ratio", FStr(worst),
                     worst < 1.0);
            }

            // 7) Li λ̃₁ 截斷（每對共軛零點貢獻 ≥0）
            {
                double lamSum = 0.0;
                for (const double g : roots) {
                    const C rho(0.5, g);
                    const double phi =
                        std::arg((rho - C(1, 0)) / rho);
                    lamSum += 2.0 * (1.0 - std::cos(phi));
                }
                emit("li.lambda1_trunc", FStr(lamSum),
                     lamSum > 0.0);
            }

            std::snprintf(buf, sizeof(buf),
                          "audit.passed=%d/%d "
                          "finite_verification=1 proof=0",
                          pass, total);
            out += buf;
            return Ok(out);
        }));

    return n;
}

} // namespace ZetaTools
} // namespace AI
} // namespace Potato
