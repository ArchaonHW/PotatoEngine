// SkyboxTest - 延遲管線天空背景測試
// headless 段：DeferredLighting::SkyDir 方向重建語意
//   （uv→世界方向、相機旋轉、決定性）
// GL 段：空 GBuffer + EnvironmentMaps → 背景像素輸出 env cubemap
//   天空（HDR，走 bloom/tonemap 同一條鏈）；幾何遮蔽天空；
//   iblIntensity 線性縮放天空亮度
// 無顯示環境下 GL 段跳過（PASS）——CI/headless 不應失敗

#ifndef GLFW_INCLUDE_NONE // PotatoEngine PUBLIC 已定義
#define GLFW_INCLUDE_NONE
#endif
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "MathUtils/MathUtils.h"
#include "Rendering/DeferredShading.h"
#include "Rendering/GBuffer.h"
#include "Rendering/IBL.h"
#include "Rendering/OpenGLRenderer.h"
#include "Rendering/PostProcess.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Potato;

namespace {

int g_pass = 0, g_fail = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        ++g_pass;
        std::printf("[PASS] %s\n", name);
    } else {
        ++g_fail;
        std::printf("[FAIL] %s\n", name);
    }
}

GLFWwindow* MakeContext() {
    if (!glfwInit()) return nullptr;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(16, 16, "t", nullptr, nullptr);
    if (!win) { glfwTerminate(); return nullptr; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwDestroyWindow(win);
        glfwTerminate();
        return nullptr;
    }
    return win;
}

constexpr float kPI = 3.14159265358979f;

Mesh MakeQuad(float cx, float cy, float z, float half) {
    Mesh m;
    std::vector<Vertex> v(6);
    const Vector3 n(0.f, 0.f, 1.f);
    const float p[6][2] = {
        {cx - half, cy - half}, {cx + half, cy - half},
        {cx + half, cy + half}, {cx - half, cy - half},
        {cx + half, cy + half}, {cx - half, cy + half},
    };
    for (int i = 0; i < 6; ++i) {
        v[i].position = Vector3(p[i][0], p[i][1], z);
        v[i].normal = n;
        v[i].texCoord = Vector2(0.f, 0.f);
    }
    m.SetVertices(v);
    return m;
}

} // namespace

