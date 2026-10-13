#pragma once

/**
 * RID / RID_Alloc — Godot core/templates/rid.h + rid_owner.h 的移植。
 *
 * 本檔含衍生自 Godot Engine 的程式碼（MIT License）：
 *   Copyright (c) 2014-present Godot Engine contributors
 *   Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur
 * 完整授權條款見 repo 根目錄 THIRD_PARTY_NOTICES.md。
 *
 * RID 是 64-bit 不透明資源句柄：低 32-bit 為 slot index、
 * 高 32-bit 為 validator（防呆代際號）。資源本體由 RID_Alloc
 * 的 slot 池持有——RID 失效（free 後重用）會被 validator
 * 檔下，get_or_null 回 nullptr 而非野指標。
 *
 * 設計目標與 Godot 相同：給 server 層（RenderingServer）一個
 * 可序列化、可跨執行緒傳遞、無所有權語義的資源參照——
 * AI agent / 工具呼叫拿到 RID 只是一個數字。
 *
 * 與 Godot 的差異：以 std::vector<std::unique_ptr<Chunk[]>>
 * 取代 memrealloc 指標陣列，free list 以 vector 當 stack
 * （Godot 用定長陣列因為 alloc_count 單調成長；語義等價）。
 */

#include "Core/CoreTypes.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <memory>

namespace Potato {

class RID_AllocBase;

class RID {
    friend class RID_AllocBase;
    uint64 _id = 0;

public:
    bool operator==(const RID& o) const { return _id == o._id; }
    bool operator!=(const RID& o) const { return _id != o._id; }
    bool operator<(const RID& o) const { return _id < o._id; }

    bool IsValid() const { return _id != 0; }
    bool IsNull() const { return _id == 0; }

    uint32 GetLocalIndex() const { return _id & 0xFFFFFFFFull; }
    uint64 GetID() const { return _id; }

    static RID FromUint64(uint64 id) {
        RID r;
        r._id = id;
        return r;
    }
};

class RID_AllocBase {
    // 全域 validator 流水號——RID_Alloc 實例間共用，
    // 代際碰撞機率壓到最低（Godot 同款 base_id）
    static std::atomic<uint64>& BaseID() {
        static std::atomic<uint64> id{1};
        return id;
    }

protected:
    static RID MakeFromID(uint64 id) {
        RID r;
        r._id = id;
        return r;
    }
    static uint64 GenID() {
        return BaseID().fetch_add(1, std::memory_order_relaxed);
    }

public:
    virtual ~RID_AllocBase() = default;
};

/**
 * RID_Alloc<T> — slot 池配置器。
 *
 * AllocateRID()：佔 slot、寫 validator（含 0x80000000
 *   「未初始化」旗標）→ 回傳 RID。此時 GetOrNull 仍回
 *   nullptr。
 * InitializeRID(rid, value)：建構 T、清旗標 → RID 生效。
 * GetOrNull(rid)：validator 相符且已初始化 → &data。
 * Free(rid)：validator 歸 0xFFFFFFFF、slot 回 free list。
 *
 * THREAD_SAFE=true 時配置/釋放/查詢全走 mutex
 * （Godot 的 RID_Alloc<T,true> 同款語義）。
 */
template <typename T, bool THREAD_SAFE = false>
class RID_Alloc : public RID_AllocBase {
    struct Chunk {
        T data;
        uint32 validator = 0xFFFFFFFF;
    };

    static constexpr uint32 kUninitializedBit = 0x80000000;
    static constexpr uint32 kValidatorMask = 0x7FFFFFFF;

    std::vector<std::unique_ptr<Chunk[]>> chunks;
    std::vector<uint32> freeList; // stack：pop_back 取 slot
    uint32 elementsInChunk;
    uint32 allocCount = 0; // 活躍數（非 slot 總數）
    const char* description = nullptr;
    mutable std::mutex mutex; // THREAD_SAFE=false 時不進 critical path

    void GrowChunks() {
        chunks.push_back(std::unique_ptr<Chunk[]>(new Chunk[elementsInChunk]));
        const uint32 base = (uint32)(chunks.size() - 1) * elementsInChunk;
        for (uint32 i = 0; i < elementsInChunk; ++i) {
            freeList.push_back(base + i);
        }
    }

public:
    explicit RID_Alloc(uint32 p_elementsInChunk = 64,
                       const char* p_description = nullptr)
        : elementsInChunk(p_elementsInChunk), description(p_description) {
        if (elementsInChunk == 0) elementsInChunk = 1;
    }

