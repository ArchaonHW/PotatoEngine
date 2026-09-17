// PortraitRenderer - 利用引擎 glTF/VRM 載入管線產生人物立繪 PNG
//
// 用法（單張）:
//   PortraitRenderer <input.vrm|glb|gltf> <output.png>
//                    [--region head|half|full]   取景範圍（預設 half）
//                    [--size WxH]                輸出解析度（預設 768x1024）
//                    [--bg R,G,B,A]              背景色 0-255（預設透明）
//                    [--yaw 角度]                繞 Y 旋轉（預設 0）
//                    [--pose relax|none]         手臂放鬆姿勢（預設 relax）
//
// 用法（批量）:
//   PortraitRenderer --batch <模型目錄> <輸出目錄>
//                    [--regions head,half,full]  多範圍（預設 half）
//                    [--yaws -30,0,30]           多角度（預設 0）
//                    [--labels <csv路徑>]        寫出標註 manifest
//   輸出檔名: <模型stem>_<region>_y<yaw>.png
//
// 流程：隱藏 GLFW 窗口建立 GL context → GLTFLoader(tinygltf) 解析 →
//       骨骼名稱啟發式放鬆姿勢（T-pose → A-pose）→ CPU 蒙皮求姿後 AABB →
//       FBO 離屏渲染（正交投影、band-limited 取景、Lambert+rim 打光）→
//       glReadPixels → ImageCodec::WritePNGFile 寫出。

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
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Potato;

namespace {

constexpr int kMaxJoints = 128; // shader uniform 陣列上限

const char* kVertSrc = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=5) in vec4 aJoints;
layout(location=6) in vec4 aWeights;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform bool uHasSkin;
uniform int uJointCount;
uniform mat4 uJointMatrices[128];
out vec2 vUV;
out vec3 vNormal;
void main() {
    mat4 skin = mat4(1.0);
    if (uHasSkin) {
        skin = mat4(0.0);
        float wsum = 0.0;
        for (int i = 0; i < 4; ++i) {
            int j = int(aJoints[i] + 0.5);
            float w = aWeights[i];
            if (j >= 0 && j < uJointCount && j < 128 && w > 0.0) {
                skin += w * uJointMatrices[j];
                wsum += w;
            }
        }
        skin = (wsum > 1e-6) ? skin / wsum : mat4(1.0);
    }
    vec4 p = skin * vec4(aPos, 1.0);
    vUV = aUV;
    vNormal = mat3(skin) * aNormal;
    gl_Position = projection * view * model * p;
}
)GLSL";

const char* kFragSrc = R"GLSL(
#version 330 core
in vec2 vUV;
in vec3 vNormal;
uniform sampler2D baseColorTexture;
uniform bool useTexture;
uniform float alphaCutoff;
uniform vec3 uLightDir;
uniform float uAmbient;
out vec4 FragColor;
void main() {
    vec4 c = useTexture ? texture(baseColorTexture, vUV) : vec4(1.0);
    if (c.a < alphaCutoff) discard;
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;              // VRM 雙面材質常見
    float d = max(dot(N, normalize(uLightDir)), 0.0);
    // Lambert 主光 + 環境 + 輕微輪廓光（透明背景下人物分離感）
    float rim = pow(1.0 - abs(N.z), 3.0) * 0.25;
    vec3 col = c.rgb * (uAmbient + (1.0 - uAmbient) * d) + rim * c.rgb;
    FragColor = vec4(col, c.a);
}
)GLSL";

enum class PoseMode { Relax, None };

struct Args {
    std::string input;
    std::string output;
    bool batch = false;
    std::vector<std::string> regions = {"half"};
    std::vector<float> yaws = {0.0f};
    int width = 768;
    int height = 1024;
    float bg[4] = {0, 0, 0, 0};
    float yawDeg = 0.0f;             // 單張模式
    std::string region = "half";     // 單張模式
    std::string labelsPath;
    PoseMode pose = PoseMode::Relax;
};

