#include "OpenGLRenderer.h"
#include "Logging/Logger.h"
#include "Platform/GLFWSharedContext.h"
#include <iostream>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

namespace Potato {

OpenGLRenderer* OpenGLRenderer::instance = nullptr;

// ============================================================================
// OpenGLRenderer 實現
// ============================================================================

OpenGLRenderer::OpenGLRenderer()
    : windowHandle(nullptr)
    , initialized(false)
{
    instance = this;
}

OpenGLRenderer::~OpenGLRenderer() {
    Shutdown();
}

bool OpenGLRenderer::Initialize() {
    if (initialized) {
        return true;
    }
    
    LOG_INFO("Initializing OpenGL Renderer...");
    
    // 初始化 GLFW
    if (!InitializeGLFW()) {
        LOG_ERROR("Failed to initialize GLFW");
        return false;
    }
    
    // 創建窗口
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    GLFWmonitor* monitor = config.window.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    windowHandle = glfwCreateWindow(
        config.window.width,
        config.window.height,
        config.window.title.c_str(),
        monitor,
        nullptr
    );
    
    if (!windowHandle) {
        LOG_ERROR("Failed to create GLFW window");
        ShutdownGLFW();
        return false;
    }
    
    glfwMakeContextCurrent(static_cast<GLFWwindow*>(windowHandle));
    
    // 設置 VSync
    if (config.window.vsync) {
        glfwSwapInterval(1);
    }
    
    // 初始化 GLAD
    if (!InitializeGLAD()) {
        LOG_ERROR("Failed to initialize GLAD");
        ShutdownGLFW();
        return false;
    }
    
    // 設置 OpenGL
    SetupOpenGL();
    
    initialized = true;
    LOG_INFO("OpenGL Renderer initialized successfully");
    
    return true;
}

void OpenGLRenderer::Shutdown() {
    if (!initialized) {
        return;
    }
    
    LOG_INFO("Shutting down OpenGL Renderer...");
    ShutdownGLFW();
    
    initialized = false;
    LOG_INFO("OpenGL Renderer shutdown complete");
}

void OpenGLRenderer::SetClearColor(const Vector3& color) {
    glClearColor(color.x, color.y, color.z, 1.0f);
}

void OpenGLRenderer::Clear() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void OpenGLRenderer::Present() {
    SwapBuffers();
}

void OpenGLRenderer::SetViewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

void OpenGLRenderer::SetScissor(int x, int y, int width, int height) {
    glScissor(x, y, width, height);
}

void OpenGLRenderer::EnableDepthTest(bool enable) {
    if (enable) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

void OpenGLRenderer::EnableStencilTest(bool enable) {
    if (enable) {
        glEnable(GL_STENCIL_TEST);
    } else {
        glDisable(GL_STENCIL_TEST);
    }
}

void OpenGLRenderer::EnableBlending(bool enable) {
    if (enable) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
}

void OpenGLRenderer::EnableCulling(bool enable) {
    if (enable) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
}

void OpenGLRenderer::SetBlendMode(BlendMode mode) {
    switch (mode) {
        case BlendMode::None:
            glDisable(GL_BLEND);
            break;
        case BlendMode::Alpha:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Additive:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case BlendMode::Multiply:
            glEnable(GL_BLEND);
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
            break;
    }
}

void OpenGLRenderer::SetCullMode(CullMode mode) {
    switch (mode) {
        case CullMode::None:
            glDisable(GL_CULL_FACE);
            break;
        case CullMode::Front:
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            break;
        case CullMode::Back:
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            break;
    }
}

void OpenGLRenderer::SetDepthFunc(DepthFunc func) {
    switch (func) {
        case DepthFunc::Never:
            glDepthFunc(GL_NEVER);
            break;
        case DepthFunc::Less:
            glDepthFunc(GL_LESS);
            break;
        case DepthFunc::Equal:
            glDepthFunc(GL_EQUAL);
            break;
        case DepthFunc::LessEqual:
            glDepthFunc(GL_LEQUAL);
            break;
        case DepthFunc::Greater:
            glDepthFunc(GL_GREATER);
            break;
        case DepthFunc::NotEqual:
            glDepthFunc(GL_NOTEQUAL);
            break;
        case DepthFunc::GreaterEqual:
            glDepthFunc(GL_GEQUAL);
            break;
        case DepthFunc::Always:
            glDepthFunc(GL_ALWAYS);
            break;
    }
}

void OpenGLRenderer::SetConfig(const RendererConfig& cfg) {
    config = cfg;
}

bool OpenGLRenderer::ShouldClose() const {
    if (!windowHandle) {
        return true; // 無窗口視為應關閉,避免 glfwWindowShouldClose(nullptr) UB
    }
    return glfwWindowShouldClose(static_cast<GLFWwindow*>(windowHandle));
}

void OpenGLRenderer::PollEvents() {
    glfwPollEvents();
}

void OpenGLRenderer::SwapBuffers() {
    glfwSwapBuffers(static_cast<GLFWwindow*>(windowHandle));
}

void* OpenGLRenderer::GetWindowHandle() const {
    return windowHandle;
}

bool OpenGLRenderer::InitializeGLFW() {
    if (!glfwInit()) {
        LOG_ERROR("GLFW initialization failed");
        return false;
    }
    
    LOG_INFO("GLFW initialized successfully");
    return true;
}

bool OpenGLRenderer::InitializeGLAD() {
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        LOG_ERROR("GLAD initialization failed");
        return false;
    }
    
    LOG_INFO("GLAD initialized successfully");
    LOG_INFO("OpenGL Version: " + std::string(reinterpret_cast<const char*>(glGetString(GL_VERSION))));
    LOG_INFO("GLSL Version: " + std::string(reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION))));
    
    return true;
}

