#pragma once

#include "Core/CoreTypes.h"
#include "Core/Interfaces/IRenderer.h"
#include "MathUtils/Vector2.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Matrix4.h"
#include <string>
#include <vector>
#include <cstdint>

namespace Potato {

/**
 * 窗口配置
 */
struct WindowConfig {
    int width = 1920;
    int height = 1080;
    std::string title = "Potato Engine";
    bool fullscreen = false;
    bool vsync = true;
    bool resizable = true;
    bool decorated = true;
};

/**
 * 渲染器配置
 */
struct RendererConfig {
    WindowConfig window;
    bool enableDebugOutput = false;
    int maxAnisotropy = 16;
    int msaaSamples = 4;
};

/**
 * OpenGL 渲染器
 */
class OpenGLRenderer : public IRenderer {
public:
    OpenGLRenderer();
    ~OpenGLRenderer() override;
    
    // IRenderer 接口實現
    bool Initialize() override;
    void Shutdown() override;
    
    void SetClearColor(const Vector3& color) override;
    void Clear() override;
    void Present() override;
    
    void SetViewport(int x, int y, int width, int height) override;
    void SetScissor(int x, int y, int width, int height) override;
    
    void EnableDepthTest(bool enable) override;
    void EnableStencilTest(bool enable) override;
    void EnableBlending(bool enable) override;
    void EnableCulling(bool enable) override;
    
    void SetBlendMode(BlendMode mode) override;
    void SetCullMode(CullMode mode) override;
    void SetDepthFunc(DepthFunc func) override;
    
    // OpenGL 特定方法
    void SetConfig(const RendererConfig& config);
    const RendererConfig& GetConfig() const { return config; }
    
    bool ShouldClose() const;
    void PollEvents();
    void SwapBuffers();
    
    void* GetWindowHandle() const;
    
private:
    bool InitializeGLFW();
    bool InitializeGLAD();
    void SetupOpenGL();
    void ShutdownGLFW();
    
private:
    RendererConfig config;
    void* windowHandle;
    bool initialized;
    
    static OpenGLRenderer* instance;
};

/**
 * 着色器類
 */
class Shader {
public:
    Shader();
    ~Shader();
    
    bool LoadFromSource(const std::string& vertexSource, const std::string& fragmentSource);
    bool LoadFromFile(const std::string& vertexPath, const std::string& fragmentPath);
    
    void Bind() const;
    void Unbind() const;
    
    void SetUniformInt(const std::string& name, int value);
    void SetUniformFloat(const std::string& name, float value);
    void SetUniformVec2(const std::string& name, const Vector2& value);
    void SetUniformVec3(const std::string& name, const Vector3& value);
    void SetUniformMat4(const std::string& name, const Matrix4& matrix);
    
    uint32 GetProgramID() const { return programID; }
    
private:
    uint32 programID;
    bool CompileShader(uint32 type, const std::string& source, uint32& shaderID);
    bool LinkProgram(uint32 vertexID, uint32 fragmentID);
};

/**
 * 頂點數組對象
 */
class VertexArray {
public:
    VertexArray();
    ~VertexArray();
    
    // GL 物件所有權唯一:禁止拷貝(拷貝會讓 dtor 刪掉共享的 VAO/VBO),
    // 提供 move 語義轉移 id
    VertexArray(const VertexArray&) = delete;
    VertexArray& operator=(const VertexArray&) = delete;
    VertexArray(VertexArray&& o) noexcept
        : buffersUploaded(o.buffersUploaded),
          pendingVertexData(std::move(o.pendingVertexData)),
          pendingIndexData(std::move(o.pendingIndexData)),
          pendingAttribs(std::move(o.pendingAttribs)),
          pendingVertexUsage(o.pendingVertexUsage),
          pendingIndexUsage(o.pendingIndexUsage),
          vao(o.vao), vbo(o.vbo), ebo(o.ebo),
          vertexCount(o.vertexCount), indexCount(o.indexCount) {
        o.vao = o.vbo = o.ebo = 0;
        o.vertexCount = o.indexCount = 0;
        o.buffersUploaded = false;
    }
    VertexArray& operator=(VertexArray&& o) noexcept {
        if (this != &o) {
            ReleaseGLObjects();
            vao = o.vao; vbo = o.vbo; ebo = o.ebo;
            vertexCount = o.vertexCount; indexCount = o.indexCount;
            buffersUploaded = o.buffersUploaded;
            pendingVertexData = std::move(o.pendingVertexData);
            pendingIndexData = std::move(o.pendingIndexData);
            pendingAttribs = std::move(o.pendingAttribs);
            pendingVertexUsage = o.pendingVertexUsage;
            pendingIndexUsage = o.pendingIndexUsage;
            o.vao = o.vbo = o.ebo = 0;
            o.vertexCount = o.indexCount = 0;
            o.buffersUploaded = false;
        }
        return *this;
    }
    