void PrintUsage() {
    printf("用法（單張）: PortraitRenderer <input> <output.png>\n");
    printf("  [--region head|half|full]  [--size WxH]  [--bg R,G,B,A]\n");
    printf("  [--yaw 角度]  [--pose relax|none]\n");
    printf("用法（批量）: PortraitRenderer --batch <dir> <outdir>\n");
    printf("  [--regions head,half,full]  [--yaws -30,0,30]  [--labels out.csv]\n");
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

std::vector<std::string> SplitStr(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t comma = s.find(sep, pos);
        if (comma == std::string::npos) { out.push_back(s.substr(pos)); break; }
        out.push_back(s.substr(pos, comma - pos));
        pos = comma + 1;
    }
    return out;
}

bool ValidRegion(const std::string& r) {
    return r == "head" || r == "half" || r == "full";
}

bool ParseArgs(int argc, char** argv, Args& args) {
    if (argc < 3) return false;
    int i = 1;
    if (std::strcmp(argv[i], "--batch") == 0) {
        args.batch = true;
        ++i;
    }
    if (i + 1 >= argc) return false;
    args.input = argv[i];
    args.output = argv[i + 1];
    for (i += 2; i < argc; ++i) {
        const char* a = argv[i];
        auto next = [&](const char* flag) -> const char* {
            return (i + 1 < argc) ? argv[++i] : nullptr;
        };
        if (std::strcmp(a, "--region") == 0) {
            const char* v = next(a); if (!v) return false;
            args.region = v;
        } else if (std::strcmp(a, "--regions") == 0) {
            const char* v = next(a); if (!v) return false;
            args.regions = SplitStr(v, ',');
        } else if (std::strcmp(a, "--yaw") == 0) {
            const char* v = next(a); if (!v) return false;
            args.yawDeg = static_cast<float>(std::atof(v));
        } else if (std::strcmp(a, "--yaws") == 0) {
            const char* v = next(a); if (!v) return false;
            args.yaws.clear();
            for (const auto& tok : SplitStr(v, ','))
                args.yaws.push_back(static_cast<float>(std::atof(tok.c_str())));
        } else if (std::strcmp(a, "--size") == 0) {
            const char* v = next(a); if (!v) return false;
            int wh[2];
            if (!ParseInts(v, 'x', wh, 2) ||
                wh[0] <= 0 || wh[1] <= 0 || wh[0] > 8192 || wh[1] > 8192) {
                printf("無效的 --size\n");
                return false;
            }
            args.width = wh[0]; args.height = wh[1];
        } else if (std::strcmp(a, "--bg") == 0) {
            const char* v = next(a); if (!v) return false;
            int rgba[4];
            if (!ParseInts(v, ',', rgba, 4)) { printf("無效的 --bg\n"); return false; }
            for (int k = 0; k < 4; ++k) args.bg[k] = rgba[k] / 255.0f;
        } else if (std::strcmp(a, "--pose") == 0) {
            const char* v = next(a); if (!v) return false;
            args.pose = (std::strcmp(v, "none") == 0) ? PoseMode::None
                                                      : PoseMode::Relax;
        } else if (std::strcmp(a, "--labels") == 0) {
            const char* v = next(a); if (!v) return false;
            args.labelsPath = v;
        } else {
            printf("未知參數: %s\n", a);
            return false;
        }
    }
    if (!args.batch) return ValidRegion(args.region);
    for (const auto& r : args.regions)
        if (!ValidRegion(r)) { printf("未知 region: %s\n", r.c_str()); return false; }
    return true;
}

// ---- 姿勢：T-pose → A-pose（骨骼名稱啟發式）-----------------------------

std::string ToLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
    return s;
}

bool HasAny(const std::string& n, std::initializer_list<const char*> subs) {
    for (const char* s : subs)
        if (n.find(s) != std::string::npos) return true;
    return false;
}

