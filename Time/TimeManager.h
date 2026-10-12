#pragma once

#include <cstdint>
#include <chrono>
#include <functional>
#include <vector>
#include <memory>
#include <algorithm>

namespace Potato {

/**
 * 時間刻度類型
 */
enum class TimeScale {
    RealTime,
    GameTime,
    FixedTime,
    SlowMotion,
    Paused
};

/**
 * 計時器類
 * 用於測量時間間隔
 */
class Timer {
public:
    Timer();
    
    void Start();
    void Stop();
    void Reset();
    
    float GetElapsedTime() const;
    float GetDeltaTime() const;
    bool IsRunning() const { return running; }
    
private:
    std::chrono::high_resolution_clock::time_point startTime;
    std::chrono::high_resolution_clock::time_point endTime;
    std::chrono::duration<float> elapsedTime;
    bool running;
};

/**
 * 計時器句柄
 */
using TimerHandle = uint32_t;

/**
 * 計時器管理器
 * 管理多個計時器
 */
class TimerManager {
public:
    TimerManager();
    ~TimerManager();
    
    TimerHandle CreateTimer();
    void DestroyTimer(TimerHandle handle);
    
    void StartTimer(TimerHandle handle);
    void StopTimer(TimerHandle handle);
    void ResetTimer(TimerHandle handle);
    
    float GetElapsedTime(TimerHandle handle) const;
    bool IsTimerRunning(TimerHandle handle) const;
    
    void UpdateAllTimers();
    void Clear();
    
private:
    std::vector<Timer> timers;
    std::vector<bool> timerValidity;
};

/**
 * 時間管理器
 * 管理遊戲時間、時間刻度和計時器
 */
class TimeManager {
public:
    TimeManager();
    ~TimeManager();
    
    // 初始化和更新
    void Initialize();
    void Update();
    void Shutdown();
    
    // 時間獲取
    float GetDeltaTime() const { return deltaTime; }
    float GetTotalTime() const { return totalTime; }
    float GetFixedDeltaTime() const { return fixedDeltaTime; }
    
    // 時間刻度
    void SetTimeScale(TimeScale scale);
    TimeScale GetTimeScale() const { return currentTimeScale; }
    
    void SetTimeScaleFactor(float factor);
    float GetTimeScaleFactor() const { return timeScaleFactor; }
    
    // 固定時間步長
    void SetFixedDeltaTime(float fixedDT) { fixedDeltaTime = fixedDT; }
    
    // 暫停和恢復
    void Pause();
    void Resume();
    bool IsPaused() const { return isPaused; }
    
    // 時間操作
    void SetTimeScaleFactor(float factor, float duration);
    void ResetTimeScaleFactor();
    
    // 幀率控制
    void SetTargetFPS(int fps);
    int GetTargetFPS() const { return targetFPS; }
    int GetCurrentFPS() const { return currentFPS; }
    
    // 計時器管理
    TimerHandle CreateTimer();
    void DestroyTimer(TimerHandle handle);
    void StartTimer(TimerHandle handle);
    void StopTimer(TimerHandle handle);
    float GetElapsedTime(TimerHandle handle) const;
    
    // 延遲執行
    using DelayedCallback = std::function<void()>;
    void DelayedCall(float delay, DelayedCallback callback);
    
    // 回調註冊
    using TimeCallback = std::function<void(float)>;
    void RegisterUpdateCallback(TimeCallback callback);
    void UnregisterUpdateCallback(TimeCallback callback);
    
    // 時間統計
    void PrintStatistics() const;
    
private:
    void UpdateFPS(float rawDeltaTime);
    void ProcessDelayedCalls();
    void UpdateTimeScale();
    
private:
    // 時間變量
    float deltaTime;
    float totalTime;
    float fixedDeltaTime;
    float timeScaleFactor;
    float targetTimeScaleFactor;
    float timeScaleTransitionDuration;
    float timeScaleTransitionTimer;
    
    // 時間刻度
    TimeScale currentTimeScale;
    bool isPaused;
    
    // FPS 控制
    int targetFPS;
    int currentFPS;
    float fpsUpdateTime;
    int frameCount;
    
    // 計時器管理
    std::unique_ptr<TimerManager> timerManager;
    
    // 延遲調用
    struct DelayedCallInfo {
        float delay;
        float timer;
        DelayedCallback callback;
    };
    std::vector<DelayedCallInfo> delayedCalls;
    
    // 更新回調
    std::vector<TimeCallback> updateCallbacks;
    
    // 系統時間
    std::chrono::high_resolution_clock::time_point lastFrameTime;
    std::chrono::high_resolution_clock::time_point currentTime;
    
    bool initialized;
};

// 全局時間管理器
extern TimeManager* gTimeManager;

/**
 * 初始化全局時間管理器
 */
bool InitializeTimeManager();

/**
 * 關閉全局時間管理器
 */
void ShutdownTimeManager();

/**
 * 獲取全局時間管理器
 */
TimeManager* GetTimeManager();

} // namespace Potato

// 便捷宏
#define GET_TIME() Potato::GetTimeManager()