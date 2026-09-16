#include "ResourceManager.h"
#include "Rendering/OpenGLRenderer.h" // Texture / Mesh / Shader 定義
#include "Audio/AudioSystem.h"       // AudioBuffer 定義
#include <iostream>
#include <algorithm>
#include <filesystem>

// 注意：LoadX/GetX 回傳的 SharedPtr 使用 no-op deleter —
// 資源生命週期由 ResourceCache 擁有，shared_ptr 僅作為非擁有性參照，
// 避免 shared_ptr 析構與 loader->Unload 雙重釋放。

namespace Potato {

// 全局資源管理器
ResourceManager* gResourceManager = nullptr;

// ============================================================================
// ResourceHandle 實現
// ============================================================================

ResourceCacheHandle::ResourceCacheHandle()
    : handle(0)
{
}

ResourceCacheHandle::ResourceCacheHandle(uint32 handle)
    : handle(handle)
{
}

// ============================================================================
// ResourceCache 實現
// ============================================================================

ResourceCache::ResourceCache()
    : maxMemory(512 * 1024 * 1024) // 512MB default
    , currentMemoryUsage(0)
    , autoUnloadEnabled(true)
    , autoUnloadThreshold(0.8f) // 80% threshold
{
}

ResourceCache::~ResourceCache() {
    UnloadAll();
}

void* ResourceCache::LoadRaw(const std::string& path, ResourceType type) {
    std::lock_guard<std::mutex> lock(mutex);
    
    // 檢查是否已加載（直接查表：IsLoaded 會重入同一 mutex 造成死結）
    auto existing = resources.find(path);
    if (existing != resources.end()) {
        ResourceMetadata& meta = metadata[path];
        if (meta.referenceCount < UINT32_MAX) {
            meta.referenceCount++;
        }
        return existing->second;
    }
    
    // 檢查加載器
    auto it = loaders.find(type);
    if (it == loaders.end()) {
        std::cerr << "No loader registered for resource type: " << static_cast<int>(type) << std::endl;
        return nullptr;
    }
    
    // 加載資源
    void* resource = nullptr;
    if (it->second->Load(path, &resource)) {
        resources[path] = resource;
        
        // 創建元數據
        ResourceMetadata meta;
        meta.name = path;
        meta.path = path;
        meta.type = type;
        meta.referenceCount = 1;
        meta.persistent = false;
        
        // 計算資源大小（簡化實現）
        meta.size = 1024; // 默认1KB，實際應該從資源獲取
        
        metadata[path] = meta;
        currentMemoryUsage += meta.size;
        
        std::cout << "Loaded resource: " << path << std::endl;
        
        // 檢查內存使用
        CheckMemoryUsage();
        
        return resource;
    }
    
    return nullptr;
}

void ResourceCache::Unload(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex);
    UnloadResource(path);
}

void ResourceCache::UnloadAll() {
    std::lock_guard<std::mutex> lock(mutex);
    
    for (auto& [path, resource] : resources) {
        auto metaIt = metadata.find(path);
        if (metaIt != metadata.end() && loaders.find(metaIt->second.type) != loaders.end()) {
            loaders[metaIt->second.type]->Unload(resource);
        }
    }
    
    resources.clear();
    metadata.clear();
    currentMemoryUsage = 0;
}

void ResourceCache::RegisterLoader(ResourceType type, SharedPtr<IResourceLoader> loader) {
    std::lock_guard<std::mutex> lock(mutex);
    loaders[type] = loader;
    std::cout << "Registered loader for type: " << static_cast<int>(type) << std::endl;
}

bool ResourceCache::IsLoaded(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex);
    return resources.find(path) != resources.end();
}

ResourceMetadata ResourceCache::GetMetadata(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = metadata.find(path);
    if (it != metadata.end()) {
        return it->second;
    }
    return ResourceMetadata();
}

size_t ResourceCache::GetLoadedResourceCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return resources.size();
}

size_t ResourceCache::GetTotalMemoryUsage() const {
    std::lock_guard<std::mutex> lock(mutex);
    return currentMemoryUsage;
}

void ResourceCache::SetMaxMemory(size_t maxMemory) {
    std::lock_guard<std::mutex> lock(mutex);
    this->maxMemory = maxMemory;
    CheckMemoryUsage();
}

void ResourceCache::EnableAutoUnload(bool enable) {
    std::lock_guard<std::mutex> lock(mutex);
    autoUnloadEnabled = enable;
}

void ResourceCache::SetAutoUnloadThreshold(float threshold) {
    std::lock_guard<std::mutex> lock(mutex);
    autoUnloadThreshold = std::clamp(threshold, 0.0f, 1.0f);
}