// 在 model 的節點名稱裡找出指定側/部位的骨頭 index。
// 支援 VRM0(J_Bip_L_UpperArm)、VRM1(leftUpperArm)、Mixamo(LeftArm)。
int FindBone(Model& model, const ModelData& md, bool left,
             std::initializer_list<const char*> parts,
             std::initializer_list<const char*> excludes = {}) {
    static const char* kLeft[] = {"_l_", "left", "_l.", "lft"};
    static const char* kRight[] = {"_r_", "right", "_r.", "rgt"};
    const char* const* sideTokens = left ? kLeft : kRight;
    const int sideCount = 4;
    for (size_t i = 0; i < md.nodes.size(); ++i) {
        std::string n = ToLower(md.nodes[i].name);
        bool sideMatch = false;
        for (int t = 0; t < sideCount; ++t)
            if (n.find(sideTokens[t]) != std::string::npos) { sideMatch = true; break; }
        if (!sideMatch) continue;
        if (!HasAny(n, parts)) continue;
        if (HasAny(n, excludes)) continue;
        return (int)i;
    }
    return -1;
}

// 放鬆手臂：上臂下垂 ~70°、前臂內收 ~15°、肩膀微沉 ~8°。
// 旋轉在 parent 空間疊加（RotateNodeLocal），對 VRM 慣例的軀幹鏈近似成立。
void ApplyRelaxPose(Model& model, const ModelData& md) {
    struct Rule { bool left; float upperZ; float foreZ; float shoulderZ; };
    // 左臂(+X) 繞 Z 負轉放下；右臂(-X) 繞 Z 正轉
    const Rule rules[2] = {{true, -70.0f, -15.0f, -8.0f},
                           {false, 70.0f, 15.0f, 8.0f}};
    const float kDeg = 3.14159265f / 180.0f;
    int applied = 0;
    for (const Rule& r : rules) {
        int upper = FindBone(model, md, r.left,
                             {"upperarm", "upper_arm", "arm"},
                             {"forearm", "lowerarm", "lower_arm", "hand",
                              "fore", "elbow"});
        int fore = FindBone(model, md, r.left,
                            {"forearm", "lowerarm", "lower_arm", "elbow"});
        int shoulder = FindBone(model, md, r.left, {"shoulder", "clavicle"});
        if (upper >= 0) {
            model.RotateNodeLocal(upper, Quaternion::FromAxisAngle(
                Vector3(0, 0, 1), r.upperZ * kDeg));
            ++applied;
        }
        if (fore >= 0) {
            model.RotateNodeLocal(fore, Quaternion::FromAxisAngle(
                Vector3(0, 0, 1), r.foreZ * kDeg));
            ++applied;
        }
        if (shoulder >= 0) {
            model.RotateNodeLocal(shoulder, Quaternion::FromAxisAngle(
                Vector3(0, 0, 1), r.shoulderZ * kDeg));
            ++applied;
        }
    }
    if (applied > 0)
        printf("放鬆姿勢：%d 個關節已調整\n", applied);
}

// ---- AABB：姿後頂點（CPU 蒙皮），band-limited ----------------------------

Vector3 SkinPoint(const ModelData& md, const MeshData& mesh,
                  size_t vi, const Model& model) {
    const Vector3& p = mesh.vertices[vi].position;
    if (mesh.joints.empty() || vi >= mesh.joints.size() ||
        vi >= mesh.weights.size() || mesh.skinIndex < 0) {
        return p;
    }
    const auto& palette = model.GetJointPalette(mesh.skinIndex);
    const auto& j = mesh.joints[vi];
    const Vector4& w = mesh.weights[vi];
    const float ws[4] = {w.x, w.y, w.z, w.w};
    const int js[4] = {j[0], j[1], j[2], j[3]};
    float wsum = ws[0] + ws[1] + ws[2] + ws[3];
    if (wsum < 1e-6f) return p;
    Vector3 out(0, 0, 0);
    for (int k = 0; k < 4; ++k) {
        if (ws[k] <= 0.0f || js[k] < 0 || js[k] >= (int)palette.size())
            continue;
        Vector3 tp = palette[js[k]].TransformPoint(p);
        out = out + tp * ws[k];
    }
    return out / wsum;
}

