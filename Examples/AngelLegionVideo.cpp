/**
 * 天使軍團影片生成器 (Angel Legion Video Generator)
 *
 * 使用 Potato Engine 渲染系統(OpenGLRenderer / Shader / Mesh / Camera)
 * 程序化渲染天使軍團飛越天空的場景,並透過 glReadPixels 擷取畫面
 * 經由 FFmpeg 管道編碼為 MP4。
 *
 * 用法:
 *   AngelLegionVideo [輸出.mp4] [秒數] [fps]
 */

#include "Rendering/OpenGLRenderer.h"
#include "MathUtils/MathUtils.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Matrix4.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <iostream>

using namespace Potato;

// ============================================================================
// 著色器源碼
// ============================================================================

// 天空(全螢幕): 漸層 + 太陽光暈 + 程序化流雲 + 星辰
static const char* SKY_VERT = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
out vec2 vNdc;
void main() {
    vNdc = aPos.xy;
    gl_Position = vec4(aPos.xy, 0.9999, 1.0);
}
)";

static const char* SKY_FRAG = R"(
#version 330 core
in vec2 vNdc;
out vec4 FragColor;

uniform float uTime;
uniform vec3  uSunDir;    // 指向太陽
uniform mat4  uInvViewProj;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x),
               mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { v += a * noise(p); p *= 2.03; a *= 0.5; }
    return v;
}

void main() {
    // 重建視線方向
    vec4 clip = vec4(vNdc, 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    vec3 dir = normalize(world.xyz / world.w);

    float h = dir.y;

    // 黃昏天空漸層
    vec3 zenith  = vec3(0.10, 0.12, 0.30);
    vec3 mid     = vec3(0.55, 0.35, 0.50);
    vec3 horizon = vec3(1.00, 0.62, 0.28);
    vec3 below   = vec3(0.16, 0.10, 0.22);

    vec3 col;
    if (h >= 0.0) {
        float t1 = smoothstep(0.0, 0.25, h);
        float t2 = smoothstep(0.15, 0.9, h);
        col = mix(horizon, mid, t1);
        col = mix(col, zenith, t2);
    } else {
        col = mix(horizon, below, smoothstep(0.0, -0.4, h));
    }

    // 太陽與光暈
    float sd = max(dot(dir, uSunDir), 0.0);
    col += vec3(1.0, 0.85, 0.55) * pow(sd, 600.0) * 3.0;   // 日輪
    col += vec3(1.0, 0.70, 0.35) * pow(sd, 24.0) * 0.55;   // 內暈
    col += vec3(0.9, 0.50, 0.25) * pow(sd, 4.0) * 0.18;    // 大氣暈

    // 流雲(只在地平線上方)
    if (h > -0.05) {
        vec2 cp = dir.xz / max(h + 0.18, 0.06);
        cp = cp * 1.4 + vec2(uTime * 0.045, uTime * 0.012);
        float cl = fbm(cp);
        float cover = smoothstep(0.52, 0.78, cl) * smoothstep(0.9, 0.15, h);
        vec3 cloudCol = mix(vec3(0.9, 0.6, 0.5), vec3(1.0, 0.85, 0.7), sd);
        col = mix(col, cloudCol, cover * 0.75);
        // 雲隙金光
        col += vec3(1.0, 0.75, 0.4) * cover * pow(sd, 8.0) * 0.4;
    }

    // 高空星辰
    if (h > 0.35) {
        vec2 sp = dir.xz * 220.0;
        float star = step(0.9975, hash(floor(sp)));
        float tw = 0.5 + 0.5 * sin(uTime * 3.0 + hash(floor(sp) + 7.0) * 40.0);
        col += vec3(0.9, 0.95, 1.0) * star * tw * smoothstep(0.35, 0.7, h) * 0.8;
    }

    FragColor = vec4(col, 1.0);
}
)";

// 天使實體: 簡單光照 + 自發光 + 邊緣光(神聖感)
static const char* ANGEL_VERT = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

