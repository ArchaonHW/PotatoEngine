#include "TimeManager.h"
#include <iostream>
#include <algorithm>

namespace Potato {

// 全局時間管理器
TimeManager* gTimeManager = nullptr;

// ============================================================================
// Timer 實現
// ============================================================================

Timer::Timer()
    : running(false)
    , elapsedTime(0.0f)
{
}

void Timer::Start() {
    startTime = std::chrono::high_resolution_clock::now();
    running = true;
}

void Timer::Stop() {
    endTime = std::chrono::high_resolution_clock::now();
    elapsedTime = std::chrono::duration<float>(endTime - startTime);
    running = false;
}

void Timer::Reset() {
    startTime = std::chrono::high_resolution_clock::now();
    elapsedTime = std::chrono::duration<float>(0.0f);
    running = false;
}

float Timer::GetElapsedTime() const {
    if (running) {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<float>(now - startTime).count();
    }
    return elapsedTime.count();
}

float Timer::GetDeltaTime() const {
    return GetElapsedTime();
}

// ============================================================================
// TimerManager 實現
// ============================================================================

TimerManager::TimerManager() {
    timers.resize(100); // 預分配100個計時器
    timerValidity.resize(100, false);
}

TimerManager::~TimerManager() {
    Clear();
}

TimerHandle TimerManager::CreateTimer() {
    // 查找可用的計時器槽
    for (size_t i = 0; i < timerValidity.size(); i++) {
        if (!timerValidity[i]) {
            timerValidity[i] = true;
            timers[i].Reset();
            return static_cast<TimerHandle>(i);
        }
    }
    
    // 如果沒有可用槽，擴展數組
    timerValidity.push_back(true);
    timers.emplace_back();
    return static_cast<TimerHandle>(timerValidity.size() - 1);
}

void TimerManager::DestroyTimer(TimerHandle handle) {
    if (handle < timerValidity.size()) {
        timerValidity[handle] = false;
    }
}

void TimerManager::StartTimer(TimerHandle handle) {
    if (handle < timerValidity.size() && timerValidity[handle]) {
        timers[handle].Start();
    }
}

void TimerManager::StopTimer(TimerHandle handle) {
    if (handle < timerValidity.size() && timerValidity[handle]) {
        timers[handle].Stop();
    }
}

void TimerManager::ResetTimer(TimerHandle handle) {
    if (handle < timerValidity.size() && timerValidity[handle]) {
        timers[handle].Reset();
    }
}

float TimerManager::GetElapsedTime(TimerHandle handle) const {
    if (handle < timerValidity.size() && timerValidity[handle]) {
        return timers[handle].GetElapsedTime();
    }
    return 0.0f;
}

bool TimerManager::IsTimerRunning(TimerHandle handle) const {
    if (handle < timerValidity.size()) {
        return timerValidity[handle] && timers[handle].IsRunning();
    }
    return false;
}

void TimerManager::UpdateAllTimers() {
    // 更新所有計時器（實際應該由外部調用）
}

void TimerManager::Clear() {
    timerValidity.clear();
    timers.clear();
}

// ============================================================================
// TimeManager 實現
// ============================================================================

TimeManager::TimeManager()
    : deltaTime(0.0f)
    , totalTime(0.0f)
    , fixedDeltaTime(0.01667f) // 60 FPS
    , timeScaleFactor(1.0f)
    , targetTimeScaleFactor(1.0f)
    , timeScaleTransitionDuration(0.0f)
    , timeScaleTransitionTimer(0.0f)
    , currentTimeScale(TimeScale::RealTime)
    , isPaused(false)
    , targetFPS(60)
    , currentFPS(60)
    , fpsUpdateTime(0.0f)
    , frameCount(0)
    , timerManager(std::make_unique<TimerManager>())
    , initialized(false)
{
}

TimeManager::~TimeManager() {
    Shutdown();
}

void TimeManager::Initialize() {
    if (initialized) return;
    
    lastFrameTime = std::chrono::high_resolution_clock::now();
    currentTime = lastFrameTime;
    
    initialized = true;
    std::cout << "Time Manager initialized" << std::endl;
}

void TimeManager::Update() {
    if (!initialized) return;
    
    currentTime = std::chrono::high_resolution_clock::now();
    
    // 計算delta time
    auto duration = std::chrono::duration<float>(currentTime - lastFrameTime);
    float rawDeltaTime = duration.count();
    
    // 應用時間刻度
    if (isPaused) {
        deltaTime = 0.0f;
    } else {
        deltaTime = rawDeltaTime * timeScaleFactor;
    }
    
    // 限制最大delta time，防止跳幀
    deltaTime = std::min(deltaTime, 0.1f); // 最大100ms
    
    totalTime += deltaTime;
    lastFrameTime = currentTime;
    
    // 更新時間刻度
    UpdateTimeScale();
    
    // 更新FPS
    UpdateFPS();
    
    // 處理延遲調用
    ProcessDelayedCalls();
    
    // 調用更新回調
    for (auto& callback : updateCallbacks) {
        callback(deltaTime);
    }
}

void TimeManager::Shutdown() {
    if (!initialized) return;
    
    updateCallbacks.clear();
    delayedCalls.clear();
    
    initialized = false;
    std::cout << "Time Manager shutdown complete" << std::endl;
}

void TimeManager::SetTimeScale(TimeScale scale) {
    currentTimeScale = scale;
    
    switch (scale) {
        case TimeScale::RealTime:
            targetTimeScaleFactor = 1.0f;
            break;
        case TimeScale::GameTime:
            targetTimeScaleFactor = 1.0f;
            break;
        case TimeScale::FixedTime:
            targetTimeScaleFactor = 1.0f;
            break;
        case TimeScale::SlowMotion:
            targetTimeScaleFactor = 0.5f;
            break;
        case TimeScale::Paused:
            targetTimeScaleFactor = 0.0f;
            break;
    }
}

void TimeManager::SetTimeScaleFactor(float factor) {
    targetTimeScaleFactor = (factor < 0.0f) ? 0.0f : (factor > 10.0f) ? 10.0f : factor;
}

void TimeManager::SetTimeScaleFactor(float factor, float duration) {
    targetTimeScaleFactor = (factor < 0.0f) ? 0.0f : (factor > 10.0f) ? 10.0f : factor;
    timeScaleTransitionDuration = duration;
    timeScaleTransitionTimer = 0.0f;
}

void TimeManager::ResetTimeScaleFactor() {
    targetTimeScaleFactor = 1.0f;
    timeScaleTransitionDuration = 0.0f;
}

void TimeManager::Pause() {
    isPaused = true;
}

void TimeManager::Resume() {
    isPaused = false;
    lastFrameTime = std::chrono::high_resolution_clock::now();
}

void TimeManager::SetTargetFPS(int fps) {
    targetFPS = (fps < 1) ? 1 : fps;
}

void TimeManager::DelayedCall(float delay, DelayedCallback callback) {
    DelayedCallInfo info;
    info.delay = delay;
    info.timer = 0.0f;
    info.callback = callback;
    
    delayedCalls.push_back(info);
}

TimerHandle TimeManager::CreateTimer() {
    return timerManager->CreateTimer();
}

void TimeManager::DestroyTimer(TimerHandle handle) {
    timerManager->DestroyTimer(handle);
}

void TimeManager::StartTimer(TimerHandle handle) {
    timerManager->StartTimer(handle);
}

void TimeManager::StopTimer(TimerHandle handle) {
    timerManager->StopTimer(handle);
}

float TimeManager::GetElapsedTime(TimerHandle handle) const {
    return timerManager->GetElapsedTime(handle);
}

void TimeManager::RegisterUpdateCallback(TimeCallback callback) {
    updateCallbacks.push_back(callback);
}

void TimeManager::UnregisterUpdateCallback(TimeCallback callback) {
    for (auto it = updateCallbacks.begin(); it != updateCallbacks.end(); ) {
        // Note: This is a simple comparison, for proper comparison we'd need to use a different approach
        // For now, just clear the callback if it matches (using target with a specific lambda)
        // In production, use std::function::target_type or store callbacks differently
        ++it;
    }
}

void TimeManager::PrintStatistics() const {
    std::cout << "=== Time Manager Statistics ===" << std::endl;
    std::cout << "Delta Time: " << deltaTime << "s" << std::endl;
    std::cout << "Total Time: " << totalTime << "s" << std::endl;
    std::cout << "Time Scale: " << timeScaleFactor << "x" << std::endl;
    std::cout << "Current FPS: " << currentFPS << std::endl;
    std::cout << "Target FPS: " << targetFPS << std::endl;
    std::cout << "Paused: " << (isPaused ? "Yes" : "No") << std::endl;
    std::cout << "Delayed Calls: " << delayedCalls.size() << std::endl;
}

void TimeManager::UpdateFPS() {
    frameCount++;
    fpsUpdateTime += deltaTime;
    
    if (fpsUpdateTime >= 1.0f) {
        currentFPS = static_cast<int>(frameCount / fpsUpdateTime);
        frameCount = 0;
        fpsUpdateTime = 0.0f;
    }
}

void TimeManager::ProcessDelayedCalls() {
    for (auto it = delayedCalls.begin(); it != delayedCalls.end(); ) {
        it->timer += deltaTime;
        
        if (it->timer >= it->delay) {
            if (it->callback) {
                it->callback();
            }
            it = delayedCalls.erase(it);
        } else {
            ++it;
        }
    }
}

void TimeManager::UpdateTimeScale() {
    if (timeScaleTransitionDuration > 0.0f) {
        timeScaleTransitionTimer += deltaTime;
        
        if (timeScaleTransitionTimer >= timeScaleTransitionDuration) {
            timeScaleFactor = targetTimeScaleFactor;
            timeScaleTransitionDuration = 0.0f;
            timeScaleTransitionTimer = 0.0f;
        } else {
            // 線性插值
            float t = timeScaleTransitionTimer / timeScaleTransitionDuration;
            timeScaleFactor = timeScaleFactor + (targetTimeScaleFactor - timeScaleFactor) * t;
        }
    } else {
        timeScaleFactor = targetTimeScaleFactor;
    }
}

// ============================================================================
// 全局函數實現
// ============================================================================

bool InitializeTimeManager() {
    if (gTimeManager) {
        return false;
    }
    
    gTimeManager = new TimeManager();
    gTimeManager->Initialize();
    
    return true;
}

void ShutdownTimeManager() {
    if (gTimeManager) {
        delete gTimeManager;
        gTimeManager = nullptr;
    }
}

TimeManager* GetTimeManager() {
    return gTimeManager;
}

} // namespace Potato