// 先以 bind AABB 求 Y band，再只統計落在 band 內的（姿後）頂點水平範圍
void ComputeFraming(const ModelData& md, const Model& model,
                    const std::string& region,
                    Vector3& center, float& halfH, float& halfW) {
    Vector3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
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
    float h = mx.y - mn.y;
    float y0, y1;
    if (region == "head") {
        y0 = mx.y - h * 0.26f; y1 = mx.y;
    } else if (region == "full") {
        y0 = mn.y; y1 = mx.y;
    } else {
        y0 = mx.y - h * 0.55f; y1 = mx.y;
    }
    // band 內姿後頂點的水平範圍（放下的手臂不參與取景）
    float bx0 = 1e30f, bx1 = -1e30f, bz0 = 1e30f, bz1 = -1e30f;
    float by0 = 1e30f, by1 = -1e30f;
    for (const auto& mesh : md.meshes) {
        for (size_t vi = 0; vi < mesh.vertices.size(); ++vi) {
            Vector3 p = SkinPoint(md, mesh, vi, model);
            if (p.y < y0 || p.y > y1) continue;
            bx0 = (std::min)(bx0, p.x); bx1 = (std::max)(bx1, p.x);
            bz0 = (std::min)(bz0, p.z); bz1 = (std::max)(bz1, p.z);
            by0 = (std::min)(by0, p.y); by1 = (std::max)(by1, p.y);
        }
    }
    if (bx0 > bx1) { // band 空：退回全域
        bx0 = mn.x; bx1 = mx.x; bz0 = mn.z; bz1 = mx.z;
        by0 = y0; by1 = y1;
    }
    center = Vector3((bx0 + bx1) * 0.5f, (by0 + by1) * 0.5f,
                     (bz0 + bz1) * 0.5f);
    halfH = (by1 - by0) * 0.5f * 1.06f;
    halfW = (bx1 - bx0) * 0.5f * 1.06f;
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

struct RenderCtx {
    GLuint fbo = 0, colorTex = 0, depthRb = 0;
    AdvancedShader shader;
    bool ok = false;
};

bool InitRenderCtx(int w, int h, RenderCtx& ctx) {
    if (!CreateFBO(w, h, ctx.fbo, ctx.colorTex, ctx.depthRb)) return false;
    if (!ctx.shader.LoadFromSource(kVertSrc, kFragSrc)) return false;
    ctx.ok = true;
    return true;
}

void DestroyRenderCtx(RenderCtx& ctx) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &ctx.fbo);
    glDeleteTextures(1, &ctx.colorTex);
    glDeleteRenderbuffers(1, &ctx.depthRb);
}

