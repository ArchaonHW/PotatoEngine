/**
 * Potato Engine Neural Graphics Implementation
 */

#include "NeuralGraphics.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

namespace Potato {
namespace Rendering {

namespace {

constexpr float kPi = 3.14159265358979f;

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

float Luma(const float* p) {
    return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
}

// Fourier 座標特徵：[x, y] + {sin,cos}(2πf·x), {sin,cos}(2πf·y), f∈{1,2,4}
// 共 14 維——純座標 MLP 學高頻紋理必須靠它，否則只會輸出模糊均值
std::vector<float> EncodeCoord(float x, float y) {
    std::vector<float> f = {x, y};
    for (float freq : {1.0f, 2.0f, 4.0f}) {
        float ax = 2.0f * kPi * freq * x;
        float ay = 2.0f * kPi * freq * y;
        f.push_back(std::sin(ax)); f.push_back(std::cos(ax));
        f.push_back(std::sin(ay)); f.push_back(std::cos(ay));
    }
    return f;
}

// 訓練集子採樣：資料量超過 cap 時以 seed 隨機抽樣，避免 epoch 過慢
void Subsample(const std::vector<std::vector<float>>& in,
               const std::vector<std::vector<float>>& tgt,
               size_t cap, unsigned int seed,
               std::vector<std::vector<float>>& outIn,
               std::vector<std::vector<float>>& outTgt) {
    if (in.size() <= cap) {
        outIn = in; outTgt = tgt;
        return;
    }
    std::vector<size_t> idx(in.size());
    for (size_t i = 0; i < in.size(); ++i) idx[i] = i;
    std::mt19937 rng(seed);
    std::shuffle(idx.begin(), idx.end(), rng);
    idx.resize(cap);
    outIn.reserve(cap); outTgt.reserve(cap);
    for (size_t i : idx) { outIn.push_back(in[i]); outTgt.push_back(tgt[i]); }
}

} // anonymous namespace

// ============================================================================
// 影像工具
// ============================================================================

FImage FImageFromRGBA8(int w, int h, const uint8* rgba) {
    FImage img(w, h);
    const size_t n = (size_t)w * h * 4;
    for (size_t i = 0; i < n; ++i) img.px[i] = rgba[i] / 255.0f;
    return img;
}

void FImageToRGBA8(const FImage& img, std::vector<uint8>& outRGBA) {
    outRGBA.resize(img.px.size());
    for (size_t i = 0; i < img.px.size(); ++i)
        outRGBA[i] = (uint8)(Clamp01(img.px[i]) * 255.0f + 0.5f);
}

FImage Downsample2x(const FImage& img) {
    int lw = img.w / 2, lh = img.h / 2;
    if (lw <= 0 || lh <= 0) return {};
    FImage out(lw, lh);
    for (int y = 0; y < lh; ++y) {
        for (int x = 0; x < lw; ++x) {
            float* d = out.At(x, y);
            const float* p00 = img.At(x * 2, y * 2);
            const float* p10 = img.At(x * 2 + 1, y * 2);
            const float* p01 = img.At(x * 2, y * 2 + 1);
            const float* p11 = img.At(x * 2 + 1, y * 2 + 1);
            for (int c = 0; c < 4; ++c)
                d[c] = (p00[c] + p10[c] + p01[c] + p11[c]) * 0.25f;
        }
    }
    return out;
}

FImage UpsampleBilinear(const FImage& img, int factor) {
    if (!img.Valid() || factor <= 0) return {};
    FImage out(img.w * factor, img.h * factor);
    for (int y = 0; y < out.h; ++y) {
        for (int x = 0; x < out.w; ++x) {
            // 目標像素中心映射回源座標
            float sx = (x + 0.5f) / factor - 0.5f;
            float sy = (y + 0.5f) / factor - 0.5f;
            int x0 = (int)std::floor(sx), y0 = (int)std::floor(sy);
            float fx = sx - x0, fy = sy - y0;
            int x1 = std::min(x0 + 1, img.w - 1), y1 = std::min(y0 + 1, img.h - 1);
            x0 = std::max(x0, 0); y0 = std::max(y0, 0);
            const float* p00 = img.At(x0, y0); const float* p10 = img.At(x1, y0);
            const float* p01 = img.At(x0, y1); const float* p11 = img.At(x1, y1);
            float* d = out.At(x, y);
            for (int c = 0; c < 4; ++c) {
                float t = p00[c] * (1 - fx) + p10[c] * fx;
                float b = p01[c] * (1 - fx) + p11[c] * fx;
                d[c] = t * (1 - fy) + b * fy;
            }
        }
    }
    return out;
}

float PSNR(const FImage& a, const FImage& b) {
    if (!a.Valid() || !b.Valid() || a.w != b.w || a.h != b.h) return 0.0f;
    double mse = 0.0;
    const size_t n = (size_t)a.w * a.h * 3; // 只比 RGB
    for (size_t i = 0; i < (size_t)a.w * a.h; ++i) {
        for (int c = 0; c < 3; ++c) {
            double d = (double)a.px[i * 4 + c] - (double)b.px[i * 4 + c];
            mse += d * d;
        }
    }
    mse /= (double)n;
    if (mse <= 1e-12) return 99.0f;
    return (float)(10.0 * std::log10(1.0 / mse));
}

std::vector<float> PatchRGB3x3(const FImage& img, int cx, int cy) {
    std::vector<float> p;
    p.reserve(27);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int x = std::min(std::max(cx + dx, 0), img.w - 1);
            int y = std::min(std::max(cy + dy, 0), img.h - 1);
            const float* s = img.At(x, y);
            p.push_back(s[0]); p.push_back(s[1]); p.push_back(s[2]);
        }
    }
    return p;
}

