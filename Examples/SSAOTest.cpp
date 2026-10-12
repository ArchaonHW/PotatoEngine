// SSAOTest - AmbientOcclusion/G-Buffer 測試
// headless 段：kernel/noise 生成不變量（半球、長度、加速分布、決定性）
// GL 段：G-Buffer 幾何 pass → SSAO → blur，驗證遮蔽確實發生
// 無顯示環境下 GL 段跳過（PASS）——CI/headless 不應失敗

#include "MathUtils/MathUtils.h"
#include "Rendering/AmbientOcclusion.h"
#include "Rendering/GBuffer.h"
#include "Rendering/OpenGLRenderer.h"
#include "Platform/GLFWSharedContext.h"

#ifndef GLFW_INCLUDE_NONE // PotatoEngine PUBLIC 已定義
#define GLFW_INCLUDE_NONE
#endif
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Potato;

static int g_pass = 0;
static int g_fail = 0;
static int g_skip = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

// 以兩個三角形組成面朝 +Z 的 XY 平面（normal 朝相機）
static Mesh MakeQuad(float cx, float cy, float z, float half) {
    Mesh m;
    std::vector<Vertex> v(6);
    const Vector3 n(0.0f, 0.0f, 1.0f);
    const float p[6][2] = {
        {cx - half, cy - half}, {cx + half, cy - half}, {cx + half, cy + half},
        {cx - half, cy - half}, {cx + half, cy + half}, {cx - half, cy + half},
    };
    for (int i = 0; i < 6; ++i) {
        v[i].position = Vector3(p[i][0], p[i][1], z);
        v[i].normal = n;
        v[i].texCoord = Vector2(0.0f, 0.0f);
    }
    m.SetVertices(v);
    return m;
}