void OpenGLRenderer::SetupOpenGL() {
    // 設置默認狀態
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    
    // 設置清除顏色
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    
    LOG_INFO("OpenGL setup complete");
}

void OpenGLRenderer::ShutdownGLFW() {
    if (windowHandle) {
        // 走 DestroyGLFWWindow 釋放共享 context,避免 context map 殘留 stale entry
        DestroyGLFWWindow(static_cast<GLFWwindow*>(windowHandle));
        windowHandle = nullptr;
    }
    
    glfwTerminate();
    LOG_INFO("GLFW terminated");
}

// ============================================================================
// Shader 實現
// ============================================================================

Shader::Shader()
    : programID(0)
{
}

Shader::~Shader() {
    if (programID) {
        glDeleteProgram(programID);
    }
}

bool Shader::LoadFromSource(const std::string& vertexSource, const std::string& fragmentSource) {
    uint32 vertexID, fragmentID;
    
    // 編譯頂點著色器
    if (!CompileShader(GL_VERTEX_SHADER, vertexSource, vertexID)) {
        return false;
    }
    
    // 編譯片段著色器
    if (!CompileShader(GL_FRAGMENT_SHADER, fragmentSource, fragmentID)) {
        glDeleteShader(vertexID);
        return false;
    }
    
    // 鏈接程序
    if (!LinkProgram(vertexID, fragmentID)) {
        glDeleteShader(vertexID);
        glDeleteShader(fragmentID);
        return false;
    }
    
    // 清理著色器對象
    glDeleteShader(vertexID);
    glDeleteShader(fragmentID);
    
    return true;
}

bool Shader::LoadFromFile(const std::string& vertexPath, const std::string& fragmentPath) {
    // 簡化實現：實際應該讀取文件內容
    std::cout << "Loading shader from files: " << vertexPath << ", " << fragmentPath << std::endl;
    return true;
}

void Shader::Bind() const {
    glUseProgram(programID);
}

void Shader::Unbind() const {
    glUseProgram(0);
}

void Shader::SetUniformInt(const std::string& name, int value) {
    glUniform1i(glGetUniformLocation(programID, name.c_str()), value);
}

void Shader::SetUniformFloat(const std::string& name, float value) {
    glUniform1f(glGetUniformLocation(programID, name.c_str()), value);
}

void Shader::SetUniformVec2(const std::string& name, const Vector2& value) {
    glUniform2f(glGetUniformLocation(programID, name.c_str()), value.x, value.y);
}

void Shader::SetUniformVec3(const std::string& name, const Vector3& value) {
    glUniform3f(glGetUniformLocation(programID, name.c_str()), value.x, value.y, value.z);
}

void Shader::SetUniformMat4(const std::string& name, const Matrix4& matrix) {
    glUniformMatrix4fv(glGetUniformLocation(programID, name.c_str()), 1, GL_FALSE, matrix.m);
}

bool Shader::CompileShader(uint32 type, const std::string& source, uint32& shaderID) {
    shaderID = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shaderID, 1, &src, nullptr);
    glCompileShader(shaderID);
    
    // 檢查編譯錯誤
    int success;
    glGetShaderiv(shaderID, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shaderID, 512, nullptr, infoLog);
        LOG_ERROR("Shader compilation failed: " + std::string(infoLog));
        return false;
    }
    
    return true;
}

bool Shader::LinkProgram(uint32 vertexID, uint32 fragmentID) {
    programID = glCreateProgram();
    glAttachShader(programID, vertexID);
    glAttachShader(programID, fragmentID);
    glLinkProgram(programID);
    
    // 檢查鏈接錯誤
    int success;
    glGetProgramiv(programID, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(programID, 512, nullptr, infoLog);
        LOG_ERROR("Shader program linking failed: " + std::string(infoLog));
        return false;
    }
    
    return true;
}

