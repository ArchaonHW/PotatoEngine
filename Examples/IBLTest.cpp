// IBL：image-based lighting 基建測試。
// 前半純 CPU（headless）：Hammersley/GGX 採樣、BRDF LUT 端點、
// equirect→cubemap 映射、irradiance 卷積、prefilter、程序化天空。
// 後半 GL-gated：EnvironmentMaps.Create/Generate 上傳 +
// glGetTexImage 讀回驗證 irradiance/prefilter 內容語意。

#ifndef GLFW_INCLUDE_NONE // PotatoEngine PUBLIC 已定義
#define GLFW_INCLUDE_NONE
#endif
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Rendering/IBL.h"
#include "Rendering/GBuffer.h"
#include "Rendering/DeferredShading.h"
#include "Rendering/OpenGLRenderer.h"
#include "Rendering/PostProcess.h"
#include "MathUtils/Matrix4.h"

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
    std::printf("=== IBL Test ===\n\n");

    // ======== 純 CPU 段 ========

    // RadicalInverse / Hammersley
    Check(std::fabs(IBL::RadicalInverse(1) - 0.5f) < 1e-6f,
          "RadicalInverse(1)=0.5");
    Check(std::fabs(IBL::RadicalInverse(2) - 0.25f) < 1e-6f,
          "RadicalInverse(2)=0.25");
    {
        bool inRange = true;
        for (uint32 i = 0; i < 256; ++i) {
            const Vector2 xi = IBL::Hammersley(i, 256);
            if (xi.x < 0 || xi.x >= 1 || xi.y < 0 || xi.y >= 1)
                inRange = false;
        }
        Check(inRange, "Hammersley 序列全在 [0,1)");
    }

    // GGX 重要性採樣：H 在 N 半球內且正規化
    {
        const Vector3 n{0, 0, 1};
        bool ok = true;
        for (uint32 i = 0; i < 128; ++i) {
            const Vector3 h = IBL::ImportanceSampleGGX(
                IBL::Hammersley(i, 128), n, 0.5f);
            if (h.Dot(n) < 0.f || std::fabs(h.Length() - 1.f) > 1e-3f)
                ok = false;
        }
        Check(ok, "GGX 採樣：H 在半球內且單位長");
        // roughness=0 → 全部 H ≈ N（鏡面退化）
        const Vector3 h0 = IBL::ImportanceSampleGGX({0.3f, 0.7f}, n, 0.f);
        Check(std::fabs(h0.z - 1.f) < 1e-4f,
              "roughness=0 → H=N（鏡面退化）");
    }

    // BRDF LUT：鏡面端點 (NoV=1, rough=0) ≈ (1,0)；值域有界
    {
        const Vector2 mirror = IBL::IntegrateBRDF(1.0f, 0.0f, 512);
        Check(std::fabs(mirror.x - 1.f) < 0.05f &&
                  std::fabs(mirror.y) < 0.05f,
              "BRDF(NoV=1,r=0)≈(1,0) 鏡面端點");
        const Vector2 rough = IBL::IntegrateBRDF(0.5f, 1.0f, 512);
        Check(rough.x >= 0.f && rough.x <= 1.f &&
                  rough.y >= 0.f && rough.y <= 1.f,
              "BRDF 值域 [0,1]");

        const auto lut = IBL::BuildBRDFLUT(16, 128);
        Check(lut.size() == 16 * 16 * 2, "LUT 尺寸 RG32F");
        Check(lut[16 * 2] >= 0.f, "LUT 讀取無異常");
    }

    // 程序化天空 + equirect 採樣
    const int EW = 128, EH = 64;
    const Vector3 sunDir{0.f, 0.7f, -0.7f}; // 高空偏 -z
    const auto sky = IBL::ProceduralSkyEquirect(EW, EH, sunDir, 15.f);
    Check(sky.size() == static_cast<size_t>(EW) * EH * 4,
          "程序化天空尺寸");
    {
        const Vector3 sunPx = IBL::SampleEquirect(sky.data(), EW, EH,
                                                  sunDir);
        Check(sunPx.x > 10.f, "太陽方向像素 HDR 高強度");
        const Vector3 zenith =
            IBL::SampleEquirect(sky.data(), EW, EH, {0, 1, 0});
        const Vector3 ground =
            IBL::SampleEquirect(sky.data(), EW, EH, {0, -1, 0});
        Check(zenith.z > zenith.x, "天頂偏藍（B>R）");
        Check(ground.x < 0.2f, "地平線下暗色");
    }

    // equirect→cubemap：+Y face 中央應為天頂藍
    {
        const auto faceUp =
            IBL::EquirectToCubeFace(sky.data(), EW, EH, 2, 32);
        const float* c = faceUp.data() + ((16 * 32) + 16) * 4;
        Check(c[2] > c[0], "cubemap +Y 中央為天頂藍");
        // 太陽 (0,0.7,-0.7) 主軸 -Z：-Z 面上應有 HDR 亮點
        // （位置在面內偏下緣——掃全臉找最大值而非猜中心）
        const auto faceNegZ =
            IBL::EquirectToCubeFace(sky.data(), EW, EH, 5, 32);
        float maxR = 0;
        for (int i = 0; i < 32 * 32; ++i)
            maxR = std::max(maxR, faceNegZ[i * 4]);
        Check(maxR > 1.f, "-Z 面含太陽 HDR 亮點（>1）");
    }

    // irradiance 卷積：+Y face 平均應被天空染色（偏藍且非零）
    {
        const auto irr =
            IBL::ConvolveIrradianceFace(sky.data(), EW, EH, 2, 8,
                                        8, 16);
        float rs = 0, bs = 0;
        for (int i = 0; i < 8 * 8; ++i) {
            rs += irr[i * 4 + 0];
            bs += irr[i * 4 + 2];
        }
        Check(bs > rs && bs > 1.f,
              "irradiance +Y 半球均值偏藍非零");
    }

    // prefilter：roughness=0 時 ≈ env 原圖（太陽盤仍亮）
    {
        const auto pf = IBL::PrefilterFace(sky.data(), EW, EH, 5, 16,
                                           0.f, 32);
        bool anyBright = false;
        for (int i = 0; i < 16 * 16; ++i)
            if (pf[i * 4] > 1.f) anyBright = true;
        Check(anyBright, "prefilter r=0 保留 HDR 亮點");
    }

    // ======== GL 段 ========

    GLFWwindow* win = MakeContext();
    Check(win != nullptr, "建立隱藏 GL context");
    if (!win) {
        std::printf("SKIP: 無 OpenGL context，GL 段略過\n");
        std::printf("結果：%d 通過 / %d 失敗\n", g_pass, g_fail);
        return 0;
    }

    EnvironmentMaps env;
    Check(env.Create(64), "EnvironmentMaps::Create");
    Check(env.GetEnvCubemap() && env.GetIrradianceMap() &&
              env.GetPrefilterMap() && env.GetBRDFLUT(),
          "四張 IBL 紋理皆配置");

    Check(env.Generate(sky.data(), EW, EH, 32, 128),
          "Generate 全鏈無 GL 錯誤");

    // 讀回 irradiance +Y face：應偏藍（天空卷積結果）
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP, env.GetIrradianceMap());
        std::vector<float> face(32 * 32 * 4);
        glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, 0, GL_RGBA,
                      GL_FLOAT, face.data());
        Check(glGetError() == GL_NO_ERROR, "irradiance 讀回");
        const float* c = face.data() + ((16 * 32) + 16) * 4;
        Check(c[2] > c[0] && c[2] > 0.1f,
              "irradiance cubemap +Y 中央偏藍");
    }

    // 讀回 BRDF LUT：鏡面端點像素 (NoV≈1, rough=0) ≈ (1,0)
    {
        glBindTexture(GL_TEXTURE_2D, env.GetBRDFLUT());
        std::vector<float> lut(32 * 32 * 2);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RG, GL_FLOAT, lut.data());
        const float* mirror = lut.data() + (31 * 2); // x=31 列 NoV 最大
        Check(mirror[0] > 0.8f && mirror[1] < 0.2f,
              "BRDF LUT 鏡面端點上傳正確");
    }

    // ---- 整合：GBuffer gMaterial 通道 → IBL per-pixel 材質 ----
    {
        const int W = 64, H = 64;
        GBuffer gb;
        Check(gb.Create(W, H), "整合：GBuffer 建立");
        Check(gb.GetMaterialTexture() != 0, "整合：gMaterial attachment");

        Shader geo;
        Check(geo.LoadFromSource(GBuffer::VertexShader(),
                                 GBuffer::FragmentShader()),
              "整合：幾何 shader");

        // 左金屬滑面 / 右粗糙介電 / 上方自發光小板
        Mesh leftQuad = MakeQuad(-2.1f, -1.f, -6.f, 1.2f);
        Mesh rightQuad = MakeQuad(2.1f, -1.f, -6.f, 1.2f);
        Mesh emisQuad = MakeQuad(0.f, 2.2f, -6.f, 0.6f);

        const Matrix4 ident = Matrix4::Identity();
        const Matrix4 proj =
            Matrix4::Perspective(60.f * kPI / 180.f, 1.f, 0.1f, 50.f);

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

        geo.SetUniformFloat("materialMetallic", 1.0f);
        geo.SetUniformFloat("materialRoughness", 0.05f);
        geo.SetUniformFloat("materialEmissive", 0.0f);
        leftQuad.Draw();
        geo.SetUniformFloat("materialMetallic", 0.0f);
        geo.SetUniformFloat("materialRoughness", 1.0f);
        rightQuad.Draw();
        geo.SetUniformFloat("materialEmissive", 1.0f);
        emisQuad.Draw();
        geo.Unbind();
        gb.Unbind();

        // 無光源 + IBL：左金屬/右介電應有別，自發光不吃光仍亮
        DeferredLighting dl;
        Check(dl.Create(), "整合：DeferredLighting 建立");
        HDRRenderTarget hdr;
        Check(hdr.Create(W, H), "整合：HDR 目標建立");
        hdr.Bind();
        dl.Render(gb, {}, {}, {}, ident, 0, nullptr, ident, 0.f, 0.005f,
                  &env, 1.0f);
        hdr.Unbind();
        Check(glGetError() == GL_NO_ERROR, "整合：IBL 渲染無錯誤");

        hdr.Bind();
        std::vector<float> pix;
        Check(hdr.ReadColor(pix), "整合：HDR 讀回");
        hdr.Unbind();
        // readback bottom-up：GL row0 = 像素 y=H-1
        auto pf = [&](int x, int y) -> const float* {
            return &pix[((H - 1 - y) * W + x) * 4];
        };
        const float* metal = pf(16, 40);   // 左下區：金屬
        const float* diel = pf(48, 40);    // 右下區：介電
        const float* emis = pf(32, 12);    // 上方自發光（y=2.2→頂部 row）
        const float lumMetal = metal[0] + metal[1] + metal[2];
        const float lumDiel = diel[0] + diel[1] + diel[2];
        const float lumEmis = emis[0] + emis[1] + emis[2];

        Check(lumEmis > 0.5f,
              "整合：自發光無光源仍亮（emissive 通道）");
        Check(std::fabs(lumMetal - lumDiel) > 0.02f,
              "整合：金屬/介電 IBL 響應不同（per-pixel PBR）");
        std::printf("    metal=%.3f diel=%.3f emis=%.3f\n",
                    lumMetal, lumDiel, lumEmis);
    }

    glfwDestroyWindow(win);
    glfwTerminate();

    std::printf("結果：%d 通過 / %d 失敗\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
