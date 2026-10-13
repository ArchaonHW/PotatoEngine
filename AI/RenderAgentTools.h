#pragma once

/**
 * RenderAgentTools — RenderingServer 的 AI Agent 工具橋接。
 *
 * 把 RenderingServer 的 RID API 註冊成 ToolFramework 工具
 * （參數一律 string map，RID 以 uint64 字串往返）——
 * LLM agent / AgentChain 節點由此取得完整的繪圖能力：
 * 2D canvas 指令、3D 場景 instance/光源/相機、
 * NeuralGraphics 生成紋理，以及 draw_frame 觸發渲染。
 *
 * 工具命名前綴 render_*，與 BuiltinTools 同風格。
 * server 為 headless（Dummy rasterizer）時全部工具仍可
 * 執行——agent 可在無 GL 環境組裝場景、驗證指令流。
 */

#include "AI/ToolFramework.h"

namespace Potato {
class RenderingServer;
namespace Rendering { class NeuralFieldImage; }

namespace AI {
namespace RenderTools {

// 把全部 render_* 工具註冊進 registry；回傳註冊數。
// server 被解構後呼叫工具會回傳錯誤（不 dangling——
// 工具只持有非擁有指標，呼叫方負責生命週期）。
size_t RegisterRenderTools(ToolRegistry& registry,
                           RenderingServer* server);

// 單獨註冊 neural field 權重（.pnn）到命名註冊表——
// render_texture_generate_neural 依名稱取用。
// 供呼叫端預先餵入訓練好的網路。
void RegisterNeuralField(const std::string& name,
                         Rendering::NeuralFieldImage* field);

} // namespace RenderTools
} // namespace AI
} // namespace Potato