int main() {
    printf("=== SSAO Tests ===\n\n");

    // [1] headless：kernel 生成不變量
    {
        Check(AmbientOcclusion::GenerateKernel(0, 1).empty(),
              "GenerateKernel(0) 回空");

        const auto k = AmbientOcclusion::GenerateKernel(64, 12345);
        Check(k.size() == 64, "GenerateKernel(64) 數量正確");

        bool hemi = true, bounded = true;
        float firstHalf = 0.0f, secondHalf = 0.0f;
        for (size_t i = 0; i < k.size(); ++i) {
            if (k[i].z < 0.0f) hemi = false;                    // +Z 半球
            if (k[i].Length() > 1.0001f) bounded = false;       // scale <= 1
            if (i < 32) firstHalf += k[i].Length();
            else        secondHalf += k[i].Length();
        }
        Check(hemi, "kernel 全在 +Z 半球（z >= 0）");
        Check(bounded, "kernel 長度 <= 1");
        // 加速分布：後半平均長度應明顯大於前半（scale 隨 i^2 0.1→1.0）
        Check(secondHalf > firstHalf * 1.5f,
              "kernel 加速分布（後半平均長度 > 前半）");

        const auto k2 = AmbientOcclusion::GenerateKernel(64, 12345);
        Check(k == k2, "相同 seed kernel 可重現");
        const auto k3 = AmbientOcclusion::GenerateKernel(64, 54321);
        Check(k != k3, "不同 seed kernel 不同");
    }

    // [2] headless：噪聲生成不變量
    {
        Check(AmbientOcclusion::GenerateNoise(0, 1).empty(),
              "GenerateNoise(0) 回空");
        const auto n = AmbientOcclusion::GenerateNoise(4, 7);
        Check(n.size() == 16, "GenerateNoise(4) 產出 16 向量");
        bool unit = true, flat = true;
        for (const auto& v : n) {
            const float l = std::sqrt(v.x * v.x + v.y * v.y);
            if (std::fabs(l - 1.0f) > 1e-5f) unit = false;
            if (v.z != 0.0f) flat = false;
        }
        Check(unit, "噪聲向量 xy 單位長（旋轉向量）");
        Check(flat, "噪聲向量 z == 0（切線平面旋轉）");
        Check(n == AmbientOcclusion::GenerateNoise(4, 7),
              "相同 seed noise 可重現");
    }

    // [3] GL context 前建構——不得觸碰 GL、不得 crash
    {
        GBuffer gb;
        AmbientOcclusion ao;
        Check(!gb.IsValid() && !ao.IsValid(),
              "context 前建構不建立 GL 資源");
    }

    // [4] 真 GL context：隱藏窗口 + glad
    if (!glfwInit()) {
        printf("  [SKIP] glfwInit 失敗——無顯示環境,GL 路徑測試跳過\n");
        g_skip++;
    } else {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        GLFWwindow* win = glfwCreateWindow(64, 64, "ssao", nullptr, nullptr);
        if (!win) {
            printf("  [SKIP] glfwCreateWindow 失敗——GL 路徑測試跳過\n");
            g_skip++;
        } else {
            glfwMakeContextCurrent(win);
            if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
                printf("  [SKIP] gladLoadGLLoader 失敗——GL 路徑測試跳過\n");
                g_skip++;
            } else {
                glGetError(); // 清掉初始化殘留

                // G-Buffer + AO 建立
                GBuffer gb;
                Check(gb.Create(64, 64) && gb.IsValid(),
                      "GBuffer::Create 成功");
                Check(gb.GetPositionTexture() != 0 &&
                      gb.GetNormalTexture() != 0 &&
                      gb.GetAlbedoTexture() != 0,
                      "GBuffer 三張 attachment 建立");

                AmbientOcclusion ao;
                SSAOSettings cfg;
                cfg.kernelSize = 200; // 超界 → 夾到 64
                cfg.radius = 3.0f;
                Check(ao.Create(64, 64, cfg) && ao.IsValid(),
                      "AmbientOcclusion::Create 成功");
                Check(ao.GetSettings().kernelSize == 64,
                      "kernelSize 超界夾取到 64");

                // 場景（view=identity，相機在原點朝 -Z）：
                // 地板 z=-6 滿版 + 遮蔽板 z=-2.5 佔中央——
                // 地板靠近遮蔽板剪影的片段其半球樣本投影會落在遮蔽板深度上
                Mesh floor = MakeQuad(0.0f, 0.0f, -6.0f, 10.0f);
                // z=-2.5 處視錐半高 ≈1.44——half=0.7 只蓋螢幕中央,
                // 留出環狀地板讓剪影周圍產生遮蔽
                Mesh blocker = MakeQuad(0.0f, 0.0f, -2.5f, 0.7f);

                Shader geo;
                Check(geo.LoadFromSource(GBuffer::VertexShader(),
                                       GBuffer::FragmentShader()),
                      "GBuffer 幾何 shader 編譯");

                const Matrix4 model = Matrix4::Identity();
                const Matrix4 view = Matrix4::Identity();
                const Matrix4 proj = Matrix4::Perspective(
                    60.0f * DEG_TO_RAD, 1.0f, 0.1f, 50.0f);

                gb.Bind();
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glEnable(GL_DEPTH_TEST);
                geo.Bind();
                geo.SetUniformMat4("model", model);
                geo.SetUniformMat4("view", view);
                geo.SetUniformMat4("projection", proj);
                geo.SetUniformVec3("albedo", Vector3(1, 1, 1));
                geo.SetUniformInt("useTexture", 0);
                floor.Draw();
                blocker.Draw();
                geo.Unbind();
                gb.Unbind();
                Check(glGetError() == GL_NO_ERROR,
                      "GBuffer 幾何 pass 無 GL 錯誤");

                // AO 計算 + blur
                ao.Compute(gb, proj);
                Check(glGetError() == GL_NO_ERROR, "AO Compute 無 GL 錯誤");

                // 讀回 AO 貼圖：遮蔽板剪影周圍應出現遮蔽（值 < 1），
                // 全部值落 [0,1]（無 NaN/垃圾）
                auto readAO = [](uint32 tex) {
                    std::vector<float> buf(64 * 64);
                    glBindTexture(GL_TEXTURE_2D, tex);
                    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT,
                                  buf.data());
                    return buf;
                };
                const auto raw = readAO(ao.GetRawAOTexture());
                const auto blurred = readAO(ao.GetAOTexture());

                float rMin = 1.0f, rMax = 0.0f;
                int occluded = 0;
                bool inRange = true;
                for (float a : raw) {
                    if (!(a >= 0.0f && a <= 1.0001f)) inRange = false;
                    rMin = a < rMin ? a : rMin;
                    rMax = a > rMax ? a : rMax;
                    if (a < 0.95f) ++occluded;
                }
                Check(inRange, "raw AO 全落 [0,1]");
                Check(rMin < 0.9f && occluded > 0,
                      "遮蔽板附近偵測到遮蔽（raw min < 0.9）");

                bool blurInRange = true;
                for (float a : blurred) {
                    if (!(a >= 0.0f && a <= 1.0001f)) blurInRange = false;
                }
                Check(blurInRange, "blur AO 全落 [0,1]");
                Check(ao.GetAOTexture() != 0 && ao.GetRawAOTexture() != 0,
                      "AO/blur 貼圖 id 非零");

                // shader 源碼常數非空（消費端自建 pipeline 可用）
                Check(AmbientOcclusion::FullscreenVertexShader()[0] != '\0' &&
                      AmbientOcclusion::CompositeFragmentShader()[0] != '\0',
                      "內建 shader 源碼可取得");
            }
            DestroyGLFWWindow(win);
        }
        glfwTerminate();
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n  SKIP: %d\n",
           g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
