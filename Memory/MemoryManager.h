#pragma once

#include "Core/Interfaces/IMemoryManager.h"
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <stack>
#include <vector>
#include <memory>
#include <string>

namespace Potato {

/**
 * 內存分配信息
 * 用於內存泄漏檢測
 */
struct AllocationInfo {
    void* pointer;
    size_t size;
    size_t alignment;
    const char* file;
    int line;
    size_t timestamp;
};

/**
 * 內存池
 * 用於高效分配固定大小的對象
 */
class MemoryPool {
public:
    MemoryPool(const std::string& name, size_t blockSize, size_t capacity);
    ~MemoryPool();
    
    void* Allocate();
    void Free(void* pointer);
    
    size_t GetBlockSize() const { return blockSize; }
    size_t GetCapacity() const { return capacity; }
    size_t GetUsedCount() const { return usedCount; }
    size_t GetFreeCount() const { return freeCount; }
    const std::string& GetName() const { return name; }
    
private:
    std::string name;
    size_t blockSize;
    size_t capacity;
    size_t usedCount;
    size_t freeCount;
    
    void* memoryBlock;
    std::stack<void*> freeBlocks;
    std::unordered_set<void*> allocatedBlocks; // 驗證歸還指標合法性（防雙重釋放/非對齊指標）
    std::mutex mutex;
};

/**
 * 內存管理器實現
 */
class MemoryManager : public IMemoryManager {
public:
    MemoryManager();
    ~MemoryManager();
    
    // IMemoryManager 接口實現
    bool Initialize(size_t maxMemoryMB) override;
    void Shutdown() override;
    
    void* Allocate(size_t size, size_t alignment = 16) override;
    void Free(void* pointer) override;
    
    void* AllocateAligned(size_t size, size_t alignment) override;
    void FreeAligned(void* pointer) override;
    
    MemoryStats GetStats() const override;
    void ResetStats() override;
    
    // 內存池管理（非接口方法）
    bool CreateMemoryPool(const std::string& name, size_t size, size_t blockSize);
    void DestroyMemoryPool(const std::string& name);
    void* AllocateFromPool(const std::string& name);
    void FreeToPool(const std::string& name, void* pointer);
    
    void EnableMemoryTracking(bool enable) override;
    void DumpMemoryLeaks() override;
    void ValidateMemory() override;
    
    // 內存管理器特定方法
    size_t GetMaxMemory() const { return maxMemory; }
    float GetMemoryUsagePercentage() const;
    
    // 調試信息
    void PrintStats() const;
    void PrintPools() const;
    
private:
    void TrackAllocation(void* pointer, size_t size, size_t alignment, const char* file = nullptr, int line = 0);
    void TrackDeallocation(void* pointer);
    void UpdatePeakUsage();
    void DumpMemoryLeaksUnlocked(); // 呼叫者須已持有 mutex
    
    bool IsValidPointer(void* pointer) const;
    size_t GetAllocationSize(void* pointer) const;
    
private:
    size_t maxMemory;
    MemoryStats stats;
    bool trackingEnabled;
    
    std::unordered_map<std::string, std::unique_ptr<MemoryPool>> memoryPools;
    std::unordered_map<void*, AllocationInfo> allocationTracker;
    mutable std::mutex mutex;
    
    static constexpr size_t DEFAULT_ALIGNMENT = 16;
};

// 全局內存管理器指針
extern MemoryManager* gMemoryManager;

/**
 * 初始化全局內存管理器
 */
bool InitializeMemoryManager(size_t maxMemoryMB = 4096);

/**
 * 關閉全局內存管理器
 */
void ShutdownMemoryManager();

/**
 * 獲取全局內存管理器
 */
MemoryManager* GetMemoryManager();

} // namespace Potato