out vec3 FragPos;
out vec3 Normal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vec4 wp = model * vec4(aPos, 1.0);
    FragPos = wp.xyz;
    Normal = mat3(transpose(inverse(model))) * aNormal;
    gl_Position = projection * view * wp;
}
)";

static const char* ANGEL_FRAG = R"(
#version 330 core
in vec3 FragPos;
in vec3 Normal;
out vec4 FragColor;

uniform vec3 uColor;
uniform vec3 uEmissive;
uniform vec3 uLightPos;
uniform vec3 uViewPos;
uniform vec3 uRimColor;
uniform float uRimStrength;

void main() {
    vec3 N = normalize(Normal);
    vec3 L = normalize(uLightPos - FragPos);
    vec3 V = normalize(uViewPos - FragPos);

    float diff = max(dot(N, L), 0.0);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.2);

    vec3 col = uColor * (0.30 + 0.85 * diff);
    col += uEmissive;
    col += uRimColor * rim * uRimStrength;

    FragColor = vec4(col, 1.0);
}
)";

// 光暈公告板(加法混合)
static const char* GLOW_VERT = R"(
#version 330 core
layout (location = 0) in vec3 aPos;    // 單位四邊形 (-0.5..0.5)
layout (location = 2) in vec2 aUV;

out vec2 vUV;

uniform vec3 uCenter;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform float uSize;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vUV = aUV;
    vec3 wp = uCenter + uCamRight * aPos.x * uSize + uCamUp * aPos.y * uSize;
    gl_Position = projection * view * vec4(wp, 1.0);
}
)";

static const char* GLOW_FRAG = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D texture1;
uniform vec3 uTint;
uniform float uIntensity;

void main() {
    float a = texture(texture1, vUV).r;
    FragColor = vec4(uTint * a * uIntensity, a * uIntensity);
}
)";

// ============================================================================
// 程序化網格
// ============================================================================

static Mesh MakeSphere(int stacks = 16, int slices = 24) {
    std::vector<Vertex> verts;
    std::vector<uint32> idx;
    for (int i = 0; i <= stacks; i++) {
        float phi = PI * i / stacks;
        for (int j = 0; j <= slices; j++) {
            float theta = TWO_PI * j / slices;
            Vertex v{};
            v.normal = Vector3(Sin(phi) * Cos(theta), Cos(phi), Sin(phi) * Sin(theta));
            v.position = v.normal;
            v.texCoord = Vector2((float)j / slices, (float)i / stacks);
            verts.push_back(v);
        }
    }
    for (int i = 0; i < stacks; i++)
        for (int j = 0; j < slices; j++) {
            uint32 a = i * (slices + 1) + j, b = a + slices + 1;
            idx.insert(idx.end(), {a, b, a + 1, b, b + 1, a + 1});
        }
    Mesh m;
    m.SetVertices(verts);
    m.SetIndices(idx);
    return m;
}

// 光環: XZ 平面圓環
static Mesh MakeRing(float inner = 0.16f, float outer = 0.30f, int segs = 32) {
    std::vector<Vertex> verts;
    std::vector<uint32> idx;
    for (int i = 0; i <= segs; i++) {
        float a = TWO_PI * i / segs;
        Vertex vi{}, vo{};
        vi.position = Vector3(Cos(a) * inner, 0.0f, Sin(a) * inner);
        vo.position = Vector3(Cos(a) * outer, 0.0f, Sin(a) * outer);
        vi.normal = vo.normal = Vector3(0, 1, 0);
        vi.texCoord = Vector2(0, 0); vo.texCoord = Vector2(1, 1);
        verts.push_back(vi); verts.push_back(vo);
    }
    for (int i = 0; i < segs; i++) {
        uint32 b = i * 2;
        idx.insert(idx.end(), {b, b + 1, b + 2, b + 1, b + 3, b + 2});
    }
    Mesh m;
    m.SetVertices(verts);
    m.SetIndices(idx);
    return m;
}