    // 佔 slot、未初始化
    RID AllocateRID() {
        std::unique_lock<std::mutex> lock(mutex, std::defer_lock);
        if constexpr (THREAD_SAFE) lock.lock();

        if (freeList.empty()) GrowChunks();
        const uint32 index = freeList.back();
        freeList.pop_back();

        const uint32 validator = 1 + (uint32)(GenID() % kValidatorMask);
        Chunk& c = chunks[index / elementsInChunk][index % elementsInChunk];
        c.validator = validator | kUninitializedBit;
        allocCount++;

        return MakeFromID((uint64(validator) << 32) | index);
    }

    // 佔 slot 並直接建構——回傳已生效 RID
    RID MakeRID(const T& value) {
        RID rid = AllocateRID();
        InitializeRID(rid, value);
        return rid;
    }
    RID MakeRID() {
        RID rid = AllocateRID();
        InitializeRID(rid, T{});
        return rid;
    }

    // 對 AllocateRID 的 slot 建構 T——完成初始化
    void InitializeRID(RID rid, const T& value) {
        std::unique_lock<std::mutex> lock(mutex, std::defer_lock);
        if constexpr (THREAD_SAFE) lock.lock();

        const uint32 index = rid.GetLocalIndex();
        const uint32 chunk = index / elementsInChunk;
        if (chunk >= chunks.size()) return;
        Chunk& c = chunks[chunk][index % elementsInChunk];
        const uint32 validator = uint32(rid.GetID() >> 32);
        if ((c.validator & kValidatorMask) != validator) return;
        c.validator = validator; // 清「未初始化」旗標
        c.data = value;
    }

    T* GetOrNull(RID rid) {
        if (rid.IsNull()) return nullptr;
        const uint32 index = rid.GetLocalIndex();
        const uint32 chunk = index / elementsInChunk;

        if constexpr (THREAD_SAFE) mutex.lock();
        T* result = nullptr;
        if (chunk < chunks.size()) {
            Chunk& c = chunks[chunk][index % elementsInChunk];
            if (c.validator == uint32(rid.GetID() >> 32)) {
                result = &c.data;
            }
        }
        if constexpr (THREAD_SAFE) mutex.unlock();
        return result;
    }

    const T* GetOrNull(RID rid) const {
        return const_cast<RID_Alloc*>(this)->GetOrNull(rid);
    }

    bool Owns(RID rid) const { return GetOrNull(rid) != nullptr; }

    // 已 AllocateRID 但未 InitializeRID 的 slot 也視為持有
    // （Free 對未初始化 slot 合法）
    bool OwnsAlloc(RID rid) const {
        if (rid.IsNull()) return false;
        const uint32 index = rid.GetLocalIndex();
        const uint32 chunk = index / elementsInChunk;
        if constexpr (THREAD_SAFE) mutex.lock();
        bool owned = false;
        if (chunk < chunks.size()) {
            const uint32 v = chunks[chunk][index % elementsInChunk].validator;
            owned = (v & kValidatorMask) == uint32(rid.GetID() >> 32) &&
                    v != 0xFFFFFFFF;
        }
        if constexpr (THREAD_SAFE) mutex.unlock();
        return owned;
    }

    void Free(RID rid) {
        if (rid.IsNull()) return;
        std::unique_lock<std::mutex> lock(mutex, std::defer_lock);
        if constexpr (THREAD_SAFE) lock.lock();

        const uint32 index = rid.GetLocalIndex();
        const uint32 chunk = index / elementsInChunk;
        if (chunk >= chunks.size()) return;
        Chunk& c = chunks[chunk][index % elementsInChunk];
        const uint32 validator = uint32(rid.GetID() >> 32);
        if ((c.validator & kValidatorMask) != validator) return;
        c.validator = 0xFFFFFFFF;
        c.data = T{};
        freeList.push_back(index);
        allocCount--;
    }

    // 列出所有存活 RID（除錯/洩漏檢查）
    std::vector<RID> GetOwnedList() const {
        std::vector<RID> out;
        for (size_t ci = 0; ci < chunks.size(); ++ci) {
            for (uint32 i = 0; i < elementsInChunk; ++i) {
                const uint32 v = chunks[ci][i].validator;
                if (v != 0xFFFFFFFF && !(v & kUninitializedBit)) {
                    out.push_back(MakeFromID(
                        (uint64(v & kValidatorMask) << 32) |
                        uint32(ci * elementsInChunk + i)));
                }
            }
        }
        return out;
    }

    uint32 GetAllocCount() const { return allocCount; }
    const char* GetDescription() const { return description; }
};

} // namespace Potato