std::vector<float> PatchRGB5x5(const FImage& img, int cx, int cy) {
    std::vector<float> p;
    p.reserve(75);
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            int x = std::min(std::max(cx + dx, 0), img.w - 1);
            int y = std::min(std::max(cy + dy, 0), img.h - 1);
            const float* s = img.At(x, y);
            p.push_back(s[0]); p.push_back(s[1]); p.push_back(s[2]);
        }
    }
    return p;
}

std::vector<float> PatchLuma3x3(const FImage& img, int cx, int cy) {
    std::vector<float> p;
    p.reserve(9);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int x = std::min(std::max(cx + dx, 0), img.w - 1);
            int y = std::min(std::max(cy + dy, 0), img.h - 1);
            p.push_back(Luma(img.At(x, y)));
        }
    }
    return p;
}

FImage AddGaussianNoise(const FImage& img, float sigma, unsigned int seed) {
    FImage out = img;
    std::mt19937 rng(seed);
    std::normal_distribution<float> n(0.0f, sigma);
    for (size_t i = 0; i < (size_t)out.w * out.h; ++i) {
        for (int c = 0; c < 3; ++c)
            out.px[i * 4 + c] = Clamp01(out.px[i * 4 + c] + n(rng));
    }
    return out;
}

// ============================================================================
// 權重持久化
// ============================================================================

bool SaveNetToFile(const NeuralNetwork& net, const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << net.Serialize();
    return f.good();
}

bool LoadNetFromFile(NeuralNetwork& net, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return net.Deserialize(ss.str());
}

// ============================================================================
// NeuralUpscaler2x
// ============================================================================

void NeuralUpscaler2x::Build(unsigned int seed) {
    // 75 → 64 → 64 → 12，linear 輸出殘差（AddLayer 的 activation 掛在輸入端層位）
    net.AddLayer(75, "relu");
    net.AddLayer(64, "relu");
    net.AddLayer(64, "linear");
    net.AddLayer(12, "linear");
    net.Build(seed);
    ready = true;
}

float NeuralUpscaler2x::Train(const std::vector<FImage>& images, size_t epochs,
                              float learningRate, unsigned int seed) {
    if (!ready) Build(seed);
    std::vector<std::vector<float>> in, tgt;
    for (const auto& img : images) {
        if (img.w < 8 || img.h < 8) continue;
        FImage low = Downsample2x(img);
        FImage base = UpsampleBilinear(low, 2); // 殘差學習的基線
        for (int y = 0; y < low.h; ++y) {
            for (int x = 0; x < low.w; ++x) {
                in.push_back(PatchRGB5x5(low, x, y));
                std::vector<float> t;
                t.reserve(12);
                for (int by = 0; by < 2; ++by) {
                    for (int bx = 0; bx < 2; ++bx) {
                        int hx = std::min(x * 2 + bx, img.w - 1);
                        int hy = std::min(y * 2 + by, img.h - 1);
                        const float* p = img.At(hx, hy);
                        const float* b = base.At(hx, hy);
                        t.push_back(p[0] - b[0]);
                        t.push_back(p[1] - b[1]);
                        t.push_back(p[2] - b[2]);
                    }
                }
                tgt.push_back(std::move(t));
            }
        }
    }
    if (in.empty()) return -1.0f;
    std::vector<std::vector<float>> si, st;
    Subsample(in, tgt, 4096, seed, si, st);
    net.Train(si, st, epochs, learningRate);
    return net.Evaluate(si, st);
}

