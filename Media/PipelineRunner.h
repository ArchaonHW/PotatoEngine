#pragma once

#include <string>
#include <vector>

namespace Potato {
namespace Media {

// ---- 管線段編排（CPU-only；AD-1：以子行程啟動 GPU 工具）----
// PipelineRunner 自己不連結 GL——它照 plan 依序以子行程
// 啟動各段工具（capture/encode/dataset/train），每段跑完
// 驗證預期工件存在。GPU 段在無獨顯機上會自行 [SKIP]，
// Runner 照樣能編排（該段產物缺席由後段跳過處理）。
//
// plan 格式：potato.pipeline_plan/1
//   {"schema":"potato.pipeline_plan/1","stages":[
//      {"name":"capture","exe":"VideoDataDemo","args":[...],
//       "outputs":["output/video_ds/dataset.json"],"optional":true},
//      {"name":"train","exe":"NeuralArtTool","args":[...],
//       "outputs":["output/model.pnn"]}]}

struct PipelineStage {
    std::string name;
    std::string exe;                 // 執行檔名（PATH 或相對路徑）
    std::vector<std::string> args;
    std::vector<std::string> outputs; // 跑完應存在的工件（存在性驗證）
    bool optional = false;            // true = 失敗不中止整條管線
};

class PipelineRunner {
public:
    // 解析 potato.pipeline_plan/1；壞 JSON/錯 schema/空 stages → false
    bool LoadPlan(const std::string& json);
    bool LoadPlanFile(const std::string& path);

    const std::vector<PipelineStage>& Stages() const { return stages_; }
    size_t StageCount() const { return stages_.size(); }

    // 依序執行：每段 system(exe args...) → 驗證 outputs 存在。
    // 必要段失敗（非零 exit 或工件缺席）→ 中止回 false；
    // optional 段失敗 → 記進 failedOptional_ 繼續。
    bool Run();

    // 最近一次的診斷（失敗段名 + 原因）
    const std::string& LastError() const { return lastError_; }
    const std::vector<std::string>& FailedOptional() const {
        return failedOptional_;
    }

private:
    std::vector<PipelineStage> stages_;
    std::string lastError_;
    std::vector<std::string> failedOptional_;
};

} // namespace Media
} // namespace Potato
