// SynthDataDemo - 渲染 → 訓練資料產生 → 神經網路學習 端到端驗證
//
// 驗證「引擎渲染管線能產出可用於深度學習的標註資料」：
//   1. 隱藏 GLFW 窗口 + FBO 離屏渲染（64x64 RGBA8）
//   2. 正交相機俯視 3x3 格子，種子化 RNG 把著色立方體放進某格
//      （含格內位置 / Y 軸旋轉 / 顏色 jitter）→ label = 格子編號 0-8
//   3. glReadPixels 抓幀 → output/synth_dataset/ 寫出
//      img_XXXX.png + labels.csv + dataset.json
//   4. 每幀縮樣到 16x16 灰階（256 維輸入），餵進 AI::NeuralNetwork
//      （256→48→9, sigmoid 輸出, MSE），144 train / 36 test
//   5. 測試準確率 ≥ 0.80 → [PASS]
//
// 無顯示環境：[SKIP] 並 exit 0（GLSmokeTest 慣例，CI/headless 不失敗）

#include "Rendering/OpenGLRenderer.h"
#include "Rendering/Shader.h"
#include "Rendering/ImageCodec.h"
#include "AI/NeuralNetwork.h"
#include "MathUtils/Matrix4.h"
#include "MathUtils/Vector3.h"
#include "Platform/GLFWSharedContext.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using namespace Potato;

namespace {

constexpr int kImageSize = 64;        // 渲染解析度 64x64
constexpr int kFeatSize = 16;         // 特徵圖 16x16（4x4 區塊降採樣）
constexpr int kInputDim = kFeatSize * kFeatSize;
constexpr int kGridSide = 3;          // 3x3 格子
constexpr int kNumClasses = kGridSide * kGridSide;
constexpr int kNumSamples = 180;      // 每格 20 筆
constexpr int kTrainPerClass = 16;    // 每格 16 train / 4 test
constexpr unsigned int kSeed = 20260917u;
constexpr float kCellSize = 1.0f;
constexpr float kCubeHalf = 0.30f;
constexpr const char* kOutDir = "output/synth_dataset";

const char* kVertSrc = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec3 vNormal;
void main() {
    vNormal = mat3(model) * aNormal;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)GLSL";

const char* kFragSrc = R"GLSL(
#version 330 core
in vec3 vNormal;
uniform vec3 baseColor;
out vec4 FragColor;
void main() {
    vec3 L = normalize(vec3(0.4, 0.85, 0.35));
    float d = max(dot(normalize(vNormal), L), 0.0);
    FragColor = vec4(baseColor * (0.35 + 0.65 * d), 1.0);
}
)GLSL";

// 立方體（6 面 x 2 三角 = 36 頂點，非索引），半徑 kCubeHalf
std::vector<Vertex> BuildCubeVertices() {
    const float h = kCubeHalf;
    const float p[6][4][3] = {
        // +Y 頂面
        {{-h, h,-h}, { h, h,-h}, { h, h, h}, {-h, h, h}},
        // -Y 底面
        {{-h,-h,-h}, {-h,-h, h}, { h,-h, h}, { h,-h,-h}},
        // +Z 前面
        {{-h,-h, h}, {-h, h, h}, { h, h, h}, { h,-h, h}},
        // -Z 後面
        {{ h,-h,-h}, { h, h,-h}, {-h, h,-h}, {-h,-h,-h}},
        // +X 右面
        {{ h,-h, h}, { h, h, h}, { h, h,-h}, { h,-h,-h}},
        // -X 左面
        {{-h,-h,-h}, {-h, h,-h}, {-h, h, h}, {-h,-h, h}},
    };
    const Vector3 n[6] = {
        Vector3(0, 1, 0),  Vector3(0,-1, 0), Vector3(0, 0, 1),
        Vector3(0, 0,-1),  Vector3(1, 0, 0), Vector3(-1,0, 0),
    };
    // 四邊形頂點順序為順時針（自外向內看），反轉三角形繞序使幾何法線朝外
    const int tri[2][3] = {{0,2,1},{0,3,2}};

    std::vector<Vertex> verts;
    verts.reserve(36);
    for (int f = 0; f < 6; ++f) {
        for (const auto& t : tri) {
            for (int k = 0; k < 3; ++k) {
                Vertex v{};
                v.position = Vector3(p[f][t[k]][0], p[f][t[k]][1], p[f][t[k]][2]);
                v.normal = n[f];
                verts.push_back(v);
            }
        }
    }
    return verts;
}