FImage NeuralUpscaler2x::Upscale(const FImage& low) {
    if (!ready || !low.Valid()) return {};
    FImage hi = UpsampleBilinear(low, 2); // 基線 + alpha 通道沿用 bilinear
    for (int y = 0; y < low.h; ++y) {
        for (int x = 0; x < low.w; ++x) {
            std::vector<float> out = net.Predict(PatchRGB5x5(low, x, y));
            for (int by = 0; by < 2; ++by) {
                for (int bx = 0; bx < 2; ++bx) {
                    int hx = x * 2 + bx, hy = y * 2 + by;
                    if (hx >= hi.w || hy >= hi.h) continue;
                    float* d = hi.At(hx, hy);
                    const size_t base = (size_t)(by * 2 + bx) * 3;
                    d[0] = Clamp01(d[0] + out[base]);
                    d[1] = Clamp01(d[1] + out[base + 1]);
                    d[2] = Clamp01(d[2] + out[base + 2]);
                }
            }
        }
    }
    return hi;
}

// ============================================================================
// NeuralDenoiser
// ============================================================================

void NeuralDenoiser::Build(unsigned int seed) {
    net.AddLayer(27, "relu");
    net.AddLayer(48, "relu");
    net.AddLayer(48, "sigmoid");
    net.AddLayer(3, "linear");
    net.Build(seed);
    ready = true;
}

float NeuralDenoiser::Train(const std::vector<FImage>& cleanImages, float sigma,
                            size_t epochs, float learningRate, unsigned int seed) {
    if (!ready) Build(seed);
    std::vector<std::vector<float>> in, tgt;
    unsigned int noiseSeed = seed;
    for (const auto& img : cleanImages) {
        if (!img.Valid()) continue;
        FImage noisy = AddGaussianNoise(img, sigma, noiseSeed++);
        for (int y = 0; y < img.h; ++y) {
            for (int x = 0; x < img.w; ++x) {
                in.push_back(PatchRGB3x3(noisy, x, y));
                const float* c = img.At(x, y);
                tgt.push_back({c[0], c[1], c[2]});
            }
        }
    }
    if (in.empty()) return -1.0f;
    std::vector<std::vector<float>> si, st;
    Subsample(in, tgt, 4096, seed + 1, si, st);
    net.Train(si, st, epochs, learningRate);
    return net.Evaluate(si, st);
}

FImage NeuralDenoiser::Denoise(const FImage& noisy) {
    if (!ready || !noisy.Valid()) return {};
    FImage out = noisy;
    for (int y = 0; y < noisy.h; ++y) {
        for (int x = 0; x < noisy.w; ++x) {
            std::vector<float> r = net.Predict(PatchRGB3x3(noisy, x, y));
            float* d = out.At(x, y);
            d[0] = Clamp01(r[0]); d[1] = Clamp01(r[1]); d[2] = Clamp01(r[2]);
        }
    }
    return out;
}

// ============================================================================
// NeuralColorizer
// ============================================================================

void NeuralColorizer::Build(unsigned int seed) {
    net.AddLayer(9, "relu");
    net.AddLayer(32, "relu");
    net.AddLayer(32, "sigmoid");
    net.AddLayer(3, "linear");
    net.Build(seed);
    ready = true;
}

float NeuralColorizer::Train(const std::vector<FImage>& colorImages,
                             size_t epochs, float learningRate,
                             unsigned int seed) {
    if (!ready) Build(seed);
    std::vector<std::vector<float>> in, tgt;
    for (const auto& img : colorImages) {
        if (!img.Valid()) continue;
        for (int y = 0; y < img.h; ++y) {
            for (int x = 0; x < img.w; ++x) {
                in.push_back(PatchLuma3x3(img, x, y));
                const float* c = img.At(x, y);
                tgt.push_back({c[0], c[1], c[2]});
            }
        }
    }
    if (in.empty()) return -1.0f;
    std::vector<std::vector<float>> si, st;
    Subsample(in, tgt, 4096, seed + 2, si, st);
    net.Train(si, st, epochs, learningRate);
    return net.Evaluate(si, st);
}