int main() {
    std::printf("=== Skybox Test ===\n\n");

    // ======== 純 CPU 段：SkyDir 方向重建 ========
    const Matrix4 ident = Matrix4::Identity();
    const Matrix4 proj =
        Matrix4::Perspective(60.f * kPI / 180.f, 1.f, 0.1f, 50.f);
    const Matrix4 invProj = proj.Inverse();

    {
        // 螢幕中央 = 相機正前方 -Z
        const Vector3 c = DeferredLighting::SkyDir({0.5f, 0.5f},
                                                   ident, invProj);
        Check(std::fabs(c.z + 1.f) < 1e-4f && std::fabs(c.x) < 1e-4f &&
                  std::fabs(c.y) < 1e-4f,
              "SkyDir 中央 = -Z（相機正前）");

        // 左右對稱、v=0 為螢幕下緣（y<0）
        const Vector3 l = DeferredLighting::SkyDir({0.f, 0.5f},
                                                   ident, invProj);
        const Vector3 r = DeferredLighting::SkyDir({1.f, 0.5f},
                                                   ident, invProj);
        Check(l.x < 0.f && r.x > 0.f &&
                  std::fabs(l.x + r.x) < 1e-4f,
              "SkyDir 左右對稱");
        const Vector3 b = DeferredLighting::SkyDir({0.5f, 0.f},
                                                   ident, invProj);
        const Vector3 t = DeferredLighting::SkyDir({0.5f, 1.f},
                                                   ident, invProj);
        Check(b.y < 0.f && t.y > 0.f &&
                  std::fabs(b.y + t.y) < 1e-4f,
              "SkyDir 上下對稱（v=0 為下緣）");

        // 全範圍輸出皆單位長
        bool unit = true;
        for (int y = 0; y <= 8; ++y) {
            for (int x = 0; x <= 8; ++x) {
                const Vector3 d = DeferredLighting::SkyDir(
                    {x / 8.f, y / 8.f}, ident, invProj);
                if (std::fabs(d.Length() - 1.f) > 1e-3f) unit = false;
            }
        }
        Check(unit, "SkyDir 全範圍單位長");

        // 相機旋轉：view=RotY(90°) → 正前方變世界 +X
        const Matrix4 view = Matrix4::RotationY(kPI / 2.f);
        const Vector3 cr = DeferredLighting::SkyDir(
            {0.5f, 0.5f}, view.Inverse(), invProj);
        Check(std::fabs(cr.x - 1.f) < 1e-3f && std::fabs(cr.z) < 1e-3f,
              "SkyDir 隨相機旋轉（RotY90 → +X）");

        // 平移不影響方向（天空與相機位置無關）
        Matrix4 viewT = Matrix4::Translation(Vector3(5.f, -3.f, 7.f));
        const Vector3 ct = DeferredLighting::SkyDir(
            {0.5f, 0.5f}, viewT.Inverse(), invProj);
        Check(std::fabs(ct.z + 1.f) < 1e-4f,
              "SkyDir 不受相機平移影響");

        // 決定性
        const Vector3 d1 = DeferredLighting::SkyDir({0.3f, 0.7f},
                                                    ident, invProj);
        const Vector3 d2 = DeferredLighting::SkyDir({0.3f, 0.7f},
                                                    ident, invProj);
        Check(d1.x == d2.x && d1.y == d2.y && d1.z == d2.z,
              "SkyDir 決定性");
    }

    // ======== GL 段 ========

    GLFWwindow* win = MakeContext();
    if (!win) {
        std::printf("SKIP: 無 OpenGL context，GL 段略過\n");
        std::printf("結果：%d 通過 / %d 失敗\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
    }
    Check(true, "建立隱藏 GL context");
    glGetError(); // 清掉初始化殘留

    // 環境：太陽放正前方 -Z（螢幕中央應有 HDR 亮點）
    const int EW = 64, EH = 32;
    const Vector3 sunDir{0.f, 0.f, -1.f};
    const auto sky = IBL::ProceduralSkyEquirect(EW, EH, sunDir, 15.f);

    EnvironmentMaps env;
    Check(env.Create(64), "EnvironmentMaps::Create");
    Check(env.Generate(sky.data(), EW, EH, 32, 128),
          "Generate 全鏈無 GL 錯誤");

    const int W = 64, H = 64;
    GBuffer gb;
    Check(gb.Create(W, H), "GBuffer 建立");

    DeferredLighting dl;
    Check(dl.Create(), "DeferredLighting 建立");
    HDRRenderTarget hdr;
    Check(hdr.Create(W, H), "HDR 目標建立");

    auto renderRead = [&](float iblIntensity,
                          std::vector<float>& out) {
        hdr.Bind();
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        dl.Render(gb, {}, {}, {}, ident, 0, nullptr, ident, 0.f, 0.005f,
                  &env, iblIntensity, 0.6f, 0.f, true, 1.f,
                  nullptr, nullptr, nullptr, proj);
        hdr.Unbind();
        hdr.Bind();
        hdr.ReadColor(out);
        hdr.Unbind();
    };
    // readback bottom-up：GL row0 = 像素 y=H-1；轉成 top-left 原點
    auto px = [&](const std::vector<float>& p, int x, int y) {
        return &p[((H - 1 - y) * W + x) * 4];
    };

    // (a) 空 GBuffer → 全背景：中央（太陽）遠亮於角落
    {
        gb.Bind();
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gb.Unbind();

        std::vector<float> img;
        renderRead(1.0f, img);
        Check(glGetError() == GL_NO_ERROR, "天空渲染無 GL 錯誤");

        const float* center = px(img, 32, 32);
        const float* corner = px(img, 2, 2);
        const float lumC = center[0] + center[1] + center[2];
        const float lumK = corner[0] + corner[1] + corner[2];
        Check(lumC > 1.0f, "天空中央有 HDR 太陽亮點（>1）");
        Check(lumC > lumK * 2.0f, "中央遠亮於角落（太陽盤方向性）");
        Check(lumK > 0.05f, "角落也非死黑（天穹漸層）");
        std::printf("    center=%.3f corner=%.3f\n", lumC, lumK);

        // iblIntensity 線性縮放
        std::vector<float> dim;
        renderRead(0.5f, dim);
        const float lumD = px(dim, 32, 32)[0] + px(dim, 32, 32)[1] +
                           px(dim, 32, 32)[2];
        Check(lumD < lumC * 0.75f && lumD > lumC * 0.25f,
              "iblIntensity 線性縮放天空亮度");
    }

    // (b) 中央畫一塊 quad → 太陽被幾何遮蔽，角落天空仍在
    {
        Mesh quad = MakeQuad(0.f, 0.f, -5.f, 1.2f);
        Shader geo;
        Check(geo.LoadFromSource(GBuffer::VertexShader(),
                                 GBuffer::FragmentShader()),
              "幾何 shader 編譯");
        gb.Bind();
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        geo.Bind();
        geo.SetUniformMat4("model", ident);
        geo.SetUniformMat4("view", ident);
        geo.SetUniformMat4("projection", proj);
        geo.SetUniformInt("useTexture", 0);
        geo.SetUniformVec3("albedo", Vector3(0.8f, 0.8f, 0.8f));
        geo.SetUniformFloat("materialMetallic", 0.0f);
        geo.SetUniformFloat("materialRoughness", 0.6f);
        geo.SetUniformFloat("materialEmissive", 0.0f);
        quad.Draw();
        geo.Unbind();
        gb.Unbind();

        std::vector<float> img;
        renderRead(1.0f, img);
        const float* center = px(img, 32, 32);
        const float* corner = px(img, 2, 2);
        const float lumC = center[0] + center[1] + center[2];
        const float lumK = corner[0] + corner[1] + corner[2];
        Check(lumC < 1.0f, "幾何遮蔽太陽（中央不再是天空 HDR）");
        Check(lumK > 0.05f, "角落天空仍輸出（幾何只遮局部）");
        std::printf("    occluded center=%.3f corner=%.3f\n",
                    lumC, lumK);
    }

    glfwDestroyWindow(win);
    glfwTerminate();

    std::printf("結果：%d 通過 / %d 失敗\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
