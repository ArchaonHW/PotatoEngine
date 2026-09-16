#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <utility>

namespace Potato {

// 基本類型定義
using int8 = int8_t;
using int16 = int16_t;
using int32 = int32_t;
using int64 = int64_t;
using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

// 浮點類型
using float32 = float;
using float64 = double;

// 字符串類型
using String = std::string;
using WString = std::wstring;

// 智能指針類型
template<typename T>
using SharedPtr = std::shared_ptr<T>;

template<typename T>
using UniquePtr = std::unique_ptr<T>;

template<typename T>
using WeakPtr = std::weak_ptr<T>;

// 自定義刪除器
template<typename T>
struct DefaultDeleter {
    void operator()(T* ptr) const {
        delete ptr;
    }
};

/**
 * 引用計數對象基類
 * 用於需要手動引用計數的對象
 */
class RefCounted {
public:
    RefCounted() : refCount(1) {}
    virtual ~RefCounted() = default;
    
    void AddRef() {
        refCount++;
    }
    
    void Release() {
        refCount--;
        if (refCount <= 0) {
            delete this;
        }
    }
    
    int GetRefCount() const {
        return refCount;
    }
    
private:
    int refCount;
};

/**
 * 引用指針類型
 * 用於引用計數對象
 */
template<typename T>
class RefPtr {
public:
    RefPtr() : ptr(nullptr) {}
    RefPtr(T* p) : ptr(p) {
        if (ptr) {
            ptr->AddRef();
        }
    }
    
    RefPtr(const RefPtr& other) : ptr(other.ptr) {
        if (ptr) {
            ptr->AddRef();
        }
    }
    
    RefPtr(RefPtr&& other) noexcept : ptr(other.ptr) {
        other.ptr = nullptr;
    }
    
    ~RefPtr() {
        if (ptr) {
            ptr->Release();
        }
    }
    
    RefPtr& operator=(const RefPtr& other) {
        if (this != &other) {
            if (ptr) {
                ptr->Release();
            }
            ptr = other.ptr;
            if (ptr) {
                ptr->AddRef();
            }
        }
        return *this;
    }
    
    RefPtr& operator=(RefPtr&& other) noexcept {
        if (this != &other) {
            if (ptr) {
                ptr->Release();
            }
            ptr = other.ptr;
            other.ptr = nullptr;
        }
        return *this;
    }
    
    T* operator->() const { return ptr; }
    T& operator*() const { return *ptr; }
    explicit operator bool() const { return ptr != nullptr; }
    
    T* Get() const { return ptr; }
    void Reset() {
        if (ptr) {
            ptr->Release();
            ptr = nullptr;
        }
    }
    
private:
    T* ptr;
};

/**
 * 引用計數智能指針工廠函數
 */
template<typename T, typename... Args>
RefPtr<T> MakeRef(Args&&... args) {
    return RefPtr<T>(new T(std::forward<Args>(args)...));
}

/**
 * UniquePtr 工廠函數
 */
template<typename T, typename... Args>
UniquePtr<T> MakeUnique(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

/**
 * SharedPtr 工廠函數
 */
template<typename T, typename... Args>
SharedPtr<T> MakeShared(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}

} // namespace Potato