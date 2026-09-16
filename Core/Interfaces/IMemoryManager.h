#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace Potato {

/**
 * 內存分配統計
 */
struct MemoryStats {
    size_t totalAllocated = 0;
    size_t totalFreed = 0;
    size_t currentUsage = 0;
    size_t peakUsage = 0;
    size_t allocationCount = 0;
};

/**
 * 內存管理器接口
 * 定義內存分配和管理的基本功能
 */
class IMemoryManager {
public:
    virtual ~IMemoryManager() = default;
    
    // 初始化和關閉
    virtual bool Initialize(size_t maxMemoryMB) = 0;
    virtual void Shutdown() = 0;
    
    // 內存分配
    virtual void* Allocate(size_t size, size_t alignment = 16) = 0;
    virtual void Free(void* pointer) = 0;
    
    // 對齊分配
    virtual void* AllocateAligned(size_t size, size_t alignment) = 0;
    virtual void FreeAligned(void* pointer) = 0;
    
    // 對象分配（帶構造函數調用） - 暫時禁用以避免模板錯誤
    // template<typename T, typename... Args>
    // T* New(Args&&... args) {
    //     void* memory = Allocate(sizeof(T), alignof(T));
    //     if (memory) {
    //         return new(memory) T(std::forward<Args>(args)...);
    //     }
    //     return nullptr;
    // }
    
    // 對象釋放（帶析構函數調用） - 暫時禁用以避免模板錯誤
    // template<typename T>
    // void Delete(T* pointer) {
    //     if (pointer) {
    //         pointer->~T();
    //         Free(pointer);
    //     }
    // }
    
    // 統計信息
    virtual MemoryStats GetStats() const = 0;
    virtual void ResetStats() = 0;
    
    // 內存調試
    virtual void EnableMemoryTracking(bool enable) = 0;
    virtual void DumpMemoryLeaks() = 0;
    virtual void ValidateMemory() = 0;
};

} // namespace Potato