// 離屏 FBO（RGBA8 color + depth renderbuffer），同 PortraitRenderer
bool CreateFBO(int w, int h, GLuint& fbo, GLuint& colorTex, GLuint& depthRb) {
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, colorTex, 0);

    glGenRenderbuffers(1, &depthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, depthRb);

    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

struct Sample {
    int label = 0;
    int cellCol = 0;
    int cellRow = 0;
    float posX = 0.0f;
    float posZ = 0.0f;
    float yawDeg = 0.0f;
    float tint = 1.0f;
    std::vector<unsigned char> rgba;   // 64*64*4，已翻轉為頂端列優先
};

// RGBA → 16x16 灰階特徵（4x4 區塊平均亮度，正規化 [0,1]）
std::vector<float> FrameToFeatures(const std::vector<unsigned char>& rgba) {
    std::vector<float> feat(kInputDim);
    const int block = kImageSize / kFeatSize;
    for (int fy = 0; fy < kFeatSize; ++fy) {
        for (int fx = 0; fx < kFeatSize; ++fx) {
            float sum = 0.0f;
            for (int by = 0; by < block; ++by) {
                for (int bx = 0; bx < block; ++bx) {
                    size_t px = (static_cast<size_t>(fy * block + by) *
                                 kImageSize + fx * block + bx) * 4;
                    sum += 0.299f * rgba[px] + 0.587f * rgba[px + 1] +
                           0.114f * rgba[px + 2];
                }
            }
            feat[fy * kFeatSize + fx] = sum / (block * block * 255.0f);
        }
    }
    return feat;
}

} // namespace

