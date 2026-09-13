#include "OpenGLRenderer.h"
#include "Logging/Logger.h"
#include <iostream>
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
        glfwDestroyWindow(static_cast<GLFWwindow*>(windowHandle));
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
    glUniformMatrix4fv(glGetUniformLocation(programID, name.c_str()), 1, GL_FALSE, matrix.data);
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
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
}

VertexArray::~VertexArray() {
    if (ebo) glDeleteBuffers(1, &ebo);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
}

void VertexArray::Bind() const {
    glBindVertexArray(vao);
}

void VertexArray::Unbind() const {
    glBindVertexArray(0);
}

void VertexArray::AddVertexBuffer(const void* data, size_t size, uint32 usage) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, size, data, usage);
    glBindVertexArray(0);
}

void VertexArray::AddIndexBuffer(const uint32* indices, size_t count, uint32 usage) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32), indices, usage);
    indexCount = static_cast<uint32>(count);
    glBindVertexArray(0);
}

void VertexArray::SetVertexAttribute(uint32 index, int size, int stride, size_t offset) {
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
    vertexArray.Bind();
    glDrawElements(GL_TRIANGLES, static_cast<int>(indices.size()), GL_UNSIGNED_INT, 0);
    vertexArray.Unbind();
}

void Mesh::DrawInstanced(int instanceCount) const {
    vertexArray.Bind();
    glDrawElementsInstanced(GL_TRIANGLES, static_cast<int>(indices.size()), GL_UNSIGNED_INT, 0, instanceCount);
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
    if (textureID) {
        glDeleteTextures(1, &textureID);
    }
}

bool Texture::LoadFromFile(const std::string& path) {
    // 簡化實現：實際應該使用 stb_image 加載
    std::cout << "Loading texture from file: " << path << std::endl;
    
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

void Texture::Bind(uint32 unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, textureID);
}

void Texture::Unbind() const {
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