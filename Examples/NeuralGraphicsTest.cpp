/**
 * NeuralGraphics headless 測試——深度學習繪圖（2D/3D）能力驗證。
 * 全部以小網路 + 程序生成訓練資料，固定 seed 可重現。
 */

#include "Rendering/NeuralGraphics.h"
#include <cmath>
#include <cstdio>
#include <random>

using namespace Potato::Rendering;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { ++g_pass; std::printf("  [PASS] %s\n", msg); } \
    else { ++g_fail; std::printf("  [FAIL] %s\n", msg); } \
} while (0)

// 程序生成參考圖：平滑漸層 + 高頻條紋 + 色塊（MLP 可學的非平凡結構）
static FImage MakeTestImage(int w, int h) {
    FImage img(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float u = (float)x / w, v = (float)y / h;
            float* p = img.At(x, y);
            p[0] = u;
            p[1] = v;
            p[2] = 0.5f + 0.5f * std::sin(u * 6.0f) * std::cos(v * 5.0f);
            if (x >= w / 4 && x < w / 2 && y >= h / 4 && y < h / 2) {
                p[0] = 0.9f; p[1] = 0.2f; p[2] = 0.1f; // 紅色色塊（銳邊——SR 學習重點）
            }
            // 對角線分界：另一條銳邊給 SR 學
            if (x + y > w + h / 3) { p[0] = 0.1f; p[1] = 0.85f; }
            p[3] = 1.0f;
        }
    }
    return img;
}

// 合成地形：兩組不同頻率/相位的波（給 latent 區分用）
static std::vector<float> MakeTerrain(int w, int h, float f1, float f2, float ph) {
    std::vector<float> out((size_t)w * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float u = (float)x / w, v = (float)y / h;
            float t = 0.5f + 0.25f * std::sin(u * f1 + ph) * std::cos(v * f2)
                            + 0.15f * std::sin((u + v) * (f1 + f2));
            out[(size_t)y * w + x] = std::min(std::max(t, 0.0f), 1.0f);
        }
    }
    return out;
}

