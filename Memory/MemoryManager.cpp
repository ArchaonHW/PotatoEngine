#include "MemoryManager.h"
#include <iostream>
#include <iomanip>
#include <chrono>

namespace Potato {

// 全局內存管理器指針
MemoryManager* gMemoryManager = nullptr;

// ============================================================================
// MemoryPool 實現
// ============================================================================

MemoryPool::MemoryPool(const std::string& name, size_t blockSize, size_t capacity)
    : name(name)
    , blockSize(blockSize)
    , capacity(capacity)
    , usedCount(0)
    , freeCount(capacity)
    , memoryBlock(nullptr)
{
    // 分配內存塊
    memoryBlock = std::malloc(blockSize * capacity);
    
    if (memoryBlock) {
        // 初始化空閒塊棧
        char* blockPtr = static_cast<char*>(memoryBlock);
        for (size_t i = 0; i < capacity; i++) {
            freeBlocks.push(blockPtr + (i * blockSize));
        }
    }
}

MemoryPool::~MemoryPool() {
    if (memoryBlock) {
        std::free(memoryBlock);
        memoryBlock = nullptr;
    }
}

void* MemoryPool::Allocate() {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (freeBlocks.empty()) {
        return nullptr; // 池已滿
    }
    
    void* pointer = freeBlocks.top();
    freeBlocks.pop();
    allocatedBlocks.insert(pointer);
    
    usedCount++;
    freeCount--;
    
    return pointer;
}

void MemoryPool::Free(void* pointer) {
    if (!pointer) return;
    
    std::lock_guard<std::mutex> lock(mutex);
    
    // 只接受 Allocate() 發出的塊起始位址：
    // 自動拒絕池外指標、塊中間指標（未對齊）與雙重釋放
    if (allocatedBlocks.erase(pointer) == 0) {
        return;
    }
    
    freeBlocks.push(pointer);
    usedCount--;
    freeCount++;
}

// ============================================================================
// MemoryManager 實現
// ============================================================================

MemoryManager::MemoryManager()
    : maxMemory(0)
    , trackingEnabled(false)
{
}

MemoryManager::~MemoryManager() {
    Shutdown();
}

bool MemoryManager::Initialize(size_t maxMemoryMB) {
    maxMemory = maxMemoryMB * 1024 * 1024; // 轉換為字節
    
    std::cout << "Memory Manager initialized with " << maxMemoryMB << " MB limit" << std::endl;
    
    return true;
}

void MemoryManager::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex);
    
    // 清理所有內存池
    memoryPools.clear();
    
    // 檢查內存泄漏（已持有 mutex，呼叫無鎖版本避免自我死結）
    if (trackingEnabled && !allocationTracker.empty()) {
        std::cout << "WARNING: Memory leaks detected!" << std::endl;
        DumpMemoryLeaksUnlocked();
    }
    
    allocationTracker.clear();
    
    std::cout << "Memory Manager shutdown complete" << std::endl;
}

void* MemoryManager::Allocate(size_t size, size_t alignment) {
    return AllocateAligned(size, alignment);
}

void MemoryManager::Free(void* pointer) {
    // Allocate() 一律走對齊分配，必須用配對的釋放函數
    // （_aligned_malloc 的記憶體用 std::free 會造成堆損毀）
    FreeAligned(pointer);
}

void* MemoryManager::AllocateAligned(size_t size, size_t alignment) {
    if (size == 0) {
        return nullptr;
    }
    if (alignment == 0) {
        alignment = DEFAULT_ALIGNMENT;
    }
#ifdef _WIN32
    // _aligned_malloc 要求 alignment 為 2 的冪且 >= sizeof(void*)
    if (alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0) {
        alignment = DEFAULT_ALIGNMENT;
    }
#endif
    
    std::lock_guard<std::mutex> lock(mutex);
    
    // 檢查內存限制（maxMemory == 0 表示未初始化/無限制）
    if (maxMemory > 0 && stats.currentUsage + size > maxMemory) {
        std::cerr << "Memory allocation failed: exceeded maximum memory limit" << std::endl;
        return nullptr;
    }
    
    // 對齊分配
#ifdef _WIN32
    void* pointer = _aligned_malloc(size, alignment);
#else
    void* pointer = nullptr;
    if (posix_memalign(&pointer, alignment, size) != 0) {
        pointer = nullptr;
    }
#endif
    
    if (pointer) {
        TrackAllocation(pointer, size, alignment);
        stats.totalAllocated += size;
        stats.currentUsage += size;
        stats.allocationCount++;
        UpdatePeakUsage();
    }
    
    return pointer;
}

