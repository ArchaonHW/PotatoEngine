#include "Rendering/SpriteBatcher.h"

#include <glad/glad.h>
#include <algorithm>

namespace Potato {

namespace {
    // 每實例 float 數：offset2 + size2 + uv4 + color4 + rot2
    constexpr int kInstanceFloats = 14;

    const char* kSpriteVS = R"GLSL(
#version 330 core

layout(location=0) in vec2 aPos;   // unit quad 0..1
layout(location=1) in vec2 aUV;    // unit quad uv
layout(location=2) in vec2 iOffset;   // 中心像素座標
layout(location=3) in vec2 iSize;     // 尺寸
layout(location=4) in vec4 iUV;       // u0 v0 u1 v1
layout(location=5) in vec4 iColor;
layout(location=6) in vec2 iRot;      // cos sin

uniform mat4 u_projection;

out vec2 v_uv;
out vec4 v_color;

void main() {
    vec2 local = (aPos - 0.5) * iSize;
    vec2 rotated = vec2(local.x * iRot.x - local.y * iRot.y,
                        local.x * iRot.y + local.y * iRot.x);
    vec2 world = iOffset + rotated;
    gl_Position = u_projection * vec4(world, 0.0, 1.0);
    v_uv = mix(iUV.xy, iUV.zw, aUV);
    v_color = iColor;
}
)GLSL";

    const char* kSpriteFS = R"GLSL(
#version 330 core

uniform sampler2D u_atlas;

in vec2 v_uv;
in vec4 v_color;
out vec4 FragColor;

void main() {
    FragColor = texture(u_atlas, v_uv) * v_color;
}
)GLSL";
}

SpriteBatcher::~SpriteBatcher() {
    Destroy();
}

void SpriteBatcher::MoveFrom(SpriteBatcher& o) noexcept {
    vao = o.vao; quadVbo = o.quadVbo; quadEbo = o.quadEbo;
    instVbo = o.instVbo;
    shader = std::move(o.shader);
    o.vao = o.quadVbo = o.quadEbo = o.instVbo = 0;
}

void SpriteBatcher::Destroy() {
    if (vao || quadVbo || quadEbo || instVbo) {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &quadVbo);
        glDeleteBuffers(1, &quadEbo);
        glDeleteBuffers(1, &instVbo);
        vao = quadVbo = quadEbo = instVbo = 0;
    }
    shader.reset();
}

const char* SpriteBatcher::VertexShader() { return kSpriteVS; }
const char* SpriteBatcher::FragmentShader() { return kSpriteFS; }

bool SpriteBatcher::Create() {
    shader = std::make_unique<Shader>();
    if (!shader->LoadFromSource(kSpriteVS, kSpriteFS))
        return false;

    // ---- unit quad：4 頂點（pos2+uv2）+ 6 索引 ----
    // pos 原點左上角（aPos=(0,0) → 精靈左上），uv 同向
    // 左上原點投影下 aUV=(0,0) 對應 uv rect 左上角（v0=頂端）
    const float quad[] = {
        0.f, 0.f,  0.f, 0.f,   // 左上
        1.f, 0.f,  1.f, 0.f,   // 右上
        1.f, 1.f,  1.f, 1.f,   // 右下
        0.f, 1.f,  0.f, 1.f,   // 左下
    };
    const uint32 idx[] = { 0, 1, 2, 0, 2, 3 };

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    glGenBuffers(1, &quadEbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float), (void*)(2 * sizeof(float)));

    // ---- instance VBO ----
    glGenBuffers(1, &instVbo);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo);
    // 預配置，Render() 時 BufferSubData / orphan
    glBufferData(GL_ARRAY_BUFFER,
                 1024 * kInstanceFloats * sizeof(float),
                 nullptr, GL_STREAM_DRAW);

    const int stride = kInstanceFloats * sizeof(float);
    auto attrib = [&](GLuint loc, GLint size, int floatOffset) {
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, size, GL_FLOAT, GL_FALSE, stride,
                              (void*)(floatOffset * sizeof(float)));
        glVertexAttribDivisor(loc, 1); // per-instance
    };
    attrib(2, 2, 0);   // iOffset
    attrib(3, 2, 2);   // iSize
    attrib(4, 4, 4);   // iUV
    attrib(5, 4, 8);   // iColor
    attrib(6, 2, 12);  // iRot

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return vao != 0;
}

void SpriteBatcher::SortByZ(std::vector<SpriteInstance>& instances) {
    std::stable_sort(instances.begin(), instances.end(),
                     [](const SpriteInstance& a, const SpriteInstance& b) {
                         return a.z < b.z;
                     });
}

std::vector<float> SpriteBatcher::PackInstances(
    const std::vector<SpriteInstance>& instances) {
    std::vector<float> packed;
    packed.reserve(instances.size() * kInstanceFloats);
    for (const auto& i : instances) {
        packed.push_back(i.x);
        packed.push_back(i.y);
        packed.push_back(i.w);
        packed.push_back(i.h);
        packed.push_back(i.u0);
        packed.push_back(i.v0);
        packed.push_back(i.u1);
        packed.push_back(i.v1);
        packed.push_back(i.r);
        packed.push_back(i.g);
        packed.push_back(i.b);
        packed.push_back(i.a);
        packed.push_back(i.rotCos);
        packed.push_back(i.rotSin);
    }
    return packed;
}

void SpriteBatcher::Render(const std::vector<SpriteInstance>& instances,
                           uint32 texture,
                           const Matrix4& projection) {
    if (!shader || instances.empty() || vao == 0)
        return;

    std::vector<float> packed = PackInstances(instances);

    shader->Bind();
    shader->SetUniformMat4("u_projection", projection);
    shader->SetUniformInt("u_atlas", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glBindBuffer(GL_ARRAY_BUFFER, instVbo);
    // orphan：丟棄舊內容避免 GL 內部同步等待
    const GLsizeiptr bytes = static_cast<GLsizeiptr>(
        packed.size() * sizeof(float));
    glBufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, packed.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindVertexArray(vao);
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr,
                            static_cast<GLsizei>(instances.size()));
    glBindVertexArray(0);
}

} // namespace Potato