// ============================================================================
// VertexArray 實現
// ============================================================================

VertexArray::VertexArray()
    : vao(0)
    , vbo(0)
    , ebo(0)
    , vertexCount(0)
    , indexCount(0)
{
    // GL 物件延遲到首次使用時才建立（EnsureCreated），
    // 允許在 GL context 建立前先建構 Mesh/Model
}

VertexArray::~VertexArray() {
    ReleaseGLObjects();
}

void VertexArray::ReleaseGLObjects() {
    if (glDeleteBuffers == nullptr) return; // GL 未初始化
    if (ebo) glDeleteBuffers(1, &ebo);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    vao = vbo = ebo = 0;
    buffersUploaded = false;
}

void VertexArray::EnsureCreated() const {
    // glad 函式指標未載入（無 GL context）時直接返回——
    // 允許 headless 建構 Mesh/Model，繪製時由 Bind 再補建
    if (glGenVertexArrays == nullptr || glBindVertexArray == nullptr ||
        glBufferData == nullptr) {
        return;
    }
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
    }
    if (vbo == 0) {
        glGenBuffers(1, &vbo);
    }
    if (ebo == 0) {
        glGenBuffers(1, &ebo);
    }
    // 重放 headless 期間暫存的 buffer/attribute（任何一類 pending 都要重放）
    if (!buffersUploaded && vao != 0 &&
        (!pendingVertexData.empty() || !pendingIndexData.empty() ||
         !pendingAttribs.empty())) {
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, pendingVertexData.size(),
                     pendingVertexData.data(), pendingVertexUsage);
        if (!pendingIndexData.empty()) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         pendingIndexData.size() * sizeof(uint32),
                         pendingIndexData.data(), pendingIndexUsage);
        }
        for (const auto& a : pendingAttribs) {
            glVertexAttribPointer(a.index, a.size, GL_FLOAT, GL_FALSE,
                                  a.stride, reinterpret_cast<void*>(a.offset));
            glEnableVertexAttribArray(a.index);
        }
        glBindVertexArray(0);
        buffersUploaded = true;
        pendingVertexData.clear();
        pendingIndexData.clear();
        pendingAttribs.clear();
    }
}

void VertexArray::Bind() const {
    EnsureCreated();
    if (vao == 0 || !buffersUploaded) return;
    glBindVertexArray(vao);
}

void VertexArray::Unbind() const {
    if (glBindVertexArray == nullptr) return;
    glBindVertexArray(0);
}

void VertexArray::AddVertexBuffer(const void* data, size_t size, uint32 usage) {
    EnsureCreated();
    if (vao == 0) {
        // 無 GL context：暫存，等 EnsureCreated 重放；空資料不暫存
        if (data != nullptr && size > 0) {
            const auto* bytes = static_cast<const unsigned char*>(data);
            pendingVertexData.assign(bytes, bytes + size);
            pendingVertexUsage = usage;
        } else {
            pendingVertexData.clear();
        }
        buffersUploaded = false;
        return;
    }
    if (data == nullptr || size == 0) return;
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, size, data, usage);
    buffersUploaded = true;
    glBindVertexArray(0);
}

void VertexArray::AddIndexBuffer(const uint32* indices, size_t count, uint32 usage) {
    EnsureCreated();
    indexCount = static_cast<uint32>(count);
    if (vao == 0) {
        if (indices != nullptr && count > 0) {
            pendingIndexData.assign(indices, indices + count);
            pendingIndexUsage = usage;
        } else {
            pendingIndexData.clear();
        }
        return;
    }
    if (indices == nullptr || count == 0) return;
    glBindVertexArray(vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32), indices, usage);
    glBindVertexArray(0);
}

void VertexArray::SetVertexAttribute(uint32 index, int size, int stride, size_t offset) {
    EnsureCreated();
    if (vao == 0) {
        pendingAttribs.push_back({index, size, stride, offset});
        return;
    }
    glBindVertexArray(vao);
    glVertexAttribPointer(index, size, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offset));
    glEnableVertexAttribArray(index);
    glBindVertexArray(0);
}

// ============================================================================
// Mesh 實現
// ============================================================================

Mesh::Mesh() {
}

Mesh::~Mesh() {
}

void Mesh::SetVertices(const std::vector<Vertex>& verts) {
    vertices = verts;
    vertexArray.AddVertexBuffer(vertices.data(), vertices.size() * sizeof(Vertex), GL_STATIC_DRAW);

    // Vertex 標準屬性佈局: position(3), normal(3), texCoord(2), tangent(3), bitangent(3)
    const size_t stride = sizeof(Vertex);
    vertexArray.SetVertexAttribute(0, 3, stride, offsetof(Vertex, position));
    vertexArray.SetVertexAttribute(1, 3, stride, offsetof(Vertex, normal));
    vertexArray.SetVertexAttribute(2, 2, stride, offsetof(Vertex, texCoord));
    vertexArray.SetVertexAttribute(3, 3, stride, offsetof(Vertex, tangent));
    vertexArray.SetVertexAttribute(4, 3, stride, offsetof(Vertex, bitangent));
}

