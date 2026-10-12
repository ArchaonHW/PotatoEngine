/**
 * Potato Engine Neural Graphics Module
 * 深度學習繪圖能力（2D/3D）——以自研 NeuralNetwork 實現，無第三方依賴。
 *
 * 2D：NeuralUpscaler2x（超解析）、NeuralDenoiser（降噪）、
 *     NeuralColorizer（灰階上色）、NeuralFieldImage（latent→紋理生成，INR）
 * 3D：NeuralTerrainGen（latent→高度場→網格）、NeuralNormalMapper
 *     （高度圖→法線貼圖）、NeuralLODSelector（智能 LOD 選擇）
 *
 * 設計：離線訓練（本模組的 Train* / NeuralArtTool CLI）產出 .pnn 權重，
 * runtime 只跑 Forward 推論。全部 headless 可測。
 */

#pragma once

#include "AI/NeuralNetwork.h"
#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"
#include <string>
#include <vector>

namespace Potato {
namespace Rendering {

using AI::NeuralNetwork;

/**
 * 浮點 RGBA 影像（0..1）。引擎內部運算用，與 ImageCodec 的 RGBA8 互轉。
 */
struct FImage {
    int w = 0, h = 0;
    std::vector<float> px; // RGBA，每通道 0..1

    FImage() = default;
    FImage(int width, int height) : w(width), h(height), px((size_t)width * height * 4, 0.0f) {}

