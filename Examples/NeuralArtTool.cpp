/**
 * NeuralArtTool — 深度學習繪圖離線工具（訓練 + 推論）
 *
 * 用法：
 *   NeuralArtTool sr-train     <in.png> <out.pnn> [epochs]      訓練 2x 超解析
 *   NeuralArtTool sr           <in.png> <out.png> <model.pnn>   推論 2x 放大
 *   NeuralArtTool denoise-train<in.png> <out.pnn> [sigma] [epochs]
 *   NeuralArtTool denoise      <in.png> <out.png> <model.pnn>
 *   NeuralArtTool colorize-train <in.png> <out.pnn> [epochs]
 *   NeuralArtTool colorize     <in.png> <out.png> <model.pnn>
 *   NeuralArtTool texgen       <out.png> <model.pnn> <w> <h> <z0,z1,...>
 *   NeuralArtTool terrain      <out_hm.png> <model.pnn> <w> <h> <z0,...>
 *   NeuralArtTool normalmap    <hm.png> <out.png> <model.pnn>
 *
 * .pnn = NeuralNetwork::Serialize 文本（PNNv1 標頭，含激活類型）。
 * 全部 CPU/headless——可在 CI 或無顯示環境跑。
 */

#include "Rendering/NeuralGraphics.h"
#include "Rendering/ImageCodec.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using namespace Potato;
using namespace Potato::Rendering;

namespace {

bool LoadPNG(const std::string& path, FImage& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::printf("無法開啟 %s\n", path.c_str()); return false; }
    std::vector<uint8> data((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    std::vector<uint8> rgba; int w = 0, h = 0; std::string err;
    if (!ImageCodec::DecodeImage(data.data(), data.size(), rgba, w, h, &err)) {
        std::printf("解碼失敗 %s: %s\n", path.c_str(), err.c_str());
        return false;
    }
    out = FImageFromRGBA8(w, h, rgba.data());
    return true;
}

bool SavePNG(const std::string& path, const FImage& img) {
    std::vector<uint8> rgba;
    FImageToRGBA8(img, rgba);
    std::string err;
    if (!ImageCodec::WritePNGFile(path, img.w, img.h, rgba.data(), &err)) {
        std::printf("寫入失敗 %s: %s\n", path.c_str(), err.c_str());
        return false;
    }
    return true;
}

std::vector<float> ParseLatent(const char* s, int expected) {
    std::vector<float> v;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) v.push_back((float)std::atof(tok.c_str()));
    while ((int)v.size() < expected) v.push_back(0.0f);
    v.resize((size_t)expected);
    return v;
}

int ArgInt(char** argv, int i, int def) {
    return argv[i] ? std::atoi(argv[i]) : def;
}
float ArgFloat(char** argv, int i, float def) {
    return argv[i] ? (float)std::atof(argv[i]) : def;
}

void Usage() {
    std::printf(
        "NeuralArtTool — 深度學習繪圖離線工具\n"
        "  sr-train       <in.png> <out.pnn> [epochs]\n"
        "  sr             <in.png> <out.png> <model.pnn>\n"
        "  denoise-train  <in.png> <out.pnn> [sigma] [epochs]\n"
        "  denoise        <in.png> <out.png> <model.pnn>\n"
        "  colorize-train <in.png> <out.pnn> [epochs]\n"
        "  colorize       <in.png> <out.png> <model.pnn>\n"
        "  texgen         <out.png> <model.pnn> <w> <h> <z0,z1,...>\n"
        "  terrain        <out.png> <model.pnn> <w> <h> <z0,...>\n"
        "  normalmap      <hm.png> <out.png> <model.pnn>\n");
}

} // anonymous namespace