static std::vector<float> AnalyticNormal(const std::vector<float>& hm,
                                         int w, int h, int x, int y) {
    auto H = [&](int xx, int yy) {
        xx = std::min(std::max(xx, 0), w - 1);
        yy = std::min(std::max(yy, 0), h - 1);
        return hm[(size_t)yy * w + xx];
    };
    float dx = (H(x + 1, y) - H(x - 1, y)) * 0.5f;
    float dz = (H(x, y + 1) - H(x, y - 1)) * 0.5f;
    Potato::Vector3 n(-dx, 1.0f, -dz);
    n = n.Normalized();
    return {n.x, n.y, n.z};
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // 崩潰時仍能定位輸出
    std::printf("=== NeuralGraphics Test ===\n");

    // ---- 1. 序列化保留激活類型 ----
    {
        Potato::AI::NeuralNetwork a;
        a.AddLayer(4, "relu");
        a.AddLayer(8, "tanh");
        a.AddLayer(8, "sigmoid");
        a.AddLayer(3, "linear");
        a.Build(42);
        std::vector<float> in = {0.3f, -0.7f, 0.9f, 0.1f};
        auto ref = a.Predict(in);

        Potato::AI::NeuralNetwork b;
        CHECK(b.Deserialize(a.Serialize()), "PNNv1 deserialize ok");
        auto out = b.Predict(in);
        bool same = out.size() == ref.size();
        for (size_t i = 0; same && i < ref.size(); ++i)
            same = std::fabs(out[i] - ref[i]) < 1e-5f;
        CHECK(same, "weights+activations round-trip identical output");

        Potato::AI::NeuralNetwork legacy;
        CHECK(!legacy.Deserialize("garbage"), "malformed data rejected");
    }

    FImage ref = MakeTestImage(20, 20);

    // ---- 2. 超解析度：NN > bilinear ----
    {
        NeuralUpscaler2x sr;
        float loss = sr.Train({ref}, 4000, 0.03f, 7);
        CHECK(loss >= 0.0f, "SR train returns loss");
        FImage low = Downsample2x(ref);
        FImage bilinear = UpsampleBilinear(low, 2);
        FImage neural = sr.Upscale(low);
        float pB = PSNR(ref, bilinear), pN = PSNR(ref, neural);
        std::printf("  SR: bilinear=%.2fdB neural=%.2fdB\n", pB, pN);
        CHECK(pN > pB, "neural SR beats bilinear PSNR");
        CHECK(neural.w == ref.w && neural.h == ref.h, "SR output 2x size");
    }

    // ---- 3. 降噪：輸出比輸入更接近原圖 ----
    {
        NeuralDenoiser dn;
        dn.Train({ref}, 0.15f, 80, 0.05f, 11);
        FImage noisy = AddGaussianNoise(ref, 0.15f, 99);
        FImage denoised = dn.Denoise(noisy);
        float pNoisy = PSNR(ref, noisy), pDn = PSNR(ref, denoised);
        std::printf("  Denoise: noisy=%.2fdB denoised=%.2fdB\n", pNoisy, pDn);
        CHECK(pDn > pNoisy, "denoiser improves PSNR");
    }

    // ---- 4. 上色：學到 luma→RGB 映射 ----
    {
        NeuralColorizer cz;
        cz.Train({ref}, 80, 0.05f, 13);
        // 灰階化
        FImage gray = ref;
        for (int i = 0; i < gray.w * gray.h; ++i) {
            float l = 0.299f * gray.px[i * 4] + 0.587f * gray.px[i * 4 + 1]
                    + 0.114f * gray.px[i * 4 + 2];
            gray.px[i * 4] = gray.px[i * 4 + 1] = gray.px[i * 4 + 2] = l;
        }
        FImage colored = cz.Colorize(gray);
        float pGray = PSNR(ref, gray), pCol = PSNR(ref, colored);
        std::printf("  Colorize: gray=%.2fdB colorized=%.2fdB\n", pGray, pCol);
        CHECK(pCol > pGray, "colorizer beats grayscale baseline");
    }

    // ---- 5. INR 紋理生成：latent 決定論 + 可分離 ----
    {
        NeuralFieldImage gen(4);
        std::vector<NeuralFieldImage::Sample> samples;
        samples.push_back({{1, 0, 0, 0}, MakeTestImage(16, 16)});
        FImage alt(16, 16);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                float* p = alt.At(x, y);
                float c = ((x / 4 + y / 4) % 2) ? 0.9f : 0.1f;
                p[0] = c; p[1] = c; p[2] = c; p[3] = 1.0f;
            }
        samples.push_back({{0, 1, 0, 0}, alt});
        gen.Train(samples, 200, 0.05f, 17);

        FImage g1 = gen.Generate({1, 0, 0, 0}, 32, 32);
        FImage g1b = gen.Generate({1, 0, 0, 0}, 32, 32);
        FImage g2 = gen.Generate({0, 1, 0, 0}, 32, 32);
        CHECK(PSNR(g1, g1b) > 90.0f, "INR deterministic same latent");
        CHECK(PSNR(g1, g2) < 40.0f, "different latent -> different texture");
        // 解析度自由：latent 相同、尺寸不同仍可生成
        FImage gBig = gen.Generate({1, 0, 0, 0}, 64, 64);
        CHECK(gBig.w == 64 && gBig.Valid(), "INR resolution-free sampling");
    }

    // ---- 6. 神經地形 + 網格 ----
    {
        NeuralTerrainGen tg(6);
        std::vector<NeuralTerrainGen::Sample> ts;
        ts.push_back({{1, 0, 0, 0, 0, 0}, MakeTerrain(16, 16, 6.0f, 8.0f, 0.0f), 16, 16});
        ts.push_back({{0, 1, 0, 0, 0, 0}, MakeTerrain(16, 16, 3.0f, 5.0f, 1.5f), 16, 16});
        tg.Train(ts, 300, 0.08f, 23);

        std::vector<float> hA, hB;
        CHECK(tg.GenerateHeightmap({1, 0, 0, 0, 0, 0}, 32, 32, hA), "terrain gen ok");
        CHECK(tg.GenerateHeightmap({0, 1, 0, 0, 0, 0}, 32, 32, hB), "terrain gen B ok");
        bool inRange = true, differ = false;
        for (size_t i = 0; i < hA.size(); ++i) {
            if (hA[i] < 0.0f || hA[i] > 1.0f) inRange = false;
            if (std::fabs(hA[i] - hB[i]) > 0.05f) differ = true;
        }
        CHECK(inRange, "heights in [0,1]");
        CHECK(differ, "different latent -> different terrain");

        GridMesh mesh = HeightmapToMesh(hA, 32, 32, 1.0f, 8.0f);
        CHECK(mesh.positions.size() == 32 * 32, "mesh vertex count");
        CHECK(mesh.indices.size() == 31 * 31 * 6, "mesh index count");
        bool idxOk = true, nrmOk = true;
        for (Potato::uint32 i : mesh.indices)
            if (i >= mesh.positions.size()) idxOk = false;
        for (const auto& n : mesh.normals)
            if (std::fabs(n.LengthSquared() - 1.0f) > 0.01f) nrmOk = false;
        CHECK(idxOk, "mesh indices in range");
        CHECK(nrmOk, "mesh normals unit length");
    }

    // ---- 7. 法線貼圖估計 ----
    {
        NeuralNormalMapper nm;
        std::vector<std::vector<float>> maps = {
            MakeTerrain(16, 16, 6.0f, 8.0f, 0.0f),
            MakeTerrain(16, 16, 3.0f, 5.0f, 1.5f)};
        std::vector<int> dims = {16, 16, 16, 16};
        nm.Train(maps, dims, 120, 0.05f, 29);
        FImage normalMap = nm.Apply(maps[0], 16, 16);
        CHECK(normalMap.Valid(), "normal map produced");
        // 抽樣比對解析法線（寬鬆閾值——MLP 只須逼近）
        double err = 0.0; int cnt = 0;
        for (int y = 2; y < 14; y += 4) {
            for (int x = 2; x < 14; x += 4) {
                auto gt = AnalyticNormal(maps[0], 16, 16, x, y);
                const float* p = normalMap.At(x, y);
                for (int c = 0; c < 3; ++c) {
                    float pv = p[c] * 2.0f - 1.0f;
                    err += std::fabs(pv - gt[c]);
                }
                ++cnt;
            }
        }
        err /= (cnt * 3);
        std::printf("  NormalMapper: mean |err| = %.3f\n", err);
        CHECK(err < 0.25f, "learned normals close to analytic");
    }

    // ---- 8. LOD 選擇器 ----
    {
        NeuralLODSelector lod(4);
        std::mt19937 rng(31);
        std::vector<NeuralLODSelector::Features> feats;
        std::vector<int> labels;
        std::uniform_real_distribution<float> ud(0.0f, 1.0f);
        for (int i = 0; i < 900; ++i) {
            NeuralLODSelector::Features f;
            f.distance = ud(rng) * 500.0f;
            f.screenRadius = ud(rng) * 400.0f;
            f.triCount = 100.0f + ud(rng) * 50000.0f;
            f.importance = ud(rng);
            f.fpsRatio = 0.5f + ud(rng) * 1.5f;
            feats.push_back(f);
            labels.push_back(NeuralLODSelector::RuleLabel(f, 4));
        }
        lod.Train(feats, labels, 800, 0.05f, 37);
        int agree = 0;
        for (size_t i = 0; i < feats.size(); ++i)
            if (lod.Select(feats[i]) == labels[i]) ++agree;
        float rate = (float)agree / feats.size();
        std::printf("  LODSelector: rule agreement = %.1f%%\n", rate * 100.0f);
        CHECK(rate > 0.90f, "LOD selector learned rule policy");
        NeuralLODSelector untrained(4);
        CHECK(untrained.Select(feats[0]) ==
              NeuralLODSelector::RuleLabel(feats[0], 4),
              "untrained falls back to rule");
    }

    // ---- 9. 權重檔案往返 ----
    {
        NeuralDenoiser dn;
        dn.Build(5);
        FImage noisy = AddGaussianNoise(ref, 0.1f, 3);
        FImage a = dn.Denoise(noisy);
        CHECK(SaveNetToFile(dn.Net(), "nng_test_net.pnn"), "save .pnn");
        NeuralDenoiser dn2;
        dn2.Build(6); // 不同 seed——載入後應被覆蓋
        CHECK(LoadNetFromFile(dn2.Net(), "nng_test_net.pnn"), "load .pnn");
        FImage b = dn2.Denoise(noisy);
        CHECK(PSNR(a, b) > 90.0f, "loaded weights reproduce output");
        std::remove("nng_test_net.pnn");
    }

    std::printf("\n=== %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
