#pragma once

#ifndef POTATO_PROTECTED_VALUE_H
#define POTATO_PROTECTED_VALUE_H

/**
 * ProtectedValue<T> — 記憶體數值混淆容器（反 Cheat Engine 式記憶體掃描）
 *
 * 問題：記憶體修改工具的典型流程是「掃描明文數值 → 玩一下 →
 * 篩出變化的位址 → 改寫」。本容器讓此流程失效：
 *   - 存放 cipher = value XOR key（逐位元組再混合位置），
 *     明文永不落記憶體，掃描已知值找不到
 *   - 每次寫入重新配 key：「搜尋舊值→篩新值」的差分流程也失效
 *   - seal 完整性欄位：不經 Set() 的直接記憶體 patch，
 *     下一次 Get() 時 IsTampered() 回 true
 *
 * 誠實性：這是混淆 + 偵測，不是密碼學防護——能下硬體中斷點
 *   觀察 Get() 解碼路徑的攻擊者仍可推出結構。高價值狀態（排名、
 *   商城餘額）應伺服器權威；本類適合單機體感數值（分數、彈藥、
 *   經驗值）提高作弊門檻。
 *
 * 用法：
 *   ProtectedValue<int> score(0);
 *   score += 100;
 *   int s = score;                 // operator T()
 *   if (score.IsTampered()) { ... }
 */

#include <atomic>
#include <cstdint>
#include <cstring>
#include <random>
#include <type_traits>

namespace Potato {
namespace Security {

template <typename T>
class ProtectedValue {
    static_assert(std::is_trivially_copyable_v<T>,
                  "ProtectedValue 僅支援 trivially copyable 型別");
    static_assert(sizeof(T) <= 16,
                  "ProtectedValue 設計給小型數值（<=16 bytes）");

public:
    ProtectedValue() { Rekey(); Set(T{}); }
    explicit ProtectedValue(T initial) { Rekey(); Set(initial); }

    T Get() const {
        uint8_t plain[sizeof(T)];
        const uint8_t* k = reinterpret_cast<const uint8_t*>(&key_);
        for (size_t i = 0; i < sizeof(T); i++)
            plain[i] = static_cast<uint8_t>(cipher_[i] ^ k[i & 7] ^
                                            static_cast<uint8_t>(i));
        T v;
        std::memcpy(&v, plain, sizeof(T));
        // seal 以儲存態計算：外部 patch cipher_ 或 key_ 即失配
        if (seal_ != Seal(cipher_, key_))
            tampered_.store(true, std::memory_order_relaxed);
        ZeroLocal(plain);
        return v;
    }

    void Set(T v) {
        Rekey();
        uint8_t plain[sizeof(T)];
        std::memcpy(plain, &v, sizeof(T));
        const uint8_t* k = reinterpret_cast<const uint8_t*>(&key_);
        for (size_t i = 0; i < sizeof(T); i++)
            cipher_[i] = static_cast<uint8_t>(plain[i] ^ k[i & 7] ^
                                              static_cast<uint8_t>(i));
        seal_ = Seal(cipher_, key_);
        ZeroLocal(plain);
    }

    // 上次 Get() 是否偵測到外部竄改（latch，ResetTampered 清除）
    bool IsTampered() const { return tampered_.load(std::memory_order_relaxed); }
    void ResetTampered() { tampered_.store(false, std::memory_order_relaxed); }

    operator T() const { return Get(); }
    ProtectedValue& operator=(T v) { Set(v); return *this; }
    ProtectedValue& operator+=(T d) { Set(static_cast<T>(Get() + d)); return *this; }
    ProtectedValue& operator-=(T d) { Set(static_cast<T>(Get() - d)); return *this; }
    ProtectedValue& operator++() { Set(static_cast<T>(Get() + T(1))); return *this; }
    ProtectedValue& operator--() { Set(static_cast<T>(Get() - T(1))); return *this; }

private:
    // FNV-1a 64 over cipher ⊕ 位置 ⊕ key，再 XOR canary。
    // 只要求「盲改儲存位元組會失配」——屬混淆層，不需密碼學強度
    static uint64_t Seal(const uint8_t* cipher, uint64_t key) {
        uint64_t h = 1469598103934665603ull ^ key;
        for (size_t i = 0; i < sizeof(T); i++) {
            h ^= static_cast<uint8_t>(cipher[i] +
                     static_cast<uint8_t>(i * 0x9Eu));
            h *= 1099511628211ull;
        }
        return h ^ 0x706F7461546F456Eull; // "PoTaToEn" canary
    }

    // splitmix64（全域 counter + thread-local random_device 鹽）：
    // 每次寫入換 key，明文殘影與差分搜尋都失效
    void Rekey() {
        static std::atomic<uint64_t> counter{0};
        static thread_local uint64_t salt = [] {
            std::random_device rd;
            uint64_t s = 0;
            for (int i = 0; i < 4; i++) s = (s << 16) ^ rd();
            return s ? s : 0xA0761D6478BD642Full;
        }();
        uint64_t z = counter.fetch_add(1, std::memory_order_relaxed) +
                     0x9E3779B97F4A7C15ull + salt;
        z ^= z >> 30; z *= 0xBF58476D1CE4E5B9ull;
        z ^= z >> 27; z *= 0x94D049BB133111EBull;
        z ^= z >> 31;
        key_ = z;
    }

    // volatile 寫零，避免編譯器把「清理解碼緩衝」最佳化掉
    static void ZeroLocal(void* p) {
        volatile uint8_t* v = static_cast<volatile uint8_t*>(p);
        for (size_t i = 0; i < sizeof(T); i++) v[i] = 0;
    }

    alignas(8) uint8_t cipher_[sizeof(T)] = {};
    uint64_t key_ = 0;
    uint64_t seal_ = 0;
    mutable std::atomic<bool> tampered_{false};
};

} // namespace Security
} // namespace Potato

#endif // POTATO_PROTECTED_VALUE_H
