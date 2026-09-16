// PortraitRenderer - 利用引擎 glTF/VRM 載入管線產生人物立繪 PNG
//
// 用法:
//   PortraitRenderer <input.vrm|glb|gltf> <output.png>
//                    [--region head|half|full]  取景範圍（預設 half）
//                    [--size WxH]               輸出解析度（預設 768x1024）
//                    [--bg R,G,B,A]             背景色 0-255（預設 0,0,0,0 透明）
//                    [--yaw 角度]               繞 Y 軸旋轉模型（預設 0）
//
// 流程：隱藏 GLFW 窗口建立 GL context → GLTFLoader(tinygltf) 解析 →
//       FBO 離屏渲染（正交投影、依 AABB 取景）→ glReadPixels →
//       ImageCodec::WritePNGFile 寫出。

#include "Rendering/ModelLoader.h"
#include "Rendering/OpenGLRenderer.h"
#include "Rendering/Shader.h"
#include "Rendering/ImageCodec.h"
#include "Platform/GLFWSharedContext.h"
#include "MathUtils/Matrix4.h"
#include "MathUtils/MathUtils.h"
#include "Logging/Logger.h"

// GLFW_INCLUDE_NONE 已由專案編譯選項定義（避免 GLFW 拉入系統 gl.h 與 glad 衝突）
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace Potato;

namespace {

const char* kVertSrc = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)GLSL";

const char* kFragSrc = R"GLSL(
#version 330 core
in vec2 vUV;
uniform sampler2D baseColorTexture;
uniform bool useTexture;
uniform float alphaCutoff;
out vec4 FragColor;
void main() {
    vec4 c = useTexture ? texture(baseColorTexture, vUV) : vec4(1.0);
    if (c.a < alphaCutoff) discard;
    FragColor = c;
}
)GLSL";

struct Args {
    std::string input;
    std::string output;
    std::string region = "half";
    int width = 768;
    int height = 1024;
    float bg[4] = {0, 0, 0, 0};
    float yawDeg = 0.0f;
};

void PrintUsage() {
    printf("用法: PortraitRenderer <input.vrm|glb|gltf> <output.png>\n");
    printf("  [--region head|half|full]  取景範圍（預設 half：上半身）\n");
    printf("  [--size WxH]               輸出解析度（預設 768x1024）\n");
    printf("  [--bg R,G,B,A]             背景色 0-255（預設 0,0,0,0 透明）\n");
    printf("  [--yaw 角度]               繞 Y 旋轉模型（預設 0）\n");
}

// 解析 n 個以 sep 分隔的整數（CI 禁用 sscanf，手寫 strtol 版本）
bool ParseInts(const char* s, char sep, int* out, int n) {
    if (!s) return false;
    for (int i = 0; i < n; ++i) {
        char* end = nullptr;
        long v = std::strtol(s, &end, 10);
        if (end == s || v < -100000 || v > 100000) return false;
        out[i] = static_cast<int>(v);
        s = end;
        if (i + 1 < n) {
            if (*s != sep) return false;
            ++s;
        }
    }
    return *s == '\0';
}

bool ParseArgs(int argc, char** argv, Args& args) {
    if (argc < 3) return false;
    args.input = argv[1];
    args.output = argv[2];
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--region") == 0 && i + 1 < argc) {
            args.region = argv[++i];
        } else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            int wh[2];
            if (!ParseInts(argv[++i], 'x', wh, 2) ||
                wh[0] <= 0 || wh[1] <= 0 || wh[0] > 8192 || wh[1] > 8192) {
                printf("無效的 --size\n");
                return false;
            }
            args.width = wh[0];
            args.height = wh[1];
        } else if (std::strcmp(argv[i], "--bg") == 0 && i + 1 < argc) {
            int rgba[4];
            if (!ParseInts(argv[++i], ',', rgba, 4)) {
                printf("無效的 --bg\n");
                return false;
            }
            for (int k = 0; k < 4; ++k)
                args.bg[k] = rgba[k] / 255.0f;
        } else if (std::strcmp(argv[i], "--yaw") == 0 && i + 1 < argc) {
            args.yawDeg = static_cast<float>(std::atof(argv[++i]));
        }
    }
    return true;
}

// 模型 AABB（頂點已烘焙世界變換）
void ComputeAABB(const ModelData& md, Vector3& mn, Vector3& mx) {
    mn = Vector3(1e30f, 1e30f, 1e30f);
    mx = Vector3(-1e30f, -1e30f, -1e30f);
    for (const auto& mesh : md.meshes) {
        for (const auto& v : mesh.vertices) {
            mn.x = (std::min)(mn.x, v.position.x);
            mn.y = (std::min)(mn.y, v.position.y);
            mn.z = (std::min)(mn.z, v.position.z);
            mx.x = (std::max)(mx.x, v.position.x);
            mx.y = (std::max)(mx.y, v.position.y);
            mx.z = (std::max)(mx.z, v.position.z);
        }
    }
}

// 依 region 決定取景的 Y 區間
void RegionRange(const std::string& region, float minY, float maxY,
                 float& y0, float& y1) {
    float h = maxY - minY;
    if (region == "head") {
        y0 = maxY - h * 0.24f;   // 頭部以上約 24%
        y1 = maxY;
    } else if (region == "full") {
        y0 = minY;
        y1 = maxY;
    } else {                      // half：胸像以上約 55%
        y0 = maxY - h * 0.55f;
        y1 = maxY;
    }
}

