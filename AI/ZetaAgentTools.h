#pragma once

/**
 * ZetaAgentTools — 黎曼 ζ/L 函數數值機器的 AI Agent 橋接。
 *
 * 把 MathUtils/RiemannZeta.h（Potato::RZ）的函數註冊成
 * ToolFramework 工具（參數一律 string map）——LLM agent /
 * AgentChain 節點由此取得解析數論計算能力：
 *   ζ/ξ/η/β/L(s,χ₅) 求值、臨界線零點掃描、輻角原理計數、
 *   Li 係數截斷、de Bruijn–Newman 熱流 H_t、
 *   Mertens/Robin 算術判據、Chebyshev ψ 篩法。
 *
 * 全部 headless 純 CPU——無 GL/網路依賴，agent 可在
 * 任何環境驗證 RH 的有限形式（數值驗證 ≠ 證明）。
 *
 * 工具命名前綴 zeta_*，與 render 系/BuiltinTools 同風格。
 */

#include "AI/ToolFramework.h"

namespace Potato {
namespace AI {
namespace ZetaTools {

// 把全部 zeta_* 工具註冊進 registry；回傳註冊數。
// 無狀態——不需要持有任何物件生命週期。
size_t RegisterZetaTools(ToolRegistry& registry);

} // namespace ZetaTools
} // namespace AI
} // namespace Potato
