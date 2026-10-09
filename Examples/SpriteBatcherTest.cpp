// SpriteBatcher：GPU instanced 精靈批次渲染測試。
// 前半純 CPU：pack 布局（14 float/實例）、SortByZ 穩定性、
// SetUV 從 SpriteFrame 接線——無 GL 皆可跑。
// 後半 GL-gated：VAO/instance VBO 建立 + 單次 instanced draw
// 批次畫出多個精靈（不同位置/色調/旋轉）+ RenderTarget 讀回驗證
// 每個精靈落在各自位置且帶各自色調。

#ifndef GLFW_INCLUDE_NONE // PotatoEngine PUBLIC 已定義
#define GLFW_INCLUDE_NONE
#endif
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Rendering/SpriteBatcher.h"
#include "Rendering/RenderTarget.h"
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

GLFWwindow* MakeContext(int w, int h) {
    if (!glfwInit()) return nullptr;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(w, h, "test", nullptr, nullptr);
    if (!win) { glfwTerminate(); return nullptr; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwDestroyWindow(win);
        glfwTerminate();
        return nullptr;
    }
    return win;
}

uint32 MakeCheckerTexture() {
    // 4x4 全不透明灰階棋盤：值 0.2 / 0.8 交錯
    uint8 px[4 * 4 * 4];
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const uint8 v = ((x + y) % 2) ? 204 : 51;
            const int i = (y * 4 + x) * 4;
            px[i] = px[i + 1] = px[i + 2] = v;
            px[i + 3] = 255;
        }
    uint32 t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

SpriteInstance MakeSprite(float x, float y, float w, float h,
                          float r, float g, float b, float rotDeg = 0) {
    SpriteInstance s;
    s.x = x; s.y = y; s.w = w; s.h = h;
    s.r = r; s.g = g; s.b = b;
    const float rad = rotDeg * 3.14159265f / 180.0f;
    s.rotCos = std::cos(rad);
    s.rotSin = std::sin(rad);
    return s;
}

} // namespace

