#pragma once

#include "Core/CoreTypes.h"
#include "Core/Interfaces/IResourceManager.h"
#include "Security/ContentScanner.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <mutex>
#include <vector>

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
 * 資源載入內容掃描策略
 *
 * 判定處置（Security::ContentScanner 結果）：
 * - Malicious  → 一律拒載（LoadX/GetX 回 nullptr）
 * - Suspicious → 預設放行但觸發回呼；SetScanBlockSuspicious(true) 改為拒載
 * - Clean/Error → 放行（Error 表示檔案不存在等，由 loader 自行回報失敗）
 */
enum class ResourceScanPolicy {
    Off,            // 不掃描（預設,向後相容：引擎自有資產視為信任）
    UntrustedOnly,  // 只掃描 AddUntrustedPath 目錄下的檔案（mod/下載區）
    All             // 掃描所有載入檔案
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

    // ---- 內容掃毒（Security::ContentScanner 整合）----
    // 掃描在路徑解析之後、loader 載入之前執行；掃描開銷只落在
    // LoadX/GetX 的檔案載入路徑上。直接呼叫 GetCache().LoadRaw()
    // 屬於底層 API,會繞過掃描——不授信內容請走 LoadX/GetX。
    void SetContentScanPolicy(ResourceScanPolicy policy);
    ResourceScanPolicy GetContentScanPolicy() const { return scanPolicy; }
    // 註記不授信來源目錄（模組/玩家上傳/下載區）,供 UntrustedOnly 策略比對
    void AddUntrustedPath(const std::string& dirPath);
    // Suspicious 判定也拒載（預設 false：放行 + 回呼,誤報由遊戲層複查）
    void SetScanBlockSuspicious(bool on) { blockSuspicious = on; }
    // 每個實際執行掃描的檔案觸發一次（含 Clean；用於稽核/上報）
    void SetResourceScanCallback(Security::ScanCallback cb);
    // 存取內部掃描器——供遊戲層加黑名單雜湊/位元組特徵/載入特徵庫
    Security::ContentScanner& GetContentScanner() { return scanner; }
    // 存取底層快取（註冊 loader 用）；注意 LoadRaw 繞過內容掃描
    ResourceCache& GetCache() { return *cache; }

private:
    std::string ResolvePath(const std::string& path) const;
    // 掃描閘門：依策略決定是否掃描,回傳 false 表示拒載
    bool PassesContentScan(const std::string& resolvedPath);
    bool IsUntrustedPath(const std::string& resolvedPath) const;
    void* LoadRawScanned(const std::string& resolvedPath, ResourceType type);

private:
    UniquePtr<ResourceCache> cache;
    std::vector<std::string> resourcePaths;
    bool asyncLoadingEnabled;
    bool initialized;

    // ---- 內容掃毒 ----
    Security::ContentScanner scanner;
    ResourceScanPolicy scanPolicy = ResourceScanPolicy::Off;
    bool blockSuspicious = false;
    std::vector<std::string> untrustedDirs;  // 正規化:小寫、'/' 分隔、無尾分隔符
    Security::ScanCallback scanCallback;
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