    void Bind() const;
    void Unbind() const;
    
    void AddVertexBuffer(const void* data, size_t size, uint32 usage);
    void AddIndexBuffer(const uint32* indices, size_t count, uint32 usage);
    
    void SetVertexAttribute(uint32 index, int size, int stride, size_t offset);
    
    // 唯讀取得 GL id——不觸發建立/重放（避免 getter 更動 GL 綁定狀態）；
    // 需要「確保已建立」請走 Bind()/IsUploaded()
    uint32 GetVAO() const { return vao; }
    uint32 GetVBO() const { return vbo; }
    uint32 GetEBO() const { return ebo; }
    // headless 建構後是否已真正把資料上傳到 GPU
    bool IsUploaded() const { return buffersUploaded; }
    
private:
    // 惰性建立 GL 物件：GL context 可能尚未初始化就建構 Mesh/Model；
    // headless 期間收到的 buffer/attribute 先暫存，EnsureCreated 時重放上傳
    void EnsureCreated() const;
    // 只釋放 GL 物件 id（move-assign 用）；不動暫存資料與成員生命週期
    void ReleaseGLObjects();

    struct PendingAttrib {
        uint32 index;
        int size;
        int stride;
        size_t offset;
    };
    mutable bool buffersUploaded = false;
    mutable std::vector<unsigned char> pendingVertexData;
    mutable std::vector<uint32> pendingIndexData;
    mutable std::vector<PendingAttrib> pendingAttribs;
    mutable uint32 pendingVertexUsage = 0;
    mutable uint32 pendingIndexUsage = 0;

    mutable uint32 vao;
    mutable uint32 vbo;
    mutable uint32 ebo;
    uint32 vertexCount;
    uint32 indexCount;
};

/**
 * 頂點結構
 */
struct Vertex {
    Vector3 position;
    Vector3 normal;
    Vector2 texCoord;
    Vector3 tangent;
    Vector3 bitangent;
};

/**
 * 網格類
 */
class Mesh {
public:
    Mesh();
    ~Mesh();
    
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) = default;
    Mesh& operator=(Mesh&&) = default;
    
    void SetVertices(const std::vector<Vertex>& vertices);
    void SetIndices(const std::vector<uint32>& indices);
    
    void Bind() const;
    void Unbind() const;
    
    void Draw() const;
    void DrawInstanced(int instanceCount) const;
    
    const VertexArray& GetVertexArray() const { return vertexArray; }
    
private:
    VertexArray vertexArray;
    std::vector<Vertex> vertices;
    std::vector<uint32> indices;
};

/**
 * 紋理類
 */
class Texture {
public:
    Texture();
    ~Texture();

    // GL id 所有權唯一：禁止拷貝（避免共享 id 被二次 glDeleteTextures）
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& o) noexcept
        : textureID(o.textureID), width(o.width), height(o.height),
          channels(o.channels),
          pendingPixels(std::move(o.pendingPixels)),
          pendingPath(std::move(o.pendingPath)) {
        o.textureID = 0; o.width = o.height = o.channels = 0;
    }
    Texture& operator=(Texture&& o) noexcept {
        if (this != &o) {
            this->~Texture();
            textureID = o.textureID; width = o.width;
            height = o.height; channels = o.channels;
            pendingPixels = std::move(o.pendingPixels);
            pendingPath = std::move(o.pendingPath);
            o.textureID = 0; o.width = o.height = o.channels = 0;
        }
        return *this;
    }

    bool LoadFromFile(const std::string& path);
    bool LoadFromMemory(const unsigned char* data, int width, int height, int channels);
    
    void Bind(uint32 unit = 0) const;
    void Unbind() const;
    
    void SetWrapMode(WrapMode mode);
    void SetFilterMode(FilterMode mode);
    
    uint32 GetTextureID() const { EnsureUploaded(); return textureID; }
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }

private:
    // GL context 未就緒時暫存像素，首次 Bind 才上傳
    void EnsureUploaded() const;

    mutable uint32 textureID;
    int width;
    int height;
    int channels;
    mutable std::vector<unsigned char> pendingPixels;
    mutable std::string pendingPath; // headless LoadFromFile 暫存路徑
};

} // namespace Potato