// 渲染單張立繪；ctx/模型由呼叫端持有（批量時重用 GL 資源）
bool RenderPortrait(const ModelData& md, Model& model,
                    RenderCtx& ctx, const Args& args,
                    const std::string& region, float yawDeg,
                    const std::string& outPath) {
    Vector3 center;
    float halfH, halfW;
    ComputeFraming(md, model, region, center, halfH, halfW);

    float aspect = static_cast<float>(args.width) / args.height;
    float w = halfH * aspect;
    if (w < halfW) w = halfW;

    // VRM 面向 +Z：相機放前方；距離依模型 z 範圍留 buffer
    float zBack = center.z, zFront = center.z;
    for (const auto& mesh : md.meshes) {
        for (const auto& v : mesh.vertices) {
            zBack = (std::min)(zBack, v.position.z);
            zFront = (std::max)(zFront, v.position.z);
        }
    }
    float camZ = zFront + 2.0f;
    Matrix4 view = Matrix4::LookAt(Vector3(center.x, center.y, camZ),
                                   center, Vector3(0, 1, 0));
    Matrix4 proj = Matrix4::Orthographic(-w, w, -halfH, halfH,
                                         0.01f, camZ - zBack + 2.0f);

    glBindFramebuffer(GL_FRAMEBUFFER, ctx.fbo);
    glViewport(0, 0, args.width, args.height);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glClearColor(args.bg[0], args.bg[1], args.bg[2], args.bg[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ctx.shader.Bind();
    ctx.shader.SetMat4("view", view);
    ctx.shader.SetMat4("projection", proj);
    ctx.shader.SetFloat("alphaCutoff", 0.5f);
    ctx.shader.SetVec3("uLightDir", Vector3(0.45f, 0.8f, 0.65f));
    ctx.shader.SetFloat("uAmbient", 0.45f);

    model.SetRotation(Quaternion::FromAxisAngle(
        Vector3(0, 1, 0), yawDeg * 3.14159265f / 180.0f));
    model.Draw(ctx.shader);

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
    if (!ImageCodec::WritePNGFile(outPath, args.width, args.height,
                                  flipped.data(), &err)) {
        printf("PNG 寫出失敗 %s: %s\n", outPath.c_str(), err.c_str());
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Args args;
    if (!ParseArgs(argc, argv, args)) {
        PrintUsage();
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
    std::ofstream labels;
    if (!args.labelsPath.empty()) {
        labels.open(args.labelsPath);
        if (labels)
            labels << "filename,model,region,yaw,width,height\n";
    }

    do {
        RenderCtx ctx;
        if (!InitRenderCtx(args.width, args.height, ctx)) {
            printf("FBO/shader 初始化失敗\n");
            break;
        }

        // 收集輸入清單（單檔或目錄批量）
        std::vector<std::filesystem::path> inputs;
        std::error_code ec;
        if (args.batch) {
            for (const auto& e :
                 std::filesystem::directory_iterator(args.input, ec)) {
                auto ext = e.path().extension().string();
                for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                if (ext == ".vrm" || ext == ".glb" || ext == ".gltf")
                    inputs.push_back(e.path());
            }
            std::sort(inputs.begin(), inputs.end());
            std::filesystem::create_directories(args.output, ec);
        } else {
            inputs.push_back(args.input);
        }
        if (inputs.empty()) { printf("找不到輸入模型\n"); break; }

        bool allOk = true;
        for (const auto& in : inputs) {
            ModelData md;
            if (!GLTFLoader::LoadFromFile(in.string(), md)) {
                printf("模型載入失敗: %s\n", in.string().c_str());
                allOk = false;
                continue;
            }
            Model model;
            if (!model.LoadFromData(md)) {
                printf("GPU 資源建立失敗: %s\n", in.string().c_str());
                allOk = false;
                continue;
            }
            if (args.pose == PoseMode::Relax) ApplyRelaxPose(model, md);

            const auto& regions = args.batch ? args.regions
                                             : std::vector<std::string>{args.region};
            const auto& yaws = args.batch ? args.yaws
                                          : std::vector<float>{args.yawDeg};
            for (const auto& region : regions) {
                for (float yaw : yaws) {
                    std::string out;
                    if (args.batch) {
                        char suffix[64];
                        std::snprintf(suffix, sizeof(suffix),
                                      "_%s_y%d.png", region.c_str(), (int)yaw);
                        out = args.output + "/" + in.stem().string() + suffix;
                    } else {
                        out = args.output;
                    }
                    if (!RenderPortrait(md, model, ctx, args, region, yaw,
                                        out)) {
                        allOk = false;
                        continue;
                    }
                    printf("立繪輸出: %s (%dx%d)\n", out.c_str(),
                           args.width, args.height);
                    if (labels) {
                        labels << std::filesystem::path(out).filename().string()
                               << ',' << in.filename().string() << ','
                               << region << ',' << yaw << ',' << args.width
                               << ',' << args.height << '\n';
                    }
                }
            }
        }
        DestroyRenderCtx(ctx);
        exitCode = allOk ? 0 : 1;
    } while (false);

    DestroyGLFWWindow(win);
    glfwTerminate();
    return exitCode;
}