void ResourceCache::PrintStatistics() const {
    std::lock_guard<std::mutex> lock(mutex);
    
    std::cout << "=== Resource Cache Statistics ===" << std::endl;
    std::cout << "Loaded Resources: " << resources.size() << std::endl;
    std::cout << "Memory Usage: " << currentMemoryUsage << " / " << maxMemory << " bytes" << std::endl;
    std::cout << "Memory Percentage: " << (static_cast<float>(currentMemoryUsage) / maxMemory * 100.0f) << "%" << std::endl;
    std::cout << "Auto Unload: " << (autoUnloadEnabled ? "Enabled" : "Disabled") << std::endl;
    std::cout << "Unload Threshold: " << (autoUnloadThreshold * 100.0f) << "%" << std::endl;
}

void ResourceCache::LoadResource(const std::string& path) {
    // 這個方法由LoadRaw內部調用
}

void ResourceCache::UnloadResource(const std::string& path) {
    auto it = resources.find(path);
    if (it == resources.end()) {
        return;
    }
    
    ResourceMetadata& meta = metadata[path];
    // uint32 在 0 時遞減會繞回 UINT_MAX，導致永遠無法卸載
    if (meta.referenceCount > 0) {
        meta.referenceCount--;
    }
    
    if (meta.referenceCount == 0 && !meta.persistent) {
        // 卸載資源
        if (loaders.find(meta.type) != loaders.end()) {
            loaders[meta.type]->Unload(it->second);
        }
        
        if (currentMemoryUsage >= meta.size) {
            currentMemoryUsage -= meta.size;
        } else {
            currentMemoryUsage = 0;
        }
        resources.erase(it);
        metadata.erase(path);
        
        std::cout << "Unloaded resource: " << path << std::endl;
    }
}

void ResourceCache::CheckMemoryUsage() {
    if (!autoUnloadEnabled) return;
    
    float usageRatio = static_cast<float>(currentMemoryUsage) / maxMemory;
    
    if (usageRatio > autoUnloadThreshold) {
        // 卸載非持久化的資源
        for (auto it = metadata.begin(); it != metadata.end(); ) {
            if (ShouldUnload(it->second)) {
                UnloadResource(it->first);
                it = metadata.begin(); // 重新開始，因為地圖已經改變
            } else {
                ++it;
            }
        }
    }
}

bool ResourceCache::ShouldUnload(const ResourceMetadata& metadata) const {
    return !metadata.persistent && metadata.referenceCount <= 0;
}

// ============================================================================
// ResourceManager 實現
// ============================================================================

ResourceManager::ResourceManager()
    : cache(MakeUnique<ResourceCache>())
    , asyncLoadingEnabled(false)
    , initialized(false)
{
}

ResourceManager::~ResourceManager() {
    Shutdown();
}

bool ResourceManager::Initialize() {
    if (initialized) {
        return true;
    }
    
    // 添加默認資源路徑
    AddResourcePath("./Resources");
    AddResourcePath("./Assets");
    
    initialized = true;
    std::cout << "Resource Manager initialized" << std::endl;
    
    return true;
}

void ResourceManager::Shutdown() {
    if (!initialized) {
        return;
    }
    
    cache->UnloadAll();
    resourcePaths.clear();
    
    initialized = false;
    std::cout << "Resource Manager shutdown complete" << std::endl;
}

SharedPtr<Texture> ResourceManager::LoadTexture(const std::string& path) {
    std::string resolvedPath = ResolveResourcePath(path);
    void* resource = cache->LoadRaw(resolvedPath, ResourceType::Texture);
    
    if (resource) {
        return SharedPtr<Texture>(static_cast<Texture*>(resource), [](Texture*){});
    }
    
    return nullptr;
}

void ResourceManager::UnloadTexture(const std::string& path) {
    std::string resolvedPath = ResolveResourcePath(path);
    cache->Unload(resolvedPath);
}

SharedPtr<Texture> ResourceManager::GetTexture(const std::string& path) {
    std::string resolvedPath = ResolveResourcePath(path);
    void* resource = cache->LoadRaw(resolvedPath, ResourceType::Texture);
    
    if (resource) {
        return SharedPtr<Texture>(static_cast<Texture*>(resource), [](Texture*){});
    }
    
    return nullptr;
}

SharedPtr<Mesh> ResourceManager::LoadMesh(const std::string& path) {
    std::string resolvedPath = ResolveResourcePath(path);
    void* resource = cache->LoadRaw(resolvedPath, ResourceType::Mesh);
    
    if (resource) {
        return SharedPtr<Mesh>(static_cast<Mesh*>(resource), [](Mesh*){});
    }
    
    return nullptr;
}

void ResourceManager::UnloadMesh(const std::string& path) {
    std::string resolvedPath = ResolveResourcePath(path);
    cache->Unload(resolvedPath);
}

SharedPtr<Mesh> ResourceManager::GetMesh(const std::string& path) {
    std::string resolvedPath = ResolveResourcePath(path);
    void* resource = cache->LoadRaw(resolvedPath, ResourceType::Mesh);
    
    if (resource) {
        return SharedPtr<Mesh>(static_cast<Mesh*>(resource), [](Mesh*){});
    }
    
    return nullptr;
}