// 左翼(沿 +X 伸展, 向 +Z 後掠), 由羽毛三角扇組成
static Mesh MakeWing() {
    std::vector<Vertex> verts;
    std::vector<uint32> idx;

    // 翼根覆羽(實心三角)
    auto addTri = [&](Vector3 a, Vector3 b, Vector3 c) {
        uint32 base = (uint32)verts.size();
        Vector3 n = (b - a).Cross(c - a).Normalized();
        for (auto& p : {a, b, c}) {
            Vertex v{};
            v.position = p; v.normal = n;
            verts.push_back(v);
        }
        idx.insert(idx.end(), {base, base + 1, base + 2});
    };

    addTri({0.05f, 0, 0.10f}, {0.9f, 0.10f, 0.05f}, {0.4f, 0, 0.45f});
    addTri({0.9f, 0.10f, 0.05f}, {1.5f, 0.16f, 0.35f}, {0.4f, 0, 0.45f});

    // 五片主羽: 根部窄, 羽端寬且後掠下垂
    for (int i = 0; i < 5; i++) {
        float f = i / 4.0f;
        Vector3 root(0.10f + f * 0.55f, 0.04f, 0.12f + f * 0.10f);
        Vector3 mid (0.55f + f * 0.95f, 0.12f + f * 0.10f, 0.20f + f * 0.28f);
        Vector3 tip (0.65f + f * 1.60f, -0.05f - f * 0.22f, 0.35f + f * 0.55f);
        addTri(root, mid, tip);
    }

    Mesh m;
    m.SetVertices(verts);
    m.SetIndices(idx);
    return m;
}

// 公告板單位四邊形
static Mesh MakeQuad() {
    std::vector<Vertex> verts(4);
    verts[0].position = {-0.5f, -0.5f, 0}; verts[0].texCoord = {0, 0};
    verts[1].position = { 0.5f, -0.5f, 0}; verts[1].texCoord = {1, 0};
    verts[2].position = { 0.5f,  0.5f, 0}; verts[2].texCoord = {1, 1};
    verts[3].position = {-0.5f,  0.5f, 0}; verts[3].texCoord = {0, 1};
    for (auto& v : verts) v.normal = Vector3(0, 0, 1);
    std::vector<uint32> idx = {0, 1, 2, 0, 2, 3};
    Mesh m;
    m.SetVertices(verts);
    m.SetIndices(idx);
    return m;
}

static Mesh MakeFullscreenTri() {
    std::vector<Vertex> verts(3);
    verts[0].position = {-1, -1, 0};
    verts[1].position = { 3, -1, 0};
    verts[2].position = {-1,  3, 0};
    std::vector<uint32> idx = {0, 1, 2};
    Mesh m;
    m.SetVertices(verts);
    m.SetIndices(idx);
    return m;
}

// 64x64 徑向光暈紋理
static Texture MakeGlowTexture() {
    const int S = 64;
    std::vector<unsigned char> data(S * S * 3);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float dx = (x - S / 2 + 0.5f) / (S / 2);
            float dy = (y - S / 2 + 0.5f) / (S / 2);
            float r = std::sqrt(dx * dx + dy * dy);
            float v = std::max(0.0f, 1.0f - r);
            v = v * v * (3.0f - 2.0f * v);   // smoothstep
            v *= v;                          // 更集中的光暈
            unsigned char c = (unsigned char)(v * 255);
            int o = (y * S + x) * 3;
            data[o] = data[o + 1] = data[o + 2] = c;
        }
    Texture t;
    t.LoadFromMemory(data.data(), S, S, 3);
    return t;
}

// ============================================================================
// 天使軍團
// ============================================================================

struct Angel {
    Vector3 offset;   // 編隊相對位置
    float phase;      // 振翅相位
    float bobPhase;   // 浮沉相位
    float scale;
};