void MemoryManager::FreeAligned(void* pointer) {
    if (!pointer) return;
    
    std::lock_guard<std::mutex> lock(mutex);
    
    size_t size = GetAllocationSize(pointer);
    if (size == 0) {
        // 未知指標（非本管理器分配或已釋放）— 拒絕釋放避免堆損毀
        std::cerr << "WARNING: MemoryManager::Free on untracked pointer" << std::endl;
        return;
    }
    stats.totalFreed += size;
    stats.currentUsage -= size;
    
    TrackDeallocation(pointer);
    
#ifdef _WIN32
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

MemoryStats MemoryManager::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex);
    return stats;
}

void MemoryManager::ResetStats() {
    std::lock_guard<std::mutex> lock(mutex);
    stats = MemoryStats();
}

bool MemoryManager::CreateMemoryPool(const std::string& name, size_t size, size_t blockSize) {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (memoryPools.find(name) != memoryPools.end()) {
        std::cerr << "Memory pool '" << name << "' already exists" << std::endl;
        return false;
    }
    
    if (blockSize == 0 || size == 0) {
        std::cerr << "Memory pool '" << name << "': blockSize and size must be non-zero" << std::endl;
        return false;
    }
    
    size_t capacity = size / blockSize;
    auto pool = std::make_unique<MemoryPool>(name, blockSize, capacity);
    
    if (pool->GetCapacity() > 0) {
        memoryPools[name] = std::move(pool);
        std::cout << "Created memory pool '" << name << "' with " << capacity 
                  << " blocks of " << blockSize << " bytes each" << std::endl;
        return true;
    }
    
    return false;
}

void MemoryManager::DestroyMemoryPool(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex);
    
    auto it = memoryPools.find(name);
    if (it != memoryPools.end()) {
        std::cout << "Destroying memory pool '" << name << "'" << std::endl;
        memoryPools.erase(it);
    }
}

void* MemoryManager::AllocateFromPool(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex);
    
    auto it = memoryPools.find(name);
    if (it != memoryPools.end()) {
        return it->second->Allocate();
    }
    
    std::cerr << "Memory pool '" << name << "' not found" << std::endl;
    return nullptr;
}

void MemoryManager::FreeToPool(const std::string& name, void* pointer) {
    if (!pointer) return;
    
    std::lock_guard<std::mutex> lock(mutex);
    
    auto it = memoryPools.find(name);
    if (it != memoryPools.end()) {
        it->second->Free(pointer);
    } else {
        std::cerr << "Memory pool '" << name << "' not found" << std::endl;
    }
}

void MemoryManager::EnableMemoryTracking(bool enable) {
    std::lock_guard<std::mutex> lock(mutex);
    trackingEnabled = enable;
    
    if (enable) {
        std::cout << "Memory tracking enabled" << std::endl;
    } else {
        std::cout << "Memory tracking disabled" << std::endl;
    }
}

void MemoryManager::DumpMemoryLeaks() {
    std::lock_guard<std::mutex> lock(mutex);
    DumpMemoryLeaksUnlocked();
}

void MemoryManager::DumpMemoryLeaksUnlocked() {
    if (allocationTracker.empty()) {
        std::cout << "No memory leaks detected" << std::endl;
        return;
    }
    
    std::cout << "=== Memory Leak Report ===" << std::endl;
    std::cout << "Total leaks: " << allocationTracker.size() << std::endl;
    std::cout << "Total leaked memory: " << stats.currentUsage << " bytes" << std::endl;
    
    for (const auto& [pointer, info] : allocationTracker) {
        std::cout << "Leak at " << pointer << ": " << info.size << " bytes";
        if (info.file) {
            std::cout << " (" << info.file << ":" << info.line << ")";
        }
        std::cout << std::endl;
    }
}