SharedPtr<Shader> ResourceManager::LoadShader(const std::string& vertexPath, const std::string& fragmentPath) {
    std::string resolvedVertexPath = ResolveResourcePath(vertexPath);
    std::string resolvedFragmentPath = ResolveResourcePath(fragmentPath);
    
    // 組合路徑作為唯一標識
    std::string combinedPath = resolvedVertexPath + "|" + resolvedFragmentPath;
    void* resource = cache->LoadRaw(combinedPath, ResourceType::Shader);
    
    if (resource) {
        return SharedPtr<Shader>(static_cast<Shader*>(resource), [](Shader*){});
    }
    
    return nullptr;
}

void ResourceManager::UnloadShader(const std::string& name) {
    cache->Unload(name);
}

SharedPtr<Shader> ResourceManager::GetShader(const std::string& name) {
    void* resource = cache->LoadRaw(name, ResourceType::Shader);
    
    if (resource) {
        return SharedPtr<Shader>(static_cast<Shader*>(resource), [](Shader*){});
    }
    
    return nullptr;
}

SharedPtr<AudioBuffer> ResourceManager::LoadAudio(const std::string& path) {
    std::string resolvedPath = ResolveResourcePath(path);
    void* resource = cache->LoadRaw(resolvedPath, ResourceType::Audio);
    
    if (resource) {
        return SharedPtr<AudioBuffer>(static_cast<AudioBuffer*>(resource), [](AudioBuffer*){});
    }
    
    return nullptr;
}

void ResourceManager::UnloadAudio(const std::string& path) {
    std::string resolvedPath = ResolveResourcePath(path);
    cache->Unload(resolvedPath);
}

SharedPtr<AudioBuffer> ResourceManager::GetAudio(const std::string& path) {
    std::string resolvedPath = ResolveResourcePath(path);
    void* resource = cache->LoadRaw(resolvedPath, ResourceType::Audio);
    
    if (resource) {
        return SharedPtr<AudioBuffer>(static_cast<AudioBuffer*>(resource), [](AudioBuffer*){});
    }
    
    return nullptr;
}

void ResourceManager::UnloadUnusedResources() {
    // 遍歷所有資源，卸載引用計數為0的
    cache->UnloadAll(); // 簡化實現
}

void ResourceManager::UnloadAllResources() {
    cache->UnloadAll();
}

size_t ResourceManager::GetMemoryUsage() const {
    return cache->GetTotalMemoryUsage();
}

int ResourceManager::GetLoadedResourceCount() const {
    return static_cast<int>(cache->GetLoadedResourceCount());
}

void ResourceManager::AddResourcePath(const std::string& path) {
    resourcePaths.push_back(path);
    std::cout << "Added resource path: " << path << std::endl;
}

void ResourceManager::RemoveResourcePath(const std::string& path) {
    auto it = std::find(resourcePaths.begin(), resourcePaths.end(), path);
    if (it != resourcePaths.end()) {
        resourcePaths.erase(it);
        std::cout << "Removed resource path: " << path << std::endl;
    }
}

std::string ResourceManager::ResolveResourcePath(const std::string& path) const {
    return ResolvePath(path);
}

void ResourceManager::EnableAsyncLoading(bool enable) {
    asyncLoadingEnabled = enable;
    std::cout << "Async loading " << (enable ? "enabled" : "disabled") << std::endl;
}

bool ResourceManager::IsAsyncLoadingEnabled() const {
    return asyncLoadingEnabled;
}

std::string ResourceManager::ResolvePath(const std::string& path) const {
    if (path.empty()) {
        return path;
    }
    
    // 如果是絕對路徑，直接返回
    if (path.find(':') != std::string::npos || path[0] == '/' || path[0] == '\\') {
        return path;
    }
    
    // 在資源路徑中查找第一個實際存在的檔案
    for (const auto& resourcePath : resourcePaths) {
        std::string fullPath = resourcePath;
        if (!fullPath.empty() && fullPath.back() != '/' && fullPath.back() != '\\') {
            fullPath += '/';
        }
        fullPath += path;
        
        std::error_code ec;
        if (std::filesystem::exists(fullPath, ec)) {
            return fullPath;
        }
    }
    
    return path;
}

// ============================================================================
// 全局函數實現
// ============================================================================

bool InitializeResourceManager() {
    if (gResourceManager) {
        return false;
    }
    
    gResourceManager = new ResourceManager();
    return gResourceManager->Initialize();
}

void ShutdownResourceManager() {
    if (gResourceManager) {
        delete gResourceManager;
        gResourceManager = nullptr;
    }
}

ResourceManager* GetResourceManager() {
    return gResourceManager;
}

} // namespace Potato