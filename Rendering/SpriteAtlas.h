#pragma once

// SpriteAtlas — F-1 圖集管線（單一 texture bind 供單位圖示/村莊/神社共用）。
//
// 索引格式 potato.sprite_atlas/1：
//   {"schema":"potato.sprite_atlas/1",
//    "image":"units.png",          // 相對於索引檔目錄
//    "size":[W,H],                 // 可選；省略時 uv 要等 LoadPixels 才知
//    "default":"missing",          // 可選：缺名 frame 的降級目標
//    "frames":{"knight":[x,y,w,h], ...}}
//
// 設計切分：
//   - 本類只管 CPU 側（索引解析 + PNG 像素），不碰 GL——
//     無頭測試可完整驗證載入/查表/降級路徑。
//   - GL 紋理上傳由消費端做（ImGui AddImage / GL tex），
//     uv 語意為「v=0 是圖像頂端」(PNG top-down 原序，與
//     ImGui uv0/uv1 及 ImageCodec 輸出同向)。

#include "Core/CoreTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Potato {

struct SpriteFrame {
    int x = 0, y = 0, w = 0, h = 0;       // 像素 rect（原點左上）
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // 正規化 uv（圖集尺寸已知才填）
};

class SpriteAtlas {
public:
    // 解析索引 JSON（不載像素）。schema 不符/壞 JSON/壞 frame → false。
    bool LoadIndex(const std::string& jsonPath);

    // 依 image 欄位載入 PNG 像素（DecodeImage，RGBA8 top-down）。
    // 索引未載或 image 缺/解碼失敗 → false。
    // 若索引有 "size" 而實際尺寸不符 → false（索引與圖不一致）。
    bool LoadPixels();

    // LoadIndex + LoadPixels 兩步合一。
    bool LoadFromFile(const std::string& jsonPath);

    // 查 frame：找到回指標；缺名 → default frame；default 也缺 → nullptr。
    const SpriteFrame* Find(const std::string& name) const;
    bool Has(const std::string& name) const {
        return frames_.find(name) != frames_.end();
    }

    const std::vector<uint8>& Pixels() const { return pixels_; }
    int Width() const { return w_; }
    int Height() const { return h_; }
    int DeclaredWidth() const { return declW_; }
    int DeclaredHeight() const { return declH_; }
    const std::string& ImagePath() const { return imagePath_; }
    size_t FrameCount() const { return frames_.size(); }
    bool IndexLoaded() const { return indexLoaded_; }
    bool PixelsLoaded() const { return !pixels_.empty(); }

private:
    void FillUvs(); // declW/declH 或載入尺寸齊了才填 uv

    std::string imagePath_;   // 索引 image 欄位解析後的完整路徑
    std::string defaultName_;
    std::unordered_map<std::string, SpriteFrame> frames_;
    int declW_ = 0, declH_ = 0; // 索引 "size"（0 = 未宣告）
    int w_ = 0, h_ = 0;         // 實際載入尺寸
    std::vector<uint8> pixels_;
    bool indexLoaded_ = false;
};

} // namespace Potato