FImage NeuralColorizer::Colorize(const FImage& gray) {
    if (!ready || !gray.Valid()) return {};
    FImage out = gray;
    for (int y = 0; y < gray.h; ++y) {
        for (int x = 0; x < gray.w; ++x) {
            std::vector<float> r = net.Predict(PatchLuma3x3(gray, x, y));
            float* d = out.At(x, y);
            d[0] = Clamp01(r[0]); d[1] = Clamp01(r[1]); d[2] = Clamp01(r[2]);
        }
    }
    return out;
}

// ============================================================================
// NeuralFieldImage（latent-conditioned INR）
// ============================================================================

NeuralFieldImage::NeuralFieldImage(int latent) : latentDim(latent) {}

void NeuralFieldImage::Build(unsigned int seed) {
    net.AddLayer((size_t)(14 + latentDim), "tanh");
    net.AddLayer(64, "tanh");
    net.AddLayer(64, "sigmoid");
    net.AddLayer(3, "linear");
    net.Build(seed);
    ready = true;
}

float NeuralFieldImage::Train(const std::vector<Sample>& samples, size_t epochs,
                              float learningRate, unsigned int seed) {
    if (!ready) Build(seed);
    std::vector<std::vector<float>> in, tgt;
    for (const auto& s : samples) {
        if (!s.image.Valid() || (int)s.latent.size() != latentDim) continue;
        for (int y = 0; y < s.image.h; ++y) {
            for (int x = 0; x < s.image.w; ++x) {
                std::vector<float> v = EncodeCoord(
                    (x + 0.5f) / s.image.w, (y + 0.5f) / s.image.h);
                v.insert(v.end(), s.latent.begin(), s.latent.end());
                in.push_back(std::move(v));
                const float* c = s.image.At(x, y);
                tgt.push_back({c[0], c[1], c[2]});
            }
        }
    }
    if (in.empty()) return -1.0f;
    std::vector<std::vector<float>> si, st;
    Subsample(in, tgt, 8192, seed + 3, si, st);
    net.Train(si, st, epochs, learningRate);
    return net.Evaluate(si, st);
}

FImage NeuralFieldImage::Generate(const std::vector<float>& latent,
                                  int w, int h) {
    // 以網路實際輸入維度為準——LoadNetFromFile 載入的模型 latentDim 可能不同
    const size_t expect =
        net.GetLayers().empty() ? 0 : net.GetLayers().front()->GetInputSize();
    if (!ready || w <= 0 || h <= 0 ||
        latent.size() + 14 != expect) return {};
    FImage out(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::vector<float> v = EncodeCoord((x + 0.5f) / w, (y + 0.5f) / h);
            v.insert(v.end(), latent.begin(), latent.end());
            std::vector<float> r = net.Predict(v);
            float* d = out.At(x, y);
            d[0] = Clamp01(r[0]); d[1] = Clamp01(r[1]); d[2] = Clamp01(r[2]);
            d[3] = 1.0f;
        }
    }
    return out;
}

// ============================================================================
// NeuralTerrainGen
// ============================================================================

NeuralTerrainGen::NeuralTerrainGen(int latent) : latentDim(latent) {}

void NeuralTerrainGen::Build(unsigned int seed) {
    net.AddLayer((size_t)(14 + latentDim), "tanh");
    net.AddLayer(64, "tanh");
    net.AddLayer(64, "sigmoid");
    net.AddLayer(1, "linear");
    net.Build(seed);
    ready = true;
}

float NeuralTerrainGen::Train(const std::vector<Sample>& samples,
                              size_t epochs, float learningRate,
                              unsigned int seed) {
    if (!ready) Build(seed);
    std::vector<std::vector<float>> in, tgt;
    for (const auto& s : samples) {
        if (s.w <= 0 || s.h <= 0 || s.heights.size() != (size_t)s.w * s.h ||
            (int)s.latent.size() != latentDim) continue;
        for (int y = 0; y < s.h; ++y) {
            for (int x = 0; x < s.w; ++x) {
                std::vector<float> v = EncodeCoord(
                    (x + 0.5f) / s.w, (y + 0.5f) / s.h);
                v.insert(v.end(), s.latent.begin(), s.latent.end());
                in.push_back(std::move(v));
                tgt.push_back({s.heights[(size_t)y * s.w + x]});
            }
        }
    }
    if (in.empty()) return -1.0f;
    std::vector<std::vector<float>> si, st;
    Subsample(in, tgt, 8192, seed + 4, si, st);
    net.Train(si, st, epochs, learningRate);
    return net.Evaluate(si, st);
}

