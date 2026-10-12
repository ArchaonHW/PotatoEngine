#include "Memory/MemoryManager.h"
#include <iostream>

using namespace Potato;

int main() {
    std::cout << "=== Potato Engine Memory Manager Test ===" << std::endl;
    
    // 初始化內存管理器
    if (!InitializeMemoryManager(1024)) { // 1GB limit
        std::cerr << "Failed to initialize memory manager" << std::endl;
        return -1;
    }
    
    MemoryManager* memoryManager = GetMemoryManager();
    
    // 測試基本分配
    std::cout << "\n--- Basic Allocation Tests ---" << std::endl;
    void* ptr1 = memoryManager->Allocate(1024);
    void* ptr2 = memoryManager->Allocate(2048);
    void* ptr3 = memoryManager->Allocate(4096);
    
    std::cout << "Allocated 3 blocks: 1024, 2048, 4096 bytes" << std::endl;
    memoryManager->PrintStats();
    
    // 測試釋放
    std::cout << "\n--- Deallocation Tests ---" << std::endl;
    memoryManager->Free(ptr2);
    std::cout << "Freed 2048 byte block" << std::endl;
    memoryManager->PrintStats();
    
    // 測試對齊分配
    std::cout << "\n--- Aligned Allocation Tests ---" << std::endl;
    void* alignedPtr = memoryManager->AllocateAligned(512, 64);
    std::cout << "Allocated 512 bytes with 64-byte alignment" << std::endl;
    std::cout << "Pointer: " << alignedPtr << " (alignment: " << (reinterpret_cast<uintptr_t>(alignedPtr) % 64 == 0 ? "OK" : "FAIL") << ")" << std::endl;
    memoryManager->FreeAligned(alignedPtr);
    
    // 測試內存池
    std::cout << "\n--- Memory Pool Tests ---" << std::endl;
    memoryManager->CreateMemoryPool("TestPool", 10240, 256); // 10KB pool with 256-byte blocks
    memoryManager->PrintPools();
    
    void* poolPtr1 = memoryManager->AllocateFromPool("TestPool");
    void* poolPtr2 = memoryManager->AllocateFromPool("TestPool");
    void* poolPtr3 = memoryManager->AllocateFromPool("TestPool");
    
    std::cout << "Allocated 3 blocks from pool" << std::endl;
    memoryManager->PrintPools();
    
    memoryManager->FreeToPool("TestPool", poolPtr2);
    std::cout << "Freed one block back to pool" << std::endl;
    memoryManager->PrintPools();
    
    memoryManager->FreeToPool("TestPool", poolPtr1);
    memoryManager->FreeToPool("TestPool", poolPtr3);
    
    // 測試內存追蹤
    std::cout << "\n--- Memory Tracking Tests ---" << std::endl;
    memoryManager->EnableMemoryTracking(true);
    
    void* trackedPtr1 = memoryManager->Allocate(128);
    void* trackedPtr2 = memoryManager->Allocate(256);
    
    std::cout << "Allocated tracked memory blocks" << std::endl;
    memoryManager->PrintStats();
    
    memoryManager->Free(trackedPtr1);
    memoryManager->Free(trackedPtr2);
    
    std::cout << "Freed tracked memory blocks" << std::endl;
    memoryManager->PrintStats();
    
    // 測試內存驗證
    std::cout << "\n--- Memory Validation Tests ---" << std::endl;
    memoryManager->ValidateMemory();
    
    // 清理
    std::cout << "\n--- Cleanup ---" << std::endl;
    memoryManager->Free(ptr1);
    memoryManager->Free(ptr3);
    memoryManager->DestroyMemoryPool("TestPool");
    
    memoryManager->PrintStats();
    
    // 關閉內存管理器
    ShutdownMemoryManager();
    
    std::cout << "\n=== Memory Manager Test Complete ===" << std::endl;
    
    return 0;
}