int main() {
    std::printf("=== SpriteBatcher Test ===\n\n");

    // ======== 純 CPU 段 ========

    {
        std::vector<SpriteInstance> batch;
        batch.push_back(MakeSprite(10, 20, 16, 8, 1, 0.5f, 0.25f, 90));
        batch.back().u0 = 0.1f; batch.back().v0 = 0.2f;
        batch.back().u1 = 0.9f; batch.back().v1 = 0.8f;
        auto packed = SpriteBatcher::PackInstances(batch);
        Check(packed.size() == 14, "pack 每實例 14 float");
        Check(packed[0] == 10.f && packed[1] == 20.f,
              "pack offset");
        Check(packed[2] == 16.f && packed[3] == 8.f,
              "pack size");
        Check(packed[4] == 0.1f && packed[7] == 0.8f,
              "pack uv rect");
        Check(packed[8] == 1.f && packed[9] == 0.5f &&
              packed[10] == 0.25f && packed[11] == 1.f,
              "pack color");
        Check(std::fabs(packed[12]) < 1e-5f &&
              std::fabs(packed[13] - 1.f) < 1e-5f,
              "pack rotation（90 度→cos≈0 sin≈1）");
    }

    {
        // z 排序穩定性：同 z 保持提交順序
        std::vector<SpriteInstance> batch;
        for (int i = 0; i < 4; ++i) {
            SpriteInstance s;
            s.x = static_cast<float>(i); // 記錄原始序
            s.z = (i == 1) ? -1.f : ((i == 3) ? 2.f : 0.f);
            batch.push_back(s);
        }
        SpriteBatcher::SortByZ(batch);
        Check(batch[0].x == 1.f, "sort z=-1 排最前");
        Check(batch[1].x == 0.f && batch[2].x == 2.f,
              "sort 同 z 穩定序");
        Check(batch[3].x == 3.f, "sort z=2 排最後");
    }

    {
        SpriteFrame f;
        f.u0 = 0.25f; f.v0 = 0.5f; f.u1 = 0.5f; f.v1 = 0.75f;
        SpriteInstance s;
        s.SetUV(&f);
        Check(s.u0 == 0.25f && s.v1 == 0.75f,
              "SetUV 接線 SpriteFrame");
        SpriteInstance whole;
        whole.SetUV(nullptr);
        Check(whole.u0 == 0.f && whole.u1 == 1.f &&
              whole.v0 == 0.f && whole.v1 == 1.f,
              "SetUV(nullptr) 整圖");
    }

    {
        SpriteBatcher headless;
        Check(!headless.IsValid(), "context 前建構無效");
        std::vector<SpriteInstance> one(1);
        headless.Render(one, 0, Matrix4()); // 不應崩潰
        Check(true, "未建立時 Render 安全早退");
    }

    // ======== GL 段 ========

    const int W = 64, H = 64;
    GLFWwindow* win = MakeContext(W, H);
    Check(win != nullptr, "建立隱藏 GL context");
    if (!win) {
        std::printf("SKIP: 無 OpenGL context，GL 段略過\n");
        std::printf("結果：%d 通過 / %d 失敗\n", g_pass, g_fail);
        return 0;
    }

    RenderTarget target;
    Check(target.Create(W, H), "輸出 RenderTarget");

    SpriteBatcher batcher;
    Check(batcher.Create(), "SpriteBatcher::Create");

    uint32 atlas = MakeCheckerTexture();

    target.Bind();
    glViewport(0, 0, W, H);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    // 三個精靈：左上紅調（非旋轉）、右下藍調旋轉 45 度、中央綠調
    std::vector<SpriteInstance> batch;
    batch.push_back(MakeSprite(8, 8, 8, 8, 1, 0, 0));
    batch.push_back(MakeSprite(56, 56, 8, 8, 0, 0, 1, 45));
    batch.push_back(MakeSprite(32, 32, 16, 16, 0, 1, 0));
    batcher.Render(batch, atlas,
                   Matrix4::Orthographic(0, (float)W, (float)H, 0, -1, 1));

    Check(glGetError() == GL_NO_ERROR, "instanced draw 無 GL 錯誤");

    std::vector<uint8> frame;
    Check(target.ReadColor(frame), "讀回輸出");

    // GL 讀回 bottom-up：row 0 = 像素 y=H-1
    auto px = [&](int x, int y) -> const uint8* {
        return &frame[((H - 1 - y) * W + x) * 4];
    };

    // 紅精靈中心（8,8）：R 顯著、G/B 低
    const uint8* red = px(8, 8);
    Check(red[0] > 40 && red[1] < 20 && red[2] < 20,
          "紅調精靈落在左上");

    // 綠精靈中心（32,32）：G 主導
    const uint8* green = px(32, 32);
    Check(green[1] > 40 && green[0] < 20 && green[2] < 20,
          "綠調精靈落在中央");

    // 藍精靈 45 度旋轉：中心（56,56）仍是藍（旋轉不影響中心採樣）
    const uint8* blue = px(56, 56);
    Check(blue[2] > 40 && blue[0] < 20 && blue[1] < 20,
          "藍調旋轉精靈落在右下");

    // 旋轉 45 度：方形變菱形——軸向延伸到 ±4·√2≈5.66px、
    // 對角方向縮短。
    // (60,56) 片元中心 (60.5,56.5) 距精靈中心 (4.5,0.5)：
    // 未旋轉 |4.5|>4 不畫；旋轉後 local=(3.54,-2.83) 在界內
    // → 畫出藍色（菱形尖端）
    const uint8* tip = px(60, 56);
    Check(tip[2] > 20,
          "旋轉 45 度：軸向覆蓋延伸到菱形尖端");
    // 反向驗證：(59,59) 距中心 (3,3)，local=(4.24,0)>4 → 黑
    const uint8* diag = px(59, 59);
    Check(diag[0] < 20 && diag[1] < 20 && diag[2] < 20,
          "旋轉 45 度：對角縮短（原方形角外露底）");
    // 未旋轉的紅精靈 (8,8) 在 (12,12) 對角處仍在框內（正方形角），
    // 而軸外 (8,14) 距中心 (0,6)>4 應為黑——驗證方形邊界正確
    const uint8* outOfRed = px(8, 14);
    Check(outOfRed[0] < 20 && outOfRed[1] < 20 && outOfRed[2] < 20,
          "精靈邊界外為底色（非旋轉方形）");

    // 空區域：底部中央 (32,60) 無精靈 → 黑
    const uint8* empty = px(32, 60);
    Check(empty[0] < 20 && empty[1] < 20 && empty[2] < 20,
          "無精靈區域保持底色");

    target.Unbind();
    glDeleteTextures(1, &atlas);
    glfwDestroyWindow(win);
    glfwTerminate();

    std::printf("結果：%d 通過 / %d 失敗\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
