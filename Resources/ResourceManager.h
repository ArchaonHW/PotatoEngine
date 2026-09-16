#pragma once

#include "Core/CoreTypes.h"
#include "Core/Interfaces/IResourceManager.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <mutex>

namespace Potato {

// 前向聲明資源類型
class Texture;
class Mesh;
class Shader;
class AudioBuffer;
class Material;

/**
 * 資源類型枚舉
 */
enum class ResourceType {
    Texture,
    Mesh,
    Shader,
    Audio,
    Material,
    Scene,
    Animation,
    Particle,
    Unknown
};

/**
 * 資源加載器接口
 */
class IResourceLoader {
public:
    virtual ~IResourceLoader() = default;
    
    virtual bool Load(const std::string& path, void** resource) = 0;
    virtual void Unload(void* resource) = 0;
    virtual ResourceType GetType() const = 0;
};

/**
 * 資源元數據
 */
struct ResourceMetadata {
    std::string name;
    std::string path;
    ResourceType type;
    size_t size;
    uint64 lastModified;
    uint32 referenceCount;
    bool persistent;
    
    ResourceMetadata()
        : type(ResourceType::Unknown)
        , size(0)
        , lastModified(0)
        , referenceCount(0)
        , persistent(false)
    {}
};

/**
 * 資源句柄
 * 用於安全地訪問資源
 */
class ResourceCacheHandle {
public:
    ResourceCacheHandle();
    explicit ResourceCacheHandle(uint32 handle);
    
    uint32 GetHandle() const { return handle; }
    bool IsValid() const { return handle != 0; }
    
private:
    uint32 handle;
};

/**
 * 資源緩存
 * 管理資源的加載、緩存和釋放
 */
class ResourceCache {
public:
    ResourceCache();
    ~ResourceCache();
    
    // 資源加載
    template<typename T>
    SharedPtr<T> Load(const std::string& path);
    
    void* LoadRaw(const std::string& path, ResourceType type);
    void Unload(const std::string& path);
    void UnloadAll();
    
    // 資源註冊
    void RegisterLoader(ResourceType type, SharedPtr<IResourceLoader> loader);
    
    // 資源查詢
    bool IsLoaded(const std::string& path) const;
    ResourceMetadata GetMetadata(const std::string& path) const;
    size_t GetLoadedResourceCount() const;
    size_t GetTotalMemoryUsage() const;
    
    // 資源管理
    void SetMaxMemory(size_t maxMemory);
    void EnableAutoUnload(bool enable);
    void SetAutoUnloadThreshold(float threshold);
    
    // 資源統計
    void PrintStatistics() const;
    
private:
    void LoadResource(const std::string& path);
    void UnloadResource(const std::string& path);
    void CheckMemoryUsage();
    bool ShouldUnload(const ResourceMetadata& metadata) const;
    
private:
    std::unordered_map<std::string, void*> resources;
    std::unordered_map<std::string, ResourceMetadata> metadata;
    std::unordered_map<ResourceType, SharedPtr<IResourceLoader>> loaders;
    
    size_t maxMemory;
    size_t currentMemoryUsage;
    bool autoUnloadEnabled;
    float autoUnloadThreshold;
    
    mutable std::mutex mutex;
};

/**
 * 資源管理器實現
 */
class ResourceManager : public IResourceManager {
public:
    ResourceManager();
    ~ResourceManager();
    
    // IResourceManager 接口實現
    bool Initialize() override;
    void Shutdown() override;
    
    // 紋理管理
    SharedPtr<Texture> LoadTexture(const std::string& path) override;
    void UnloadTexture(const std::string& path) override;
    SharedPtr<Texture> GetTexture(const std::string& path) override;
    
    SharedPtr<Mesh> LoadMesh(const std::string& path) override;
    void UnloadMesh(const std::string& path) override;
    SharedPtr<Mesh> GetMesh(const std::string& path) override;
    
    SharedPtr<Shader> LoadShader(const std::string& vertexPath, const std::string& fragmentPath) override;
    void UnloadShader(const std::string& name) override;
    SharedPtr<Shader> GetShader(const std::string& name) override;
    
    SharedPtr<AudioBuffer> LoadAudio(const std::string& path) override;
    void UnloadAudio(const std::string& path) override;
    SharedPtr<AudioBuffer> GetAudio(const std::string& path) override;
    
    // 資源清理
    void UnloadUnusedResources() override;
    void UnloadAllResources() override;
    
    // 資源統計
    size_t GetMemoryUsage() const override;
    int GetLoadedResourceCount() const override;
    
    // 資源路徑管理
    void AddResourcePath(const std::string& path);
    void RemoveResourcePath(const std::string& path);
    std::string ResolveResourcePath(const std::string& path) const;
    
    // 異步加載
    void EnableAsyncLoading(bool enable);
    bool IsAsyncLoadingEnabled() const;
    
private:
    std::string ResolvePath(const std::string& path) const;
    
private:
    UniquePtr<ResourceCache> cache;
    std::vector<std::string> resourcePaths;
    bool asyncLoadingEnabled;
    bool initialized;
};

// 全局資源管理器
extern ResourceManager* gResourceManager;

/**
 * 初始化全局資源管理器
 */
bool InitializeResourceManager();

/**
 * 關閉全局資源管理器
 */
void ShutdownResourceManager();

/**
 * 獲取全局資源管理器
 */
ResourceManager* GetResourceManager();

} // namespace Potato

// 便捷宏
#define GET_RESOURCE_MANAGER() Potato::GetResourceManager()