bool NeuralTerrainGen::GenerateHeightmap(const std::vector<float>& latent,
                                         int w, int h,
                                         std::vector<float>& heights,
                                         FImage* hm) {
    const size_t expect =
        net.GetLayers().empty() ? 0 : net.GetLayers().front()->GetInputSize();
    if (!ready || w <= 0 || h <= 0 || latent.size() + 14 != expect)
        return false;
    heights.resize((size_t)w * h);
    if (hm) *hm = FImage(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::vector<float> v = EncodeCoord((x + 0.5f) / w, (y + 0.5f) / h);
            v.insert(v.end(), latent.begin(), latent.end());
            float hv = Clamp01(net.Predict(v)[0]);
            heights[(size_t)y * w + x] = hv;
            if (hm) {
                float* d = hm->At(x, y);
                d[0] = d[1] = d[2] = hv; d[3] = 1.0f;
            }
        }
    }
    return true;
}

// ============================================================================
// HeightmapToMesh
// ============================================================================

GridMesh HeightmapToMesh(const std::vector<float>& heights, int w, int h,
                         float cellSize, float heightScale) {
    GridMesh m;
    if (w < 2 || h < 2 || heights.size() < (size_t)w * h) return m;
    m.positions.resize((size_t)w * h);
    m.normals.resize((size_t)w * h);
    m.uvs.resize((size_t)w * h * 2);
    auto H = [&](int x, int y) {
        x = std::min(std::max(x, 0), w - 1);
        y = std::min(std::max(y, 0), h - 1);
        return heights[(size_t)y * w + x] * heightScale;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t i = (size_t)y * w + x;
            m.positions[i] = Vector3(x * cellSize, H(x, y), y * cellSize);
            // 中央差分法線：n = normalize(-dh/dx, 2·cell, -dh/dz)
            float dx = (H(x + 1, y) - H(x - 1, y)) / (2.0f * cellSize);
            float dz = (H(x, y + 1) - H(x, y - 1)) / (2.0f * cellSize);
            Vector3 n(-dx, 1.0f, -dz);
            m.normals[i] = n.Normalized();
            m.uvs[i * 2] = (float)x / (w - 1);
            m.uvs[i * 2 + 1] = (float)y / (h - 1);
        }
    }
    m.indices.reserve((size_t)(w - 1) * (h - 1) * 6);
    for (int y = 0; y + 1 < h; ++y) {
        for (int x = 0; x + 1 < w; ++x) {
            uint32 a = (uint32)(y * w + x), b = a + 1;
            uint32 c = a + (uint32)w, d = c + 1;
            m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
        }
    }
    return m;
}

// ============================================================================
// NeuralNormalMapper
// ============================================================================

void NeuralNormalMapper::Build(unsigned int seed) {
    net.AddLayer(9, "relu");
    net.AddLayer(24, "relu");
    net.AddLayer(24, "tanh");
    net.AddLayer(3, "linear");
    net.Build(seed);
    ready = true;
}

float NeuralNormalMapper::Train(
    const std::vector<std::vector<float>>& heightmaps,
    const std::vector<int>& dims, size_t epochs, float learningRate,
    unsigned int seed) {
    if (!ready) Build(seed);
    std::vector<std::vector<float>> in, tgt;
    for (size_t mi = 0; mi < heightmaps.size(); ++mi) {
        int w = dims[mi * 2], h = dims[mi * 2 + 1];
        const auto& hm = heightmaps[mi];
        if (hm.size() < (size_t)w * h || w < 3 || h < 3) continue;
        auto H = [&](int x, int y) {
            x = std::min(std::max(x, 0), w - 1);
            y = std::min(std::max(y, 0), h - 1);
            return hm[(size_t)y * w + x];
        };
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                std::vector<float> p;
                p.reserve(9);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        p.push_back(H(x + dx, y + dy));
                in.push_back(std::move(p));
                float ddx = (H(x + 1, y) - H(x - 1, y)) * 0.5f;
                float ddz = (H(x, y + 1) - H(x, y - 1)) * 0.5f;
                Vector3 n(-ddx, 1.0f, -ddz);
                n = n.Normalized();
                tgt.push_back({n.x, n.y, n.z});
            }
        }
    }
    if (in.empty()) return -1.0f;
    std::vector<std::vector<float>> si, st;
    Subsample(in, tgt, 4096, seed + 5, si, st);
    net.Train(si, st, epochs, learningRate);
    return net.Evaluate(si, st);
}