int main(int argc, char** argv) {
    if (argc < 2) { Usage(); return 1; }
    const std::string mode = argv[1];

    if (mode == "sr-train" && argc >= 4) {
        FImage img;
        if (!LoadPNG(argv[2], img)) return 1;
        NeuralUpscaler2x sr;
        float loss = sr.Train({img}, (size_t)ArgInt(argv, 4, 400), 0.05f, 7);
        std::printf("SR 訓練完成 loss=%.5f\n", loss);
        return SaveNetToFile(sr.Net(), argv[3]) ? 0 : 1;
    }
    if (mode == "sr" && argc >= 5) {
        FImage img;
        if (!LoadPNG(argv[2], img)) return 1;
        NeuralUpscaler2x sr; sr.Build();
        if (!LoadNetFromFile(sr.Net(), argv[4])) {
            std::printf("載入模型失敗 %s\n", argv[4]); return 1;
        }
        return SavePNG(argv[3], sr.Upscale(img)) ? 0 : 1;
    }
    if (mode == "denoise-train" && argc >= 4) {
        FImage img;
        if (!LoadPNG(argv[2], img)) return 1;
        NeuralDenoiser dn;
        float loss = dn.Train({img}, ArgFloat(argv, 4, 0.15f),
                              (size_t)ArgInt(argv, 5, 300), 0.05f, 11);
        std::printf("降噪訓練完成 loss=%.5f\n", loss);
        return SaveNetToFile(dn.Net(), argv[3]) ? 0 : 1;
    }
    if (mode == "denoise" && argc >= 5) {
        FImage img;
        if (!LoadPNG(argv[2], img)) return 1;
        NeuralDenoiser dn; dn.Build();
        if (!LoadNetFromFile(dn.Net(), argv[4])) return 1;
        return SavePNG(argv[3], dn.Denoise(img)) ? 0 : 1;
    }
    if (mode == "colorize-train" && argc >= 4) {
        FImage img;
        if (!LoadPNG(argv[2], img)) return 1;
        NeuralColorizer cz;
        float loss = cz.Train({img}, (size_t)ArgInt(argv, 4, 300), 0.05f, 13);
        std::printf("上色訓練完成 loss=%.5f\n", loss);
        return SaveNetToFile(cz.Net(), argv[3]) ? 0 : 1;
    }
    if (mode == "colorize" && argc >= 5) {
        FImage img;
        if (!LoadPNG(argv[2], img)) return 1;
        NeuralColorizer cz; cz.Build();
        if (!LoadNetFromFile(cz.Net(), argv[4])) return 1;
        return SavePNG(argv[3], cz.Colorize(img)) ? 0 : 1;
    }
    if (mode == "texgen" && argc >= 7) {
        NeuralFieldImage gen;
        gen.Build();
        if (!LoadNetFromFile(gen.Net(), argv[3])) return 1;
        auto latent = ParseLatent(argv[6], gen.LatentDim());
        return SavePNG(argv[2],
            gen.Generate(latent, ArgInt(argv, 4, 128), ArgInt(argv, 5, 128)))
            ? 0 : 1;
    }
    if (mode == "terrain" && argc >= 7) {
        NeuralTerrainGen tg;
        tg.Build();
        if (!LoadNetFromFile(tg.Net(), argv[3])) return 1;
        int w = ArgInt(argv, 4, 128), h = ArgInt(argv, 5, 128);
        auto latent = ParseLatent(argv[6], tg.LatentDim());
        std::vector<float> heights; FImage hm;
        if (!tg.GenerateHeightmap(latent, w, h, heights, &hm)) return 1;
        return SavePNG(argv[2], hm) ? 0 : 1;
    }
    if (mode == "normalmap" && argc >= 5) {
        FImage hmImg;
        if (!LoadPNG(argv[2], hmImg)) return 1;
        // 取 R 通道為高度
        std::vector<float> heights((size_t)hmImg.w * hmImg.h);
        for (size_t i = 0; i < heights.size(); ++i) heights[i] = hmImg.px[i * 4];
        NeuralNormalMapper nm; nm.Build();
        if (!LoadNetFromFile(nm.Net(), argv[4])) return 1;
        return SavePNG(argv[3], nm.Apply(heights, hmImg.w, hmImg.h)) ? 0 : 1;
    }

    Usage();
    return 1;
}