// 建立離屏 FBO（RGBA8 color texture + depth renderbuffer）
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

} // namespace

int main(int argc, char** argv) {
    Args args;
    if (!ParseArgs(argc, argv, args)) {
        PrintUsage();
        return 1;
    }
    if (args.region != "head" && args.region != "half" &&
        args.region != "full") {
        printf("未知 region: %s\n", args.region.c_str());
        return 1;
    }

    // ---- GL context（隱藏窗口）----
    if (!glfwInit()) {
        printf("glfwInit 失敗——無顯示環境無法渲染\n");
        return 1;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "portrait", nullptr, nullptr);
    if (!win) {
        printf("glfwCreateWindow 失敗\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        printf("gladLoadGLLoader 失敗\n");
        DestroyGLFWWindow(win);
        glfwTerminate();
        return 1;
    }

    int exitCode = 1;
    do {
        // ---- 載入模型 ----
        ModelData modelData;
        if (!GLTFLoader::LoadFromFile(args.input, modelData)) {
            printf("模型載入失敗: %s\n", args.input.c_str());
            break;
        }
        if (modelData.hasVrmExtension)
            printf("偵測到 VRM 擴充（以綁定姿勢靜態渲染）\n");

        Vector3 mn, mx;
        ComputeAABB(modelData, mn, mx);
        printf("AABB: (%.3f,%.3f,%.3f) ~ (%.3f,%.3f,%.3f)\n",
               mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);

        // GPU 資源（需要 context）
        Model model;
        if (!model.LoadFromData(modelData)) {
            printf("GPU 資源建立失敗\n");
            break;
        }

        // ---- 取景 ----
        float y0, y1;
        RegionRange(args.region, mn.y, mx.y, y0, y1);
        float cx = (mn.x + mx.x) * 0.5f;
        float cy = (y0 + y1) * 0.5f;
        float cz = (mn.z + mx.z) * 0.5f;
        float halfH = (y1 - y0) * 0.5f * 1.06f;              // 6% 留白
        float aspect = static_cast<float>(args.width) / args.height;
        float halfW = halfH * aspect;
        float minHalfW = (mx.x - mn.x) * 0.5f * 1.06f;
        if (halfW < minHalfW) halfW = minHalfW;              // 寬度不足時以模型寬度為準
        // VRM 面向 +Z：相機放前方 +Z 側
        float camZ = mx.z + (mx.z - mn.z) + 1.0f;
        Matrix4 view = Matrix4::LookAt(Vector3(cx, cy, camZ),
                                       Vector3(cx, cy, cz),
                                       Vector3(0, 1, 0));
        float nearP = 0.01f;
        float farP = camZ - mn.z + 1.0f;
        Matrix4 proj = Matrix4::Orthographic(-halfW, halfW, -halfH, halfH,
                                             nearP, farP);

        // ---- FBO ----
        GLuint fbo = 0, colorTex = 0, depthRb = 0;
        if (!CreateFBO(args.width, args.height, fbo, colorTex, depthRb)) {
            printf("FBO 建立失敗\n");
            break;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, args.width, args.height);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);   // VRM 雙面材質常見
        glClearColor(args.bg[0], args.bg[1], args.bg[2], args.bg[3]);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ---- 渲染 ----
        AdvancedShader shader;
        if (!shader.LoadFromSource(kVertSrc, kFragSrc)) {
            printf("shader 編譯失敗\n");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            break;
        }
        shader.Bind();
        shader.SetMat4("view", view);
        shader.SetMat4("projection", proj);
        shader.SetFloat("alphaCutoff", 0.5f);

        model.SetRotation(Quaternion::FromAxisAngle(
            Vector3(0, 1, 0), args.yawDeg * 3.14159265f / 180.0f));
        model.Draw(shader);

        // ---- 讀回 + 翻轉列序（GL 由下而上 → PNG 由上而下）----
        std::vector<unsigned char> pixels(
            static_cast<size_t>(args.width) * args.height * 4);
        glFinish();
        glReadPixels(0, 0, args.width, args.height, GL_RGBA,
                     GL_UNSIGNED_BYTE, pixels.data());
        std::vector<unsigned char> flipped(pixels.size());
        size_t rowBytes = static_cast<size_t>(args.width) * 4;
        for (int y = 0; y < args.height; ++y) {
            std::memcpy(flipped.data() + static_cast<size_t>(y) * rowBytes,
                        pixels.data() +
                            static_cast<size_t>(args.height - 1 - y) * rowBytes,
                        rowBytes);
        }

        std::string err;
        if (!ImageCodec::WritePNGFile(args.output, args.width, args.height,
                                     flipped.data(), &err)) {
            printf("PNG 寫出失敗: %s\n", err.c_str());
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            break;
        }
        printf("立繪輸出: %s (%dx%d)\n", args.output.c_str(),
               args.width, args.height);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &colorTex);
        glDeleteRenderbuffers(1, &depthRb);
        exitCode = 0;
    } while (false);

    DestroyGLFWWindow(win);
    glfwTerminate();
    return exitCode;
}