    float* At(int x, int y) { return &px[((size_t)y * w + x) * 4]; }
    const float* At(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
    bool Valid() const { return w > 0 && h > 0 && px.size() == (size_t)w * h * 4; }
};

// ---- 影像工具 ----
FImage FImageFromRGBA8(int w, int h, const uint8* rgba);
void FImageToRGBA8(const FImage& img, std::vector<uint8>& outRGBA);

// box 2x 降採樣 / bilinear 任意倍率升採樣（SR 訓練的降質/基線）
FImage Downsample2x(const FImage& img);
FImage UpsampleBilinear(const FImage& img, int factor);

// 峰值信噪比（dB；越高越好，完全相同回 inf 上限 99）
float PSNR(const FImage& a, const FImage& b);

// 3x3 / 5x5 RGB patch（邊界夾取）→ 27 / 75 維；luma 版 → 9 維
std::vector<float> PatchRGB3x3(const FImage& img, int cx, int cy);
std::vector<float> PatchRGB5x5(const FImage& img, int cx, int cy);
std::vector<float> PatchLuma3x3(const FImage& img, int cx, int cy);

// 加性高斯噪聲（seed 可重現）
FImage AddGaussianNoise(const FImage& img, float sigma, unsigned int seed);

// ---- 權重持久化 ----
// .pnn：NeuralNetwork::Serialize 文本 + "PNNv1" 標頭（含激活類型）
bool SaveNetToFile(const NeuralNetwork& net, const std::string& path);
bool LoadNetFromFile(NeuralNetwork& net, const std::string& path);

// ============================================================================
// 2D：超解析度（patch MLP）
// ============================================================================
// 輸入：低解析 5x5 RGB patch（75，大感受野可判斷邊緣方向）；
// 輸出：高解析 2x2 RGB 區塊相對 bilinear 的殘差（12，linear 輸出）。
// 自我監督：訓練集 = 高解析圖 downsample2x 的 patch → (原圖區塊 − bilinear)。
class NeuralUpscaler2x {
public:
    void Build(unsigned int seed = 0);
    // images：高解析參考圖（建議 >= 16x16）。回傳最終 epoch 平均 loss。
    float Train(const std::vector<FImage>& images, size_t epochs,
                float learningRate, unsigned int seed = 0);
    // 2x 放大（邊界用夾取 patch）
    FImage Upscale(const FImage& low);
    NeuralNetwork& Net() { return net; }
    const NeuralNetwork& Net() const { return net; }
    bool Ready() const { return ready; }

private:
    NeuralNetwork net;
    bool ready = false;
};

// ============================================================================
// 2D：降噪（patch MLP）
// ============================================================================
// 輸入：帶噪 3x3 RGB patch（27）；輸出：中心像素乾淨 RGB（3）。
class NeuralDenoiser {
public:
    void Build(unsigned int seed = 0);
    // cleanImages：乾淨參考圖；sigma：訓練用噪聲強度
    float Train(const std::vector<FImage>& cleanImages, float sigma,
                size_t epochs, float learningRate, unsigned int seed = 0);
    FImage Denoise(const FImage& noisy);
    NeuralNetwork& Net() { return net; }
    const NeuralNetwork& Net() const { return net; }
    bool Ready() const { return ready; }

private:
    NeuralNetwork net;
    bool ready = false;
};

// ============================================================================
// 2D：上色 / 色彩遷移（luma patch → RGB）
// ============================================================================
// 輸入：灰階 3x3 luma patch（9）；輸出：RGB（3）。
// 訓練：彩色圖取亮度作輸入、原色彩作目標——學到「紋理→色彩」映射。
class NeuralColorizer {
public:
    void Build(unsigned int seed = 0);
    float Train(const std::vector<FImage>& colorImages, size_t epochs,
                float learningRate, unsigned int seed = 0);
    FImage Colorize(const FImage& gray); // 只取 gray 的亮度
    NeuralNetwork& Net() { return net; }
    const NeuralNetwork& Net() const { return net; }
    bool Ready() const { return ready; }

private:
    NeuralNetwork net;
    bool ready = false;
};

// ============================================================================
// 2D：程序化紋理生成（latent-conditioned INR）
// ============================================================================
// 輸入：Fourier 座標特徵（14）+ latent（latentDim）；輸出：RGB（3）。
// 訓練多張 (latent, image) 樣本後，對新 latent 取樣 = 生成新紋理，
// latent 連續變化 → 紋理連續變化（可插值）。
class NeuralFieldImage {
public:
    explicit NeuralFieldImage(int latentDim = 4);
    void Build(unsigned int seed = 0);
    struct Sample { std::vector<float> latent; FImage image; };
    float Train(const std::vector<Sample>& samples, size_t epochs,
                float learningRate, unsigned int seed = 0);
    FImage Generate(const std::vector<float>& latent, int w, int h);
    int LatentDim() const { return latentDim; }
    NeuralNetwork& Net() { return net; }
    const NeuralNetwork& Net() const { return net; }
    bool Ready() const { return ready; }

private:
    int latentDim;
    NeuralNetwork net;
    bool ready = false;
};

// ============================================================================
// 3D：神經地形生成（latent → 高度場 → 網格）
// ============================================================================
// 與 NeuralFieldImage 同構（Fourier 座標 + latent → 高度純量）。
// Generate 產出高度圖（灰階 FImage + 原始高度陣列），HeightmapToMesh
// 轉成 renderer 無關的 CPU 網格。
class NeuralTerrainGen {
public:
    explicit NeuralTerrainGen(int latentDim = 6);
    void Build(unsigned int seed = 0);
    struct Sample { std::vector<float> latent; std::vector<float> heights; int w, h; };
    float Train(const std::vector<Sample>& samples, size_t epochs,
                float learningRate, unsigned int seed = 0);
    // 生成高度場：heights[w*h]（0..1），hm 為視覺化灰階圖
    bool GenerateHeightmap(const std::vector<float>& latent, int w, int h,
                           std::vector<float>& heights, FImage* hm = nullptr);
    int LatentDim() const { return latentDim; }
    NeuralNetwork& Net() { return net; }
    const NeuralNetwork& Net() const { return net; }
    bool Ready() const { return ready; }

private:
    int latentDim;
    NeuralNetwork net;
    bool ready = false;
};

// renderer 無關的 CPU 網格（Indexed triangle list，Y-up）
struct GridMesh {
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<float> uvs; // 每頂點 2 分量
    std::vector<uint32> indices;
};

// 高度場 → 網格（中央差分法線）。cellSize：格距；heightScale：高度縮放。
GridMesh HeightmapToMesh(const std::vector<float>& heights, int w, int h,
                         float cellSize = 1.0f, float heightScale = 8.0f);

// ============================================================================
// 3D：法線貼圖估計（高度圖 → normal map）
// ============================================================================
// 輸入：高度 3x3 patch（9）；輸出：法線 xyz（linear，推論後單位化）。
// 訓練資料免費：合成地形 + 解析中央差分法線。
class NeuralNormalMapper {
public:
    void Build(unsigned int seed = 0);
    float Train(const std::vector<std::vector<float>>& heightmaps,
                const std::vector<int>& dims /* w,h per map */,
                size_t epochs, float learningRate, unsigned int seed = 0);
    // 高度場 → 法線貼圖（RGB 編碼 0..1 = xyz*0.5+0.5）
    FImage Apply(const std::vector<float>& heights, int w, int h);
    NeuralNetwork& Net() { return net; }
    const NeuralNetwork& Net() const { return net; }
    bool Ready() const { return ready; }

private:
    NeuralNetwork net;
    bool ready = false;
};

// ============================================================================
// 3D：智能 LOD 選擇器
// ============================================================================
// 輸入特徵（5）：logDistance、screenRadius、triCount(norm)、importance、fpsRatio
// 輸出：numLods 類 logits，argmax = LOD 等級（0 = 最高細節）。
// 訓練：規則標註的合成樣本——學會平滑/可調的 LOD 策略取代硬閾值。
class NeuralLODSelector {
public:
    explicit NeuralLODSelector(int numLods = 4);
    void Build(unsigned int seed = 0);
    struct Features {
        float distance;      // 世界距離
        float screenRadius;  // 投影到螢幕的半徑（像素）
        float triCount;      // 網格三角形數
        float importance;    // 0..1 語意重要性（主角/英雄單位 > 背景）
        float fpsRatio;      // currentFPS / targetFPS（<1 = 效能吃緊）
    };
    // 參考規則標註器（產生訓練標籤；也可單獨作 baseline）
    static int RuleLabel(const Features& f, int numLods);
    float Train(const std::vector<Features>& feats, const std::vector<int>& labels,
                size_t epochs, float learningRate, unsigned int seed = 0);
    int Select(const Features& f);
    int NumLods() const { return numLods; }
    NeuralNetwork& Net() { return net; }
    const NeuralNetwork& Net() const { return net; }
    bool Ready() const { return ready; }

private:
    int numLods;
    NeuralNetwork net;
    bool ready = false;
    static std::vector<float> Encode(const Features& f);
};

} // namespace Rendering
} // namespace Potato
