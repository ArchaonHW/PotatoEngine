// PostProcessTest - HDR/Bloom/ToneMap 後處理鏈測試
// headless 段：GaussianWeights 正規化/單調、ACESFilmic 單調且飽和 <1
// GL 段：HDR 場景（左半超亮右半暗）→ bloom 提取 → tonemap 讀回驗證
// 無顯示環境下 GL 段跳過（PASS）——CI/headless 不應失敗

#include "MathUtils/MathUtils.h"
#include "Rendering/PostProcess.h"
#include "Rendering/RenderTarget.h"
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

int main() {
    printf("=== Post Process Tests ===\n\n");

    // [1] headless：Gaussian 權重
    {
        Check(Bloom::GaussianWeights(0, 1.0f).empty(),
              "GaussianWeights(0) 回空");
        Check(Bloom::GaussianWeights(4, 0.0f).empty(),
              "GaussianWeights(sigma=0) 回空");
        const auto w = Bloom::GaussianWeights(4, 1.6f);
        Check(w.size() == 5, "GaussianWeights(4) 回傳 radius+1=5");

        float total = w[0];
        for (size_t i = 1; i < w.size(); ++i) total += 2.0f * w[i];
        Check(std::fabs(total - 1.0f) < 1e-5f, "權重正規化和為 1");
        Check(w[0] > w[1] && w[1] > w[4],
              "權重中心遞減（w0 > w1 > w4）");
    }

    // [2] headless：ACES 曲線
    {
        const Vector3 zero = ToneMapper::ACESFilmic(Vector3(0, 0, 0));
        Check(zero.x == 0.0f, "ACES(0) = 0");
        const float low = ToneMapper::ACESFilmic(Vector3(0.5f, 0, 0)).x;
        const float high = ToneMapper::ACESFilmic(Vector3(2.0f, 0, 0)).x;
        Check(high > low, "ACES 單調遞增");
        const float sat = ToneMapper::ACESFilmic(Vector3(100.0f, 0, 0)).x;
        Check(sat <= 1.0f && sat > 0.9f, "ACES 飽和收斂於 (0.9, 1.0]");
    }

    // [3] GL context 前建構——不得觸碰 GL、不得 crash
    {
        HDRRenderTarget hdr;
        Bloom bloom;
        Check(!hdr.IsValid() && !bloom.IsValid(),
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
        GLFWwindow* win = glfwCreateWindow(64, 64, "pp", nullptr, nullptr);
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

                // HDR 場景：左半 (6,5,4) 超亮，右半 (0.08) 暗
                HDRRenderTarget hdr;
                Check(hdr.Create(64, 64) && hdr.IsValid(),
                      "HDRRenderTarget::Create 成功");

                Shader scene;
                const char* sceneFS = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoords;
void main()
{
    FragColor = TexCoords.x < 0.5 ? vec4(6.0, 5.0, 4.0, 1.0)
                                : vec4(vec3(0.08), 1.0);
}
)";
                Check(scene.LoadFromSource(Bloom::FullscreenVertexShader(),
                                         sceneFS),
                      "HDR 場景 shader 編譯");
                hdr.Bind();
                glDisable(GL_DEPTH_TEST);
                scene.Bind();
                uint32 vao = 0;
                glGenVertexArrays(1, &vao);
                glBindVertexArray(vao);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glBindVertexArray(0);
                scene.Unbind();
                hdr.Unbind();
                Check(glGetError() == GL_NO_ERROR,
                      "HDR 場景渲染無 GL 錯誤");

                // Bloom：threshold=1.0，左半應產生明顯亮部
                Bloom bloom;
                BloomSettings bs;
                bs.threshold = 1.0f;
                bs.blurPasses = 2;
                Check(bloom.Create(64, 64, bs) && bloom.IsValid(),
                      "Bloom::Create 成功");
                Check(bloom.GetWorkWidth() == 32 && bloom.GetWorkHeight() == 32,
                      "工作解析度 = 64/2 = 32");
                bloom.Extract(hdr.GetColorTexture());
                Check(glGetError() == GL_NO_ERROR, "Bloom 提取無 GL 錯誤");

                // 讀回 bloom 貼圖：左半亮部均值應顯著高於右半
                std::vector<float> bpx(32 * 32 * 4);
                glBindTexture(GL_TEXTURE_2D, bloom.GetTexture());
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT,
                              bpx.data());
                float lSum = 0.0f, rSum = 0.0f;
                for (int y = 0; y < 32; ++y) {
                    for (int x = 0; x < 32; ++x) {
                        const float v = bpx[(y * 32 + x) * 4];
                        if (x < 16) lSum += v; else rSum += v;
                    }
                }
                const float lAvg = lSum / (32.0f * 16.0f);
                const float rAvg = rSum / (32.0f * 16.0f);
                Check(lAvg > 0.05f, "左半亮部被提取（bloom > 0）");
                Check(lAvg > rAvg * 3.0f, "bloom 空間分布正確（左遠亮於右）");
                printf("  [info] bloom 左均 %.3f / 右均 %.3f\n", lAvg, rAvg);

                // ToneMap：ACES 壓縮 HDR → LDR
                RenderTarget ldr;
                Check(ldr.Create(64, 64), "LDR RenderTarget 建立");
                ToneMapper tm;
                Check(tm.Create() && tm.IsValid(), "ToneMapper 建立");
                ldr.Bind();
                tm.Apply(hdr.GetColorTexture(), bloom.GetTexture(),
                         1.0f, 1.0f);
                ldr.Unbind();
                Check(glGetError() == GL_NO_ERROR, "ToneMap 無 GL 錯誤");

                std::vector<unsigned char> px;
                ldr.Bind();
                Check(ldr.ReadColor(px), "LDR 讀回");
                ldr.Unbind();
                const int lR = px[(32 * 64 + 8) * 4];   // 左半中間
                const int rR = px[(32 * 64 + 56) * 4];  // 右半中間
                Check(lR > rR, "左半比右半亮");
                Check(lR > 200,
                      "ACES 壓縮：HDR 6.x 映射到近飽和亮區 (>200)");
                Check(rR > 10 && rR < 220, "暗區映射到合理 LDR 範圍");
                printf("  [info] LDR 左 %d / 右 %d\n", lR, rR);

                glDeleteVertexArrays(1, &vao);
            }
            DestroyGLFWWindow(win);
        }
        glfwTerminate();
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n  SKIP: %d\n",
           g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
