// ZetaAgentToolsTest - zeta_* Agent 工具的 headless 驗證
//
// 確認 ToolFramework 註冊、字串參數解析、數值正確性
// （對照 RiemannExplorerTest 的已知值）與錯誤路徑。

#include "AI/ZetaAgentTools.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

using Potato::AI::ToolRegistry;
using Potato::AI::ToolResult;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

static ToolResult Call(ToolRegistry& reg, const char* name,
                       std::unordered_map<std::string,
                                          std::string>
                           params = {}) {
    return reg.ExecuteTool(name, params);
}

static bool Has(const ToolResult& r, const char* sub) {
    return r.success && r.output.find(sub) !=
                            std::string::npos;
}

int main() {
    printf("=== ZetaAgentTools 測試 ===\n\n");

    ToolRegistry reg;
    const size_t n =
        Potato::AI::ZetaTools::RegisterZetaTools(reg);
    Check(n >= 10, "註冊 >=10 支 zeta_* 工具");

    // zeta_eval：ζ(2)=π²/6≈1.644934、ζ(½)≈−1.460355
    Check(Has(Call(reg, "zeta_eval", {{"sigma", "2"}}),
              "1.644934"), "zeta_eval ζ(2)=π²/6");
    {
        ToolResult r =
            Call(reg, "zeta_eval", {{"sigma", "0.5"}});
        Check(Has(r, "-1.4603"), "zeta_eval ζ(½)≈−1.4603");
    }

    // η(1)=ln2≈0.693147
    Check(Has(Call(reg, "zeta_eta_eval", {{"sigma", "1"}}),
              "0.69314"), "zeta_eta_eval η(1)=ln2");

    // ξ(½)=½(½)(−½)π^{−1/4}Γ(¼)ζ(½)≈0.4971
    Check(Has(Call(reg, "zeta_xi_eval", {{"sigma", "0.5"}}),
              "0.497"), "zeta_xi_eval ξ(½)≈0.4971");

    // β(1)=π/4≈0.785398、L(1,χ₅)=2lnφ/√5≈0.430409
    Check(Has(Call(reg, "zeta_beta_eval", {{"sigma", "1"}}),
              "0.78539"), "zeta_beta_eval β(1)=π/4");
    Check(Has(Call(reg, "zeta_chi5_eval",
                   {{"sigma", "1"}}),
              "0.4304"), "zeta_chi5_eval L(1,χ₅)=2lnφ/√5");

    // 零點掃描 t∈[10,50]：應 10 個,首根 14.13473
    {
        ToolResult r = Call(reg, "zeta_zeros_scan",
                            {{"t_lo", "10"}, {"t_hi", "50"}});
        Check(Has(r, "count=10") && Has(r, "14.1347"),
              "zeta_zeros_scan 10 零點含 γ₁=14.1347");
    }
    // 錯誤路徑：t_hi>1000
    {
        ToolResult r = Call(reg, "zeta_zeros_scan",
                            {{"t_lo", "10"},
                             {"t_hi", "2000"}});
        Check(!r.success, "zeta_zeros_scan 拒絕 t_hi>1000");
    }

    // 輻角繞數 σ∈[0.4,0.6]×t∈[1,15] = 1（僅 14.13）
    Check(Has(Call(reg, "zeta_winding",
                   {{"s_lo", "0.4"}, {"s_hi", "0.6"},
                    {"t_lo", "1"}, {"t_hi", "15"}}),
              "winding=1"), "zeta_winding 矩形內 1 零點");

    // Li λ̃_1 截斷下界（全正 + 逼近 0.0231 之下）
    {
        ToolResult r = Call(reg, "zeta_li_lambda",
                            {{"n", "1"}, {"t_max", "130"}});
        Check(Has(r, "lambda~0.0"), "zeta_li_lambda λ̃₁>0");
    }
    Check(!Call(reg, "zeta_li_lambda", {{"n", "99"}})
               .success,
          "zeta_li_lambda 拒絕 n=99");

    // de Bruijn–Newman：H_0(28.2695)≈0（2γ₁ 零點）
    {
        ToolResult r = Call(reg, "zeta_debruijn",
                            {{"x", "28.2695"}, {"t", "0"}});
        Check(r.success, "zeta_debruijn 求值成功");
    }
    Check(Has(Call(reg, "zeta_debruijn_scan",
                   {{"x_lo", "4"}, {"x_hi", "60"},
                    {"t", "0.2"}}),
              "real_zeros="),
          "zeta_debruijn_scan t=0.2 實零點計數");
    Check(!Call(reg, "zeta_debruijn",
                {{"x", "200"}}).success,
          "zeta_debruijn 拒絕 |x|>85 噪底區");

    // Mertens：x≤10⁶ 應 bound_ok=1
    Check(Has(Call(reg, "zeta_mertens",
                   {{"x_max", "1000000"}}),
              "bound_ok=1"), "zeta_mertens |M|<√x");

    // Robin：n≤10⁶ 應 bound_ok=1
    Check(Has(Call(reg, "zeta_robin",
                   {{"n_max", "1000000"}}),
              "bound_ok=1"), "zeta_robin 比值<1");

    // L(1,χ₋₇)=π/√7≈1.18741
    Check(Has(Call(reg, "zeta_chi7_eval",
                   {{"sigma", "1"}}),
              "1.187"), "zeta_chi7_eval L(1,χ₋₇)=π/√7");

    // Laguerre–Turán：t=30 處 L/ξ² 應 ≤0
    Check(Has(Call(reg, "zeta_laguerre", {{"t", "30"}}),
              "holds=1"), "zeta_laguerre t=30 成立");

    // ψ(100)=ΣΛ(n)≈94.0453
    Check(Has(Call(reg, "zeta_psi", {{"x", "100"}}),
              "94.04"), "zeta_psi ψ(100)≈94.045");

    // 統計/泛函形式工具
    {
        // γ₁=14.1347…：Z 在零點應≈0（解析輸出值驗證）
        ToolResult r = Call(reg, "zeta_z_eval",
                            {{"t", "14.13473"}});
        bool near0 = false;
        if (r.success) {
            const auto eq = r.output.find('=');
            if (eq != std::string::npos)
                near0 = std::fabs(
                            std::atof(
                                r.output.c_str() + eq +
                                1)) < 0.01;
        }
        Check(near0, "zeta_z_eval Z(γ₁)≈0");
    }
    Check(!Call(reg, "zeta_z_eval", {{"t", "0.5"}})
               .success,
          "zeta_z_eval 拒絕 t<1（θ 漸近失效區）");
    Check(Has(Call(reg, "zeta_gue_spacing",
                   {{"t_max", "150"}}),
              "gaps="),
          "zeta_gue_spacing 間距分佈");
    {
        ToolResult r = Call(reg, "zeta_explicit_formula",
                            {{"t_max", "130"}});
        Check(Has(r, "max_err="),
              "zeta_explicit_formula 重建誤差輸出");
    }
    Check(Has(Call(reg, "zeta_dedekind_fe"), "holds=1"),
          "zeta_dedekind_fe Λ_K(s)=Λ_K(1−s)");
    Check(Has(Call(reg, "zeta_speiser",
                   {{"t_max", "30"}}),
              "speiser_ok=1"),
          "zeta_speiser ζ′ 左半帶無零點");
    {
        ToolResult r = Call(reg, "zeta_moments",
                            {{"t_hi", "100"},
                             {"step", "0.2"}});
        Check(Has(r, "M2_ratio=") && Has(r, "M4_ratio="),
              "zeta_moments 比值輸出");
    }
    {
        ToolResult r = Call(reg, "zeta_selberg",
                            {{"t_hi", "120"},
                             {"step", "0.2"}});
        Check(Has(r, "kurtosis="),
              "zeta_selberg CLT 統計輸出");
    }
    Check(Has(Call(reg, "zeta_gram", {{"t_max", "130"}}),
              "gram_ok=1"),
          "zeta_gram 區塊定律");
    Check(Has(Call(reg, "zeta_euler_product",
                   {{"sigma", "2"}, {"p_max", "2000"}}),
              "rel_err="),
          "zeta_euler_product 自洽");
    {
        ToolResult r = Call(reg, "zeta_pair_correlation",
                            {{"t_max", "150"}});
        Check(Has(r, "gue_repulsion=1"),
              "zeta_pair_correlation GUE 排斥");
    }
    Check(Has(Call(reg, "zeta_st", {{"t_max", "150"}}),
              "bounded=1"), "zeta_st 有界");
    Check(Has(Call(reg, "zeta_min_spacing",
                   {{"t_max", "150"}}),
              "min_spacing=0."),
          "zeta_min_spacing 非退化間距");
    Check(Has(Call(reg, "zeta_chebyshev_bias",
                   {{"x_max", "100000"}}),
              "bias_4_3_leads=1"),
          "zeta_chebyshev_bias π_{4,3} 領先");
    Check(Has(Call(reg, "zeta_baez_duarte",
                   {{"n_basis", "8"},
                    {"n_points", "20000"}}),
              "monotone=1"),
          "zeta_baez_duarte d_N 單調遞減");

    // 複合審計：t_max=40, x_max=1e5——逐項 pass + 結尾
    // finite_verification=1 proof=0
    {
        ToolResult r = Call(reg, "zeta_audit",
                            {{"t_max", "40"},
                             {"x_max", "100000"}});
        Check(Has(r, "zeros.count=6"),
              "zeta_audit 掃描 6 零點（t≤40）");
        Check(Has(r, "pass=0") == false &&
                  Has(r, "audit.passed="),
              "zeta_audit 逐項全過");
        Check(Has(r, "finite_verification=1 proof=0"),
              "zeta_audit 標示有限驗證非證明");
    }
    Check(!Call(reg, "zeta_audit", {{"t_max", "500"}})
               .success,
          "zeta_audit 拒絕 t_max>150");

    // 未註冊工具錯誤路徑
    Check(!Call(reg, "zeta_nonexistent").success,
          "未知工具回傳錯誤");

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n",
           g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
