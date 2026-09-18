#pragma once

#include <string>
#include <vector>

namespace Potato {
namespace Media {

// ---- 資料集 manifest：potato.<kind>_dataset/1（AD-2）----
// 跨機器 wire format：GPU 機產資料集，consumer 只讀 manifest
// 不掃目錄慣例。新資料集一律用此統一 schema。
//
// dataset.json 形狀：
//   {
//     "schema": "potato.<kind>_dataset/1",
//     "seed": 42,
//     "episodes": ["ep_0000", "ep_0001"],
//     "extras": { ... 各資料集自訂欄位原樣保留 ... }
//   }

struct DatasetManifest {
    std::string kind;                  // e.g. "video", "synth_grid", "portrait"
    int seed = 0;
    std::vector<std::string> episodes; // 子目錄/樣本群名（可空）
    std::string extrasJson;            // 未認欄位整包保留（round-trip 不丟資料）

    // 完整 schema 字串："potato." + kind + "_dataset/1"
    std::string SchemaTag() const;
    bool IsValid() const { return !kind.empty(); }

    // 寫出 dataset.json 內容（呼叫端負責落盤）
    std::string ToJson() const;
    // 解析；schema tag 不符或壞 JSON → false，本物件不被部分覆寫
    bool FromJson(const std::string& json);

    // 讀檔便利函式：path 指向 dataset.json；失敗回 false
    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;
};

} // namespace Media
} // namespace Potato
