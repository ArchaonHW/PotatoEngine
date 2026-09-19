// SpriteAtlasTest — F-1 圖集索引/像素/uv/降級無頭驗證
// 涵蓋:索引解析、uv 查表（宣告 size 免像素）、缺名→default 降級、
//       default 缺席回 nullptr、壞 schema/壞 frame/尺寸錯配拒絕、
//       PNG 像素實際載入。

#include "Rendering/SpriteAtlas.h"
#include "Rendering/ImageCodec.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace Potato;

static int failures = 0;
static void Check(bool cond, const char* name) {
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        printf("  [FAIL] %s\n", name);
        ++failures;
    }
}

static bool WriteText(const char* path, const std::string& s) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << s;
    return (bool)f;
}

static bool Nearly(float a, float b) {
    const float d = a - b;
    return d < 1e-4f && d > -1e-4f;
}

int main() {
    printf("=== SpriteAtlas Tests (F-1) ===\n");

    // ---- 準備 8x4 PNG：左半紅右半藍 ----
    const char* pngPath = "sprite_atlas_test.png";
    std::vector<uint8> px(8 * 4 * 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 8; ++x) {
            uint8* p = &px[((size_t)y * 8 + x) * 4];
            p[0] = (x < 4) ? 255 : 0;
            p[1] = 0;
            p[2] = (x < 4) ? 0 : 255;
            p[3] = 255;
        }
    }
    Check(ImageCodec::WritePNGFile(pngPath, 8, 4, px.data()),
          "準備: 測試 PNG 寫出");

    // ---- [1] 索引載入（宣告 size,免像素即有 uv）----
    printf("\n[1] 索引 + uv\n");
    const char* idxPath = "sprite_atlas_test.json";
    WriteText(idxPath,
        "{\"schema\":\"potato.sprite_atlas/1\",\"image\":\"sprite_atlas_test.png\","
        "\"size\":[8,4],\"default\":\"missing\","
        "\"frames\":{\"ally\":[0,0,4,4],\"enemy\":[4,0,4,4],"
        "\"missing\":[0,0,2,2]}}");
    {
        SpriteAtlas at;
        Check(at.LoadIndex(idxPath), "LoadIndex 成功");
        Check(at.FrameCount() == 3, "三個 frame");
        Check(at.Has("ally") && !at.Has("nope"), "Has 查名");
        const SpriteFrame* f = at.Find("ally");
        Check(f && f->w == 4 && f->h == 4, "ally rect");
        Check(f && Nearly(f->u0, 0.0f) && Nearly(f->u1, 0.5f) &&
              Nearly(f->v1, 1.0f), "ally uv = 左半幅");
        const SpriteFrame* e = at.Find("enemy");
        Check(e && Nearly(e->u0, 0.5f) && Nearly(e->u1, 1.0f),
              "enemy uv = 右半幅");
        // 缺名 → default(missing frame)
        const SpriteFrame* m = at.Find("no_such_sprite");
        Check(m && m->w == 2 && m->h == 2, "缺名降級 default frame");
    }

    // ---- [2] 無 default → nullptr ----
    printf("\n[2] 降級路徑\n");
    const char* idx2 = "sprite_atlas_nodflt.json";
    WriteText(idx2,
        "{\"schema\":\"potato.sprite_atlas/1\",\"image\":\"sprite_atlas_test.png\","
        "\"frames\":{\"a\":[0,0,1,1]}}");
    {
        SpriteAtlas at;
        Check(at.LoadIndex(idx2), "無 default 索引可載");
        Check(at.Find("zzz") == nullptr, "無 default 缺名回 nullptr");
    }

    // ---- [3] 拒絕路徑 ----
    printf("\n[3] 拒絕\n");
    {
        SpriteAtlas at;
        WriteText("sprite_bad1.json",
            "{\"schema\":\"other/1\",\"image\":\"x.png\","
            "\"frames\":{\"a\":[0,0,1,1]}}");
        Check(!at.LoadIndex("sprite_bad1.json"), "錯 schema 拒絕");

        WriteText("sprite_bad2.json",
            "{\"schema\":\"potato.sprite_atlas/1\",\"image\":\"x.png\","
            "\"frames\":{\"a\":[0,0,1]}}");
        Check(!at.LoadIndex("sprite_bad2.json"), "frame 缺邊拒絕");

        WriteText("sprite_bad3.json",
            "{\"schema\":\"potato.sprite_atlas/1\",\"image\":\"x.png\","
            "\"default\":\"ghost\",\"frames\":{\"a\":[0,0,1,1]}}");
        Check(!at.LoadIndex("sprite_bad3.json"), "default 指空拒絕");

        WriteText("sprite_bad4.json", "{not json");
        Check(!at.LoadIndex("sprite_bad4.json"), "壞 JSON 拒絕");

        Check(!at.LoadIndex("no_such_index.json"), "缺檔拒絕");
        // 拒絕後不留半套索引
        Check(!at.IndexLoaded(), "拒絕後 IndexLoaded 仍 false");
    }

    // ---- [4] 像素載入 + 尺寸錯配 ----
    printf("\n[4] 像素\n");
    {
        SpriteAtlas at;
        Check(at.LoadFromFile(idxPath), "LoadFromFile 索引+像素");
        Check(at.PixelsLoaded() && at.Width() == 8 && at.Height() == 4,
              "像素尺寸 8x4");
        Check(at.Pixels()[0] == 255 && at.Pixels()[2] == 0,
              "左上像素為紅");
        Check(at.Pixels()[((size_t)4) * 4 + 2] == 255,
              "右側像素為藍");

        // 尺寸宣告錯配 → LoadPixels 拒絕
        const char* idx3 = "sprite_atlas_wrongsize.json";
        WriteText(idx3,
            "{\"schema\":\"potato.sprite_atlas/1\","
            "\"image\":\"sprite_atlas_test.png\",\"size\":[16,4],"
            "\"frames\":{\"a\":[0,0,1,1]}}");
        SpriteAtlas bad;
        Check(bad.LoadIndex(idx3), "錯配索引本身可載");
        Check(!bad.LoadPixels(), "尺寸錯配 LoadPixels 拒絕");
    }

    std::remove(pngPath);
    std::remove(idxPath);
    std::remove(idx2);
    std::remove("sprite_bad1.json");
    std::remove("sprite_bad2.json");
    std::remove("sprite_bad3.json");
    std::remove("sprite_bad4.json");
    std::remove("sprite_atlas_wrongsize.json");

    printf("\n=== 結果: %s ===\n", failures == 0 ? "全部 PASS" : "有 FAIL");
    return failures == 0 ? 0 : 1;
}