void Mesh::SetIndices(const std::vector<uint32>& inds) {
    indices = inds;
    vertexArray.AddIndexBuffer(indices.data(), indices.size(), GL_STATIC_DRAW);
}

void Mesh::Bind() const {
    vertexArray.Bind();
}

void Mesh::Unbind() const {
    vertexArray.Unbind();
}

void Mesh::Draw() const {
    if (indices.empty() && vertices.empty()) return;
    vertexArray.Bind();
    if (!vertexArray.IsUploaded()) return; // headless/未上傳：不繪製（避免畫到外部 VAO）
    if (!indices.empty()) {
        glDrawElements(GL_TRIANGLES, static_cast<int>(indices.size()), GL_UNSIGNED_INT, 0);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<int>(vertices.size()));
    }
    vertexArray.Unbind();
}

void Mesh::DrawInstanced(int instanceCount) const {
    if (instanceCount <= 0 || (indices.empty() && vertices.empty())) return;
    vertexArray.Bind();
    if (vertexArray.GetVAO() == 0) return;
    if (!indices.empty()) {
        glDrawElementsInstanced(GL_TRIANGLES, static_cast<int>(indices.size()), GL_UNSIGNED_INT, 0, instanceCount);
    } else {
        glDrawArraysInstanced(GL_TRIANGLES, 0, static_cast<int>(vertices.size()), instanceCount);
    }
    vertexArray.Unbind();
}

// ============================================================================
// Texture 實現
// ============================================================================

Texture::Texture()
    : textureID(0)
    , width(0)
    , height(0)
    , channels(0)
{
}

Texture::~Texture() {
    if (textureID && glDeleteTextures != nullptr) {
        glDeleteTextures(1, &textureID);
    }
}

bool Texture::LoadFromFile(const std::string& path) {
    // 簡化實現：實際應該使用 stb_image 加載
    std::cout << "Loading texture from file: " << path << std::endl;

    if (glGenTextures == nullptr) {
        // headless：無 GL context，只記錄尺寸（1x1 佔位）
        width = height = 1;
        channels = 3;
        return true;
    }

    // 創建紋理
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    // 設置紋理參數
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // 創建默認紋理數據
    unsigned char data[] = {255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    
    return true;
}

bool Texture::LoadFromMemory(const unsigned char* data, int w, int h, int ch) {
    width = w;
    height = h;
    channels = ch;

    if (glGenTextures == nullptr) {
        // headless：暫存像素，首次 Bind 時補上傳
        const size_t sz = static_cast<size_t>(w) * h * ch;
        pendingPixels.assign(data, data + sz);
        return true;
    }

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    GLenum format = GL_RGB;
    if (channels == 4) format = GL_RGBA;
    else if (channels == 1) format = GL_RED;

    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    return true;
}

void Texture::EnsureUploaded() const {
    if (pendingPixels.empty() || glGenTextures == nullptr) return;
    // 補上傳：複用 LoadFromMemory 的寫法（會清掉 pendingPixels）
    std::vector<unsigned char> pixels;
    pixels.swap(pendingPixels);
    const_cast<Texture*>(this)->LoadFromMemory(pixels.data(), width, height,
                                             channels);
}

void Texture::Bind(uint32 unit) const {
    EnsureUploaded();
    if (textureID == 0 || glActiveTexture == nullptr) return;
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, textureID);
}

void Texture::Unbind() const {
    if (glBindTexture == nullptr) return;
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::SetWrapMode(WrapMode mode) {
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    GLenum wrap = GL_REPEAT;
    switch (mode) {
        case WrapMode::Repeat: wrap = GL_REPEAT; break;
        case WrapMode::Clamp: wrap = GL_CLAMP_TO_EDGE; break;
        case WrapMode::Mirror: wrap = GL_MIRRORED_REPEAT; break;
    }
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
}

void Texture::SetFilterMode(FilterMode mode) {
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    GLenum minFilter = GL_LINEAR;
    GLenum magFilter = GL_LINEAR;
    
    switch (mode) {
        case FilterMode::Nearest:
            minFilter = GL_NEAREST;
            magFilter = GL_NEAREST;
            break;
        case FilterMode::Linear:
            minFilter = GL_LINEAR;
            magFilter = GL_LINEAR;
            break;
        case FilterMode::Mipmap:
            minFilter = GL_LINEAR_MIPMAP_LINEAR;
            magFilter = GL_LINEAR;
            break;
    }
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
}

} // namespace Potato