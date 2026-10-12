/**
 * NeuralArtDemo — assets/neural/ 預訓 .pnn 權重的載入 + 推論示範
 *
 * 用法:
 *   NeuralArtDemo [weights_dir] [out_dir]
 *   預設 weights_dir = assets/neural(從 repo 根目錄執行)
 *   預設 out_dir     = neural_demo_out
 *
 * 對六種 NeuralGraphics 模型各跑一次推論並輸出 PNG:
 *   sr2x / denoise / colorize / texfield / terrain / normalmap
 * 全部 CPU/headless;任一模型載入或推論失敗回非零。
 * assets/neural/ 為 .gitignore 排除的本機產物——
 * 權重由 NeuralArtTool 以 MiniMax-H3 抽幀重訓產生。
 */

#include "Rendering/NeuralGraphics.h"
#include "Rendering/ImageCodec.h"
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace Potato;
using namespace Potato::Rendering;

namespace {

int g_failures = 0;

void Check(bool cond, const char* msg) {
    if (cond) std::printf("  ok: %s\n", msg);
    else { std::printf("FAIL: %s\n", msg); ++g_failures; }
}

// 32x32 程序化測試圖:對角漸層 + 色彩帶(自給自足,不需外部素材)
FImage MakeTestImage(int w, int h) {
    FImage img(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float* p = img.At(x, y);
            p[0] = (float)x / (float)(w - 1);
            p[1] = (float)y / (float)(h - 1);
            p[2] = (float)((x + y) % 8) / 7.0f;
            p[3] = 1.0f;
        }
    }
    return img;
}

bool SavePNG(const std::string& path, const FImage& img) {
    std::vector<uint8> rgba;
    FImageToRGBA8(img, rgba);
    std::string err;
    if (!ImageCodec::WritePNGFile(path, img.w, img.h, rgba.data(), &err)) {
        std::printf("FAIL: 寫入 %s: %s\n", path.c_str(), err.c_str());
        ++g_failures;
        return false;
    }
    std::printf("  -> %s (%dx%d)\n", path.c_str(), img.w, img.h);
    return true;
}

} // anonymous namespace

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "assets/neural";
    const std::string out = (argc > 2) ? argv[2] : "neural_demo_out";
    std::filesystem::create_directories(out);
    std::printf("NeuralArtDemo — 權重目錄 %s,輸出 %s\n", dir.c_str(), out.c_str());

    FImage input = MakeTestImage(32, 32);
    FImage noisy = AddGaussianNoise(input, 0.15f, 42);
    // 灰階版(取 luma)供 colorize
    FImage gray(input.w, input.h);
    for (size_t i = 0; i < input.px.size(); i += 4) {
        float l = 0.299f * input.px[i] + 0.587f * input.px[i + 1]
                + 0.114f * input.px[i + 2];
        gray.px[i] = gray.px[i + 1] = gray.px[i + 2] = l;
        gray.px[i + 3] = 1.0f;
    }

    // 1) 2x 超解析
    {
        NeuralUpscaler2x sr; sr.Build();
        Check(LoadNetFromFile(sr.Net(), dir + "/h3_sr2x.pnn"), "載入 h3_sr2x.pnn");
        if (sr.Ready()) {
            FImage up = sr.Upscale(input);
            Check(up.Valid() && up.w == input.w * 2, "SR 輸出尺寸 2x");
            SavePNG(out + "/demo_sr2x.png", up);
        }
    }

    // 2) 降噪
    {
        NeuralDenoiser dn; dn.Build();
        Check(LoadNetFromFile(dn.Net(), dir + "/h3_denoise.pnn"), "載入 h3_denoise.pnn");
        if (dn.Ready()) {
            FImage clean = dn.Denoise(noisy);
            Check(clean.Valid(), "降噪輸出有效");
            SavePNG(out + "/demo_denoise.png", clean);
        }
    }

    // 3) 灰階上色
    {
        NeuralColorizer cz; cz.Build();
        Check(LoadNetFromFile(cz.Net(), dir + "/h3_colorize.pnn"), "載入 h3_colorize.pnn");
        if (cz.Ready()) {
            FImage colored = cz.Colorize(gray);
            Check(colored.Valid(), "上色輸出有效");
            SavePNG(out + "/demo_colorize.png", colored);
        }
    }

    // 4) INR 紋理場(latent 全零 = 重現訓練圖)
    {
        NeuralFieldImage gen; gen.Build();
        Check(LoadNetFromFile(gen.Net(), dir + "/h3_texfield.pnn"), "載入 h3_texfield.pnn");
        if (gen.Ready()) {
            FImage tex = gen.Generate(std::vector<float>(gen.LatentDim(), 0.0f), 64, 64);
            Check(tex.Valid(), "紋理場輸出有效");
            SavePNG(out + "/demo_texfield.png", tex);
        }
    }

    // 4b) 其餘風格紋理場:列舉 dir 內所有 *_texfield.pnn(核心檔除外)
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        const std::string name = e.path().filename().string();
        if (name.size() < 13 ||
            name.compare(name.size() - 13, 13, "_texfield.pnn") != 0 ||
            name == "h3_texfield.pnn") {
            continue;
        }
        NeuralFieldImage gen; gen.Build();
        Check(LoadNetFromFile(gen.Net(), e.path().string()),
              ("載入 " + name).c_str());
        if (gen.Ready()) {
            FImage tex = gen.Generate(
                std::vector<float>(gen.LatentDim(), 0.0f), 64, 64);
            Check(tex.Valid(), (name + " 輸出有效").c_str());
            SavePNG(out + "/demo_" + e.path().stem().string() + ".png", tex);
        }
    }

    // 5) 神經地形(latent 全零)+ 高度場→網格
    std::vector<float> heights;
    {
        NeuralTerrainGen tg; tg.Build();
        Check(LoadNetFromFile(tg.Net(), dir + "/h3_terrain.pnn"), "載入 h3_terrain.pnn");
        if (tg.Ready()) {
            FImage hm;
            bool ok = tg.GenerateHeightmap(
                std::vector<float>(tg.LatentDim(), 0.0f), 64, 64, heights, &hm);
            Check(ok && !heights.empty(), "地形高度場產生");
            if (ok) {
                SavePNG(out + "/demo_terrain_hm.png", hm);
                GridMesh mesh = HeightmapToMesh(heights, 64, 64);
                Check(!mesh.positions.empty() && !mesh.indices.empty(),
                      "高度場→網格轉換");
            }
        }
    }

    // 6) 法線貼圖(用上一階段的高度場)
    {
        NeuralNormalMapper nm; nm.Build();
        Check(LoadNetFromFile(nm.Net(), dir + "/h3_normalmap.pnn"), "載入 h3_normalmap.pnn");
        if (nm.Ready() && !heights.empty()) {
            FImage nmap = nm.Apply(heights, 64, 64);
            Check(nmap.Valid(), "法線貼圖輸出有效");
            SavePNG(out + "/demo_normalmap.png", nmap);
        }
    }

    std::printf("%s(%d 失敗)\n",
                g_failures == 0 ? "全部通過" : "有失敗項目", g_failures);
    return g_failures == 0 ? 0 : 1;
}
