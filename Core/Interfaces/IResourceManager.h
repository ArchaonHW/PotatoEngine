#pragma once

#include <string>
#include <memory>

namespace Potato {

// 前向聲明資源類型
class Texture;
class Mesh;
class Shader;
class AudioBuffer;

/**
 * 資源管理器接口
 * 定義資源加載和管理的基本功能
 */
class IResourceManager {
public:
    virtual ~IResourceManager() = default;
    
    // 初始化和關閉
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    
    // 紋理管理
    virtual std::shared_ptr<Texture> LoadTexture(const std::string& path) = 0;
    virtual void UnloadTexture(const std::string& path) = 0;
    virtual std::shared_ptr<Texture> GetTexture(const std::string& path) = 0;
    
    // 模型管理
    virtual std::shared_ptr<Mesh> LoadMesh(const std::string& path) = 0;
    virtual void UnloadMesh(const std::string& path) = 0;
    virtual std::shared_ptr<Mesh> GetMesh(const std::string& path) = 0;
    
    // 著色器管理
    virtual std::shared_ptr<Shader> LoadShader(const std::string& vertexPath, 
                                               const std::string& fragmentPath) = 0;
    virtual void UnloadShader(const std::string& name) = 0;
    virtual std::shared_ptr<Shader> GetShader(const std::string& name) = 0;
    
    // 音頻資源管理
    virtual std::shared_ptr<AudioBuffer> LoadAudio(const std::string& path) = 0;
    virtual void UnloadAudio(const std::string& path) = 0;
    virtual std::shared_ptr<AudioBuffer> GetAudio(const std::string& path) = 0;
    
    // 資源清理
    virtual void UnloadUnusedResources() = 0;
    virtual void UnloadAllResources() = 0;
    
    // 資源統計
    virtual size_t GetMemoryUsage() const = 0;
    virtual int GetLoadedResourceCount() const = 0;
};

} // namespace Potato