void MemoryManager::ValidateMemory() {
    std::lock_guard<std::mutex> lock(mutex);
    
    std::cout << "Validating memory..." << std::endl;
    
    // 檢查所有分配的內存
    for (const auto& [pointer, info] : allocationTracker) {
        if (!IsValidPointer(pointer)) {
            std::cerr << "Invalid pointer detected: " << pointer << std::endl;
        }
    }
    
    // 檢查內存池
    for (const auto& [name, pool] : memoryPools) {
        size_t total = pool->GetUsedCount() + pool->GetFreeCount();
        if (total != pool->GetCapacity()) {
            std::cerr << "Memory pool '" << name << "' has inconsistent counts" << std::endl;
        }
    }
    
    std::cout << "Memory validation complete" << std::endl;
}

float MemoryManager::GetMemoryUsagePercentage() const {
    if (maxMemory == 0) return 0.0f;
    return (static_cast<float>(stats.currentUsage) / static_cast<float>(maxMemory)) * 100.0f;
}

void MemoryManager::PrintStats() const {
    std::lock_guard<std::mutex> lock(mutex);
    
    std::cout << "=== Memory Statistics ===" << std::endl;
    std::cout << "Total Allocated: " << stats.totalAllocated << " bytes" << std::endl;
    std::cout << "Total Freed: " << stats.totalFreed << " bytes" << std::endl;
    std::cout << "Current Usage: " << stats.currentUsage << " bytes" << std::endl;
    std::cout << "Peak Usage: " << stats.peakUsage << " bytes" << std::endl;
    std::cout << "Allocation Count: " << stats.allocationCount << std::endl;
    std::cout << "Memory Limit: " << maxMemory << " bytes" << std::endl;
    std::cout << "Usage Percentage: " << std::fixed << std::setprecision(2) 
              << GetMemoryUsagePercentage() << "%" << std::endl;
}

void MemoryManager::PrintPools() const {
    std::lock_guard<std::mutex> lock(mutex);
    
    std::cout << "=== Memory Pools ===" << std::endl;
    
    if (memoryPools.empty()) {
        std::cout << "No memory pools created" << std::endl;
        return;
    }
    
    for (const auto& [name, pool] : memoryPools) {
        std::cout << "Pool: " << name << std::endl;
        std::cout << "  Block Size: " << pool->GetBlockSize() << " bytes" << std::endl;
        std::cout << "  Capacity: " << pool->GetCapacity() << std::endl;
        std::cout << "  Used: " << pool->GetUsedCount() << std::endl;
        std::cout << "  Free: " << pool->GetFreeCount() << std::endl;
    }
}

void MemoryManager::TrackAllocation(void* pointer, size_t size, size_t alignment, const char* file, int line) {
    // 無論 trackingEnabled 都記錄：FreeAligned 需要查表取得 size 來更新統計
    // trackingEnabled 只控制 Shutdown 時是否輸出洩漏報告
    AllocationInfo info;
    info.pointer = pointer;
    info.size = size;
    info.alignment = alignment;
    info.file = file;
    info.line = line;
    info.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    allocationTracker[pointer] = info;
}

void MemoryManager::TrackDeallocation(void* pointer) {
    allocationTracker.erase(pointer);
}

void MemoryManager::UpdatePeakUsage() {
    if (stats.currentUsage > stats.peakUsage) {
        stats.peakUsage = stats.currentUsage;
    }
}

bool MemoryManager::IsValidPointer(void* pointer) const {
    return pointer != nullptr && allocationTracker.find(pointer) != allocationTracker.end();
}

size_t MemoryManager::GetAllocationSize(void* pointer) const {
    auto it = allocationTracker.find(pointer);
    if (it != allocationTracker.end()) {
        return it->second.size;
    }
    return 0;
}

// ============================================================================
// 全局函數實現
// ============================================================================

bool InitializeMemoryManager(size_t maxMemoryMB) {
    if (gMemoryManager) {
        std::cerr << "Memory manager already initialized" << std::endl;
        return false;
    }
    
    gMemoryManager = new MemoryManager();
    return gMemoryManager->Initialize(maxMemoryMB);
}

void ShutdownMemoryManager() {
    if (gMemoryManager) {
        delete gMemoryManager;
        gMemoryManager = nullptr;
    }
}

MemoryManager* GetMemoryManager() {
    return gMemoryManager;
}

} // namespace Potato