static std::vector<Angel> BuildLegion() {
    std::vector<Angel> legion;
    auto vFormation = [&](Vector3 origin, int ranks, float sx, float sz, float scale) {
        legion.push_back({origin, 0.0f, (float)legion.size() * 1.7f, scale});
        for (int r = 1; r <= ranks; r++)
            for (int s = -1; s <= 1; s += 2) {
                Vector3 off = origin + Vector3(s * r * sx, r * 0.25f, r * sz);
                legion.push_back({off, r * 0.45f, (float)legion.size() * 1.7f, scale});
            }
    };

    // 主 V 編隊(領頭 + 兩翼各 15 列 = 31 名)
    vFormation(Vector3(0, 8.0f, 0), 15, 2.6f, 3.1f, 1.0f);
    // 上方護衛小編隊(13 名)
    vFormation(Vector3(0, 13.0f, 14.0f), 6, 2.4f, 2.8f, 0.85f);

    return legion;   // 共 44 名天使
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char** argv) {
    std::string outPath = (argc > 1) ? argv[1] : "angel_legion.mp4";
    float duration = (argc > 2) ? (float)atof(argv[2]) : 15.0f;
    int   fps      = (argc > 3) ? atoi(argv[3]) : 30;
    const int W = 1280, H = 720;
    const int totalFrames = (int)(duration * fps);

    std::cout << "=== Potato Engine - Angel Legion Video ===" << std::endl;
    std::cout << "Output: " << outPath << " | " << duration << "s @ " << fps << "fps" << std::endl;

    // ---- 渲染器初始化 ----
    OpenGLRenderer renderer;
    RendererConfig rcfg;
    rcfg.window.width = W;
    rcfg.window.height = H;
    rcfg.window.title = "Potato Engine - Angel Legion";
    rcfg.window.vsync = false;
    renderer.SetConfig(rcfg);
    if (!renderer.Initialize()) {
        std::cerr << "Renderer init failed" << std::endl;
        return 1;
    }
    renderer.SetViewport(0, 0, W, H);

    // ---- 著色器 ----
    Shader skyShader, angelShader, glowShader;
    if (!skyShader.LoadFromSource(SKY_VERT, SKY_FRAG) ||
        !angelShader.LoadFromSource(ANGEL_VERT, ANGEL_FRAG) ||
        !glowShader.LoadFromSource(GLOW_VERT, GLOW_FRAG)) {
        std::cerr << "Shader compile failed" << std::endl;
        return 1;
    }

    // ---- 網格與紋理 ----
    Mesh sphere   = MakeSphere();
    Mesh ring     = MakeRing();
    Mesh wing     = MakeWing();
    Mesh quad     = MakeQuad();
    Mesh skyTri   = MakeFullscreenTri();
    Texture glowTex = MakeGlowTexture();

    // ---- 天使軍團 ----
    std::vector<Angel> legion = BuildLegion();
    std::cout << "Legion size: " << legion.size() << " angels" << std::endl;

    // ---- 光之微粒 ----
    const int MOTES = 180;
    std::vector<Vector3> motes(MOTES);
    std::vector<float> motePhase(MOTES);
    for (int i = 0; i < MOTES; i++) {
        float h1 = (float)((i * 2654435761u) % 1000) / 1000.0f;
        float h2 = (float)((i * 40503u + 77) % 1000) / 1000.0f;
        float h3 = (float)((i * 65599u + 13) % 1000) / 1000.0f;
        motes[i] = Vector3((h1 - 0.5f) * 60.0f, 2.0f + h2 * 16.0f, (h3 - 0.5f) * 70.0f);
        motePhase[i] = h1 * TWO_PI;
    }

    // ---- 投影矩陣 ----
    Matrix4 proj = Matrix4::Perspective(50.0f * DEG_TO_RAD, (float)W / H, 0.1f, 400.0f);

    // ---- FFmpeg 管道 ----
    std::string cmd = "ffmpeg -y -f rawvideo -pix_fmt rgb24 -s " +
        std::to_string(W) + "x" + std::to_string(H) +
        " -r " + std::to_string(fps) +
        " -i - -vf vflip -an -c:v libx264 -pix_fmt yuv420p -crf 18 -movflags +faststart \"" +
        outPath + "\" 2> ffmpeg_encode.log";
    FILE* pipe = _popen(cmd.c_str(), "wb");
    if (!pipe) {
        std::cerr << "Failed to open ffmpeg pipe" << std::endl;
        return 1;
    }

    std::vector<unsigned char> frameBuf(W * H * 3);

    Vector3 sunDir = Vector3(-0.55f, 0.28f, -0.79f).Normalized();
    Vector3 lightPos = sunDir * 200.0f;

    std::cout << "Rendering " << totalFrames << " frames..." << std::endl;

    for (int f = 0; f < totalFrames; f++) {
        float t = (float)f / fps;
        float nt = t / duration;

        renderer.PollEvents();

        // ---- 編隊前進 ----
        Vector3 legionCenter(0, 0, -t * 2.2f);

        // ---- 相機運鏡: 前方左側 -> 前方右側 掃過 + 緩慢升高 ----
        float ease = nt * nt * (3.0f - 2.0f * nt);
        float orbitAng = (-0.55f + 1.10f * ease);          // -31° -> +31°
        float dist = 15.5f - 2.5f * ease;                  // 緩慢推近
        float camH = (-2.0f + 6.0f * ease);                // 仰角 -> 俯角
        Vector3 camPos = legionCenter + Vector3(
            Sin(orbitAng) * dist,
            8.0f + camH,
            -Cos(orbitAng) * dist - 2.0f);
        Vector3 camTarget = legionCenter + Vector3(0, 8.5f, -4.0f);

        Matrix4 view = Matrix4::LookAt(camPos, camTarget, Vector3(0, 1, 0));
        Matrix4 invVP = (proj * view).Inverse();

        // 公告板軸
        Vector3 camRight(view.m[0], view.m[4], view.m[8]);
        Vector3 camUp(view.m[1], view.m[5], view.m[9]);

        renderer.SetClearColor(Vector3(0.05f, 0.05f, 0.10f));
        renderer.Clear();

        // ---- 天空 ----
        glDepthMask(GL_FALSE);
        glDisable(GL_DEPTH_TEST);
        skyShader.Bind();
        skyShader.SetUniformFloat("uTime", t);
        skyShader.SetUniformVec3("uSunDir", sunDir);
        skyShader.SetUniformMat4("uInvViewProj", invVP);
        skyTri.Draw();
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);

        // ---- 天使 ----
        renderer.EnableCulling(false);
        angelShader.Bind();
        angelShader.SetUniformMat4("view", view);
        angelShader.SetUniformMat4("projection", proj);
        angelShader.SetUniformVec3("uLightPos", lightPos);
        angelShader.SetUniformVec3("uViewPos", camPos);
        angelShader.SetUniformVec3("uRimColor", Vector3(1.0f, 0.85f, 0.55f));

        for (const Angel& a : legion) {
            float bob = Sin(t * 1.6f + a.bobPhase) * 0.18f;
            Vector3 pos = legionCenter + a.offset + Vector3(0, bob, 0);
            float flap = Sin(t * 3.2f + a.phase) * 0.85f + 0.15f;
            float sc = a.scale;

            Matrix4 base = Matrix4::Translation(pos.x, pos.y, pos.z) *
                           Matrix4::Scale(sc, sc, sc);

            // 身軀(白袍)
            angelShader.SetUniformVec3("uColor", Vector3(0.98f, 0.94f, 0.86f));
            angelShader.SetUniformVec3("uEmissive", Vector3(0.30f, 0.22f, 0.12f));
            angelShader.SetUniformFloat("uRimStrength", 0.8f);
            angelShader.SetUniformMat4("model",
                base * Matrix4::Scale(0.34f, 0.78f, 0.34f));
            sphere.Draw();

            // 頭部
            angelShader.SetUniformVec3("uColor", Vector3(1.0f, 0.88f, 0.72f));
            angelShader.SetUniformVec3("uEmissive", Vector3(0.35f, 0.25f, 0.15f));
            angelShader.SetUniformMat4("model",
                base * Matrix4::Translation(0, 0.95f, -0.02f) *
                Matrix4::Scale(0.20f, 0.22f, 0.20f));
            sphere.Draw();

            // 光環(強自發光)
            angelShader.SetUniformVec3("uColor", Vector3(0, 0, 0));
            angelShader.SetUniformVec3("uEmissive", Vector3(1.0f, 0.82f, 0.35f) * 1.6f);
            angelShader.SetUniformFloat("uRimStrength", 0.0f);
            angelShader.SetUniformMat4("model",
                base * Matrix4::Translation(0, 1.28f, -0.02f));
            ring.Draw();

            // 雙翼(白羽帶暖光)
            angelShader.SetUniformVec3("uColor", Vector3(1.0f, 0.97f, 0.90f));
            angelShader.SetUniformVec3("uEmissive", Vector3(0.25f, 0.20f, 0.14f));
            angelShader.SetUniformFloat("uRimStrength", 1.2f);

            Vector3 sh(0, 0.55f, 0.18f);   // 肩部
            Matrix4 toSh = Matrix4::Translation(sh.x, sh.y, sh.z);
            Matrix4 fromSh = Matrix4::Translation(-sh.x, -sh.y, -sh.z);

            // 左翼(+X)
            angelShader.SetUniformMat4("model",
                base * toSh * Matrix4::RotationZ(flap) * fromSh);
            wing.Draw();
            // 右翼(鏡像)
            angelShader.SetUniformMat4("model",
                base * toSh * Matrix4::RotationZ(-flap) *
                Matrix4::Scale(-1, 1, 1) * fromSh);
            wing.Draw();
        }

        // ---- 光暈與微粒(加法混合) ----
        renderer.EnableBlending(true);
        renderer.SetBlendMode(BlendMode::Additive);
        glDepthMask(GL_FALSE);

        glowShader.Bind();
        glowShader.SetUniformMat4("view", view);
        glowShader.SetUniformMat4("projection", proj);
        glowShader.SetUniformVec3("uCamRight", camRight);
        glowShader.SetUniformVec3("uCamUp", camUp);
        glowShader.SetUniformInt("texture1", 0);
        glowTex.Bind(0);

        // 每位天使的聖光
        glowShader.SetUniformVec3("uTint", Vector3(1.0f, 0.85f, 0.55f));
        for (const Angel& a : legion) {
            float bob = Sin(t * 1.6f + a.bobPhase) * 0.18f;
            Vector3 pos = legionCenter + a.offset + Vector3(0, 0.6f + bob, 0);
            glowShader.SetUniformVec3("uCenter", pos);
            glowShader.SetUniformFloat("uSize", 4.2f * a.scale);
            glowShader.SetUniformFloat("uIntensity", 0.30f);
            quad.Draw();
        }

        // 飄浮光之微粒
        glowShader.SetUniformVec3("uTint", Vector3(1.0f, 0.90f, 0.65f));
        for (int i = 0; i < MOTES; i++) {
            Vector3 p = motes[i];
            p.y += Sin(t * 0.7f + motePhase[i]) * 0.8f;
            p.x += Sin(t * 0.3f + motePhase[i] * 2.0f) * 1.2f;
            p.z += legionCenter.z;  // 跟隨編隊
            glowShader.SetUniformVec3("uCenter", p);
            glowShader.SetUniformFloat("uSize", 0.45f);
            glowShader.SetUniformFloat("uIntensity",
                0.35f + 0.30f * Sin(t * 2.0f + motePhase[i] * 3.0f));
            quad.Draw();
        }

        glDepthMask(GL_TRUE);
        renderer.EnableBlending(false);
        renderer.EnableCulling(true);

        // ---- 擷取畫面 ----
        glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, frameBuf.data());
        fwrite(frameBuf.data(), 1, frameBuf.size(), pipe);

        renderer.Present();

        if (f % 30 == 0)
            std::cout << "  frame " << f << "/" << totalFrames << std::endl;
    }

    std::cout << "Finalizing encode..." << std::endl;
    _pclose(pipe);
    renderer.Shutdown();

    std::cout << "Done: " << outPath << std::endl;
    return 0;
}