FImage NeuralNormalMapper::Apply(const std::vector<float>& heights,
                                 int w, int h) {
    if (!ready || w < 3 || h < 3 || heights.size() < (size_t)w * h) return {};
    FImage out(w, h);
    auto H = [&](int x, int y) {
        x = std::min(std::max(x, 0), w - 1);
        y = std::min(std::max(y, 0), h - 1);
        return heights[(size_t)y * w + x];
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::vector<float> p;
            p.reserve(9);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    p.push_back(H(x + dx, y + dy));
            std::vector<float> r = net.Predict(p);
            Vector3 n(r[0], r[1], r[2]);
            if (n.LengthSquared() < 1e-8f) n = Vector3(0, 1, 0);
            n = n.Normalized();
            float* d = out.At(x, y);
            d[0] = n.x * 0.5f + 0.5f;
            d[1] = n.y * 0.5f + 0.5f;
            d[2] = n.z * 0.5f + 0.5f;
            d[3] = 1.0f;
        }
    }
    return out;
}

// ============================================================================
// NeuralLODSelector
// ============================================================================

NeuralLODSelector::NeuralLODSelector(int lods) : numLods(lods) {}

void NeuralLODSelector::Build(unsigned int seed) {
    net.AddLayer(5, "relu");
    net.AddLayer(32, "relu");
    net.AddLayer(32, "sigmoid");
    net.AddLayer((size_t)numLods, "linear");
    net.Build(seed);
    ready = true;
}

std::vector<float> NeuralLODSelector::Encode(const Features& f) {
    // 對數壓縮距離/尺寸，特徵正規化到 ~0..1 讓 MLP 學得動
    return {
        std::log2(1.0f + f.distance) / 10.0f,
        std::log2(1.0f + f.screenRadius) / 10.0f,
        std::log2(1.0f + f.triCount) / 17.0f,
        f.importance,
        f.fpsRatio / 2.0f,
    };
}

int NeuralLODSelector::RuleLabel(const Features& f, int numLods) {
    // 螢幕佔比為主、fps 壓力與重要性為修正項的參考策略
    float lod = 0.0f;
    if (f.screenRadius < 256.0f) lod = 1.0f;
    if (f.screenRadius < 64.0f)  lod = 2.0f;
    if (f.screenRadius < 16.0f)  lod = 3.0f;
    // 效能吃緊時降一級；重要物件升一級
    if (f.fpsRatio < 0.9f) lod += 1.0f;
    if (f.importance > 0.7f) lod -= 1.0f;
    int r = (int)lod;
    return std::min(std::max(r, 0), numLods - 1);
}

float NeuralLODSelector::Train(const std::vector<Features>& feats,
                               const std::vector<int>& labels, size_t epochs,
                               float learningRate, unsigned int seed) {
    if (!ready) Build(seed);
    std::vector<std::vector<float>> in, tgt;
    for (size_t i = 0; i < feats.size() && i < labels.size(); ++i) {
        if (labels[i] < 0 || labels[i] >= numLods) continue;
        in.push_back(Encode(feats[i]));
        std::vector<float> t((size_t)numLods, 0.0f);
        t[labels[i]] = 1.0f;
        tgt.push_back(std::move(t));
    }
    if (in.empty()) return -1.0f;
    net.Train(in, tgt, epochs, learningRate);
    return net.Evaluate(in, tgt);
}

int NeuralLODSelector::Select(const Features& f) {
    if (!ready) return RuleLabel(f, numLods); // 未訓練退回規則
    std::vector<float> out = net.Predict(Encode(f));
    return (int)std::distance(out.begin(),
        std::max_element(out.begin(), out.end()));
}

} // namespace Rendering
} // namespace Potato