int main() {
    printf("=== SynthDataDemo：渲染 -> 訓練資料 -> 神經網路 ===\n\n");

    // ---- GL context（隱藏窗口），無顯示環境 SKIP ----
    if (!glfwInit()) {
        printf("  [SKIP] glfwInit 失敗——無顯示環境\n");
        return 0;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "synthdata", nullptr, nullptr);
    if (!win) {
        printf("  [SKIP] glfwCreateWindow 失敗\n");
        glfwTerminate();
        return 0;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        printf("  [SKIP] gladLoadGLLoader 失敗\n");
        DestroyGLFWWindow(win);
        glfwTerminate();
        return 0;
    }

    int exitCode = 1;
    do {
        // ---- 輸出目錄 ----
        std::error_code ec;
        std::filesystem::create_directories(kOutDir, ec);
        if (ec) {
            printf("  [FAIL] 無法建立輸出目錄 %s: %s\n", kOutDir,
                   ec.message().c_str());
            break;
        }

        // ---- 渲染資源 ----
        Mesh cube;
        cube.SetVertices(BuildCubeVertices());

        AdvancedShader shader;
        if (!shader.LoadFromSource(kVertSrc, kFragSrc)) {
            printf("  [FAIL] shader 編譯失敗\n");
            break;
        }

        GLuint fbo = 0, colorTex = 0, depthRb = 0;
        if (!CreateFBO(kImageSize, kImageSize, fbo, colorTex, depthRb)) {
            printf("  [FAIL] FBO 建立失敗\n");
            if (fbo) glDeleteFramebuffers(1, &fbo);
            if (colorTex) glDeleteTextures(1, &colorTex);
            if (depthRb) glDeleteRenderbuffers(1, &depthRb);
            break;
        }

        // 俯視帶小傾角（看得到 3D 體感，格子仍可分離）
        Matrix4 view = Matrix4::LookAt(Vector3(0, 9.0f, 2.2f),
                                       Vector3(0, 0, 0),
                                       Vector3(0, 1, 0));
        Matrix4 proj = Matrix4::Orthographic(-1.7f, 1.7f, -1.7f, 1.7f,
                                             0.1f, 20.0f);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, kImageSize, kImageSize);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);

        // ---- 產生 180 筆標註樣本 ----
        std::mt19937 rng(kSeed);
        std::uniform_real_distribution<float> jitPos(-0.28f, 0.28f);
        std::uniform_real_distribution<float> jitYaw(0.0f, 360.0f);
        std::uniform_real_distribution<float> jitTint(0.75f, 1.0f);

        std::vector<Sample> samples(kNumSamples);
        std::ofstream csv(std::string(kOutDir) + "/labels.csv");
        if (!csv) {
            printf("  [FAIL] labels.csv 無法開啟\n");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &colorTex);
            glDeleteRenderbuffers(1, &depthRb);
            break;
        }
        csv << "filename,label,cell_col,cell_row,pos_x,pos_z,yaw_deg\n";

        bool renderFailed = false;
        const size_t rowBytes = static_cast<size_t>(kImageSize) * 4;
        for (int i = 0; i < kNumSamples; ++i) {
            Sample& s = samples[i];
            s.label = i % kNumClasses;
            s.cellCol = s.label % kGridSide;
            s.cellRow = s.label / kGridSide;
            s.posX = (s.cellCol - 1) * kCellSize + jitPos(rng);
            s.posZ = (s.cellRow - 1) * kCellSize + jitPos(rng);
            s.yawDeg = jitYaw(rng);
            s.tint = jitTint(rng);
            s.rgba.resize(static_cast<size_t>(kImageSize) * kImageSize * 4);

            // 渲染立方體
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            Matrix4 model = Matrix4::Translation(s.posX, kCubeHalf, s.posZ) *
                            Matrix4::RotationY(s.yawDeg * 3.14159265f / 180.0f);
            shader.Bind();
            shader.SetMat4("model", model);
            shader.SetMat4("view", view);
            shader.SetMat4("projection", proj);
            float warm = s.tint;
            shader.SetVec3("baseColor", Vector3(warm, warm * 0.92f,
                                                warm * 0.80f));
            cube.Draw();

            // 讀回 + 翻轉列序
            std::vector<unsigned char> raw(s.rgba.size());
            glFinish();
            glReadPixels(0, 0, kImageSize, kImageSize, GL_RGBA,
                         GL_UNSIGNED_BYTE, raw.data());
            for (int y = 0; y < kImageSize; ++y) {
                std::memcpy(s.rgba.data() + static_cast<size_t>(y) * rowBytes,
                            raw.data() + static_cast<size_t>(kImageSize - 1 - y)
                                           * rowBytes,
                            rowBytes);
            }

            // 寫 PNG + CSV
            char name[32];
            std::snprintf(name, sizeof(name), "img_%04d.png", i);
            std::string full = std::string(kOutDir) + "/" + name;
            std::string err;
            if (!ImageCodec::WritePNGFile(full, kImageSize, kImageSize,
                                          s.rgba.data(), &err)) {
                printf("  [FAIL] PNG 寫出失敗 %s: %s\n", full.c_str(),
                       err.c_str());
                renderFailed = true;
                break;
            }
            csv << name << ',' << s.label << ',' << s.cellCol << ','
                << s.cellRow << ',' << s.posX << ',' << s.posZ << ','
                << s.yawDeg << '\n';
        }
        csv.close();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &colorTex);
        glDeleteRenderbuffers(1, &depthRb);
        if (renderFailed) break;

        // ---- dataset.json ----
        {
            std::ofstream js(std::string(kOutDir) + "/dataset.json");
            if (!js) {
                printf("  [FAIL] dataset.json 無法開啟\n");
                break;
            }
            js << "{\n"
               << "  \"name\": \"synth_grid_cube\",\n"
               << "  \"task\": \"classification\",\n"
               << "  \"seed\": " << kSeed << ",\n"
               << "  \"image_width\": " << kImageSize << ",\n"
               << "  \"image_height\": " << kImageSize << ",\n"
               << "  \"samples\": " << kNumSamples << ",\n"
               << "  \"num_classes\": " << kNumClasses << ",\n"
               << "  \"class_names\": [\"cell_0\",\"cell_1\",\"cell_2\","
                  "\"cell_3\",\"cell_4\",\"cell_5\",\"cell_6\",\"cell_7\","
                  "\"cell_8\"],\n"
               << "  \"label_format\": \"cell_index = row * 3 + col\",\n"
               << "  \"feature_extract\": \"16x16 grayscale block-average\",\n"
               << "  \"labels_file\": \"labels.csv\"\n"
               << "}\n";
        }
        printf("  [OK] 資料集寫出：%d 筆 -> %s/\n", kNumSamples, kOutDir);

        // ---- 特徵提取 + 分層 train/test 分割 ----
        std::vector<std::vector<float>> feats(kNumSamples);
        for (int i = 0; i < kNumSamples; ++i)
            feats[i] = FrameToFeatures(samples[i].rgba);

        std::vector<std::vector<float>> trainX, trainY, testX, testY;
        std::mt19937 splitRng(kSeed ^ 0x5bd1e995u);
        for (int c = 0; c < kNumClasses; ++c) {
            std::vector<int> idx;
            for (int i = 0; i < kNumSamples; ++i)
                if (samples[i].label == c) idx.push_back(i);
            std::shuffle(idx.begin(), idx.end(), splitRng);
            for (size_t k = 0; k < idx.size(); ++k) {
                std::vector<float> onehot(kNumClasses, 0.0f);
                onehot[c] = 1.0f;
                if (static_cast<int>(k) < kTrainPerClass) {
                    trainX.push_back(feats[idx[k]]);
                    trainY.push_back(onehot);
                } else {
                    testX.push_back(feats[idx[k]]);
                    testY.push_back(onehot);
                }
            }
        }
        printf("  [OK] 分割：train %zu / test %zu（每類 %d/%d）\n",
               trainX.size(), testX.size(), kTrainPerClass,
               kNumSamples / kNumClasses - kTrainPerClass);

        // ---- 訓練 ----
        AI::NeuralNetwork net;
        net.AddLayer(kInputDim, "relu");   // 層0：256->48, relu
        net.AddLayer(48, "sigmoid");       // 層1：48->9, sigmoid
        net.AddLayer(kNumClasses);
        net.Build();
        net.SetLossFunction("mse");

        // NeuralLayer 內部以 random_device 播種（無法注入 seed）——
        // 用本地 mt19937 產生 Xavier 初始化並覆寫，讓整個流程可重現
        {
            std::mt19937 initRng(kSeed ^ 0x853c49e6u);
            for (const auto& layer : net.GetLayers()) {
                const size_t in = layer->GetInputSize();
                const size_t out = layer->GetOutputSize();
                const float bound = std::sqrt(6.0f / (in + out));
                std::uniform_real_distribution<float> wdist(-bound, bound);
                std::vector<std::vector<float>> w(
                    out, std::vector<float>(in));
                for (auto& row : w)
                    for (auto& v : row) v = wdist(initRng);
                layer->SetWeights(w);
                layer->SetBiases(std::vector<float>(out, 0.0f));
            }
        }

        const int epochs = 200;
        const float lr = 0.15f;
        std::mt19937 trainRng(kSeed ^ 0x9e3779b9u);
        for (int ep = 0; ep < epochs; ++ep) {
            std::vector<size_t> order(trainX.size());
            std::iota(order.begin(), order.end(), 0);
            std::shuffle(order.begin(), order.end(), trainRng);
            float loss = 0.0f;
            for (size_t idx : order)
                loss += net.TrainStep(trainX[idx], trainY[idx], lr);
            if (ep % 20 == 0 || ep == epochs - 1)
                printf("  epoch %3d  loss %.4f\n", ep,
                       loss / trainX.size());
        }

        // ---- 評估 ----
        int correct = 0;
        for (size_t i = 0; i < testX.size(); ++i) {
            int pred = net.PredictClass(testX[i]);
            int truth = 0;
            for (int c = 1; c < kNumClasses; ++c)
                if (testY[i][c] > testY[i][truth]) truth = c;
            if (pred == truth) ++correct;
        }
        float acc = static_cast<float>(correct) / testX.size();
        printf("\n  測試準確率：%d/%zu = %.1f%%（門檻 80%%）\n",
               correct, testX.size(), acc * 100.0f);

        if (acc < 0.80f) {
            printf("  [FAIL] 準確率不足——渲染資料未能被學習\n");
            break;
        }
        printf("  [PASS] 渲染 -> 資料 -> 學習 端到端通路驗證成功\n");
        exitCode = 0;
    } while (false);

    DestroyGLFWWindow(win);
    glfwTerminate();
    return exitCode;
}
