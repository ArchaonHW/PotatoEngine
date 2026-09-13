#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector2.h"
#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace Potato {

/**
 * 平台類型
 */
enum class Platform {
    Windows,
    Linux,
    macOS,
    Android,
    iOS,
    Unknown
};

/**
 * 窗口模式
 */
enum class WindowMode {
    Windowed,
    Fullscreen,
    Borderless
};

/**
 * 窗口配置
 */
struct WindowConfig {
    int width;
    int height;
    std::string title;
    WindowMode mode;
    bool resizable;
    bool decorated;
    bool vsync;
    
    WindowConfig()
        : width(1280)
        , height(720)
        , title("Potato Engine")
        , mode(WindowMode::Windowed)
        , resizable(true)
        , decorated(true)
        , vsync(true)
    {
    }
};

/**
 * 窗口事件類型
 */
enum class WindowEventType {
    Close,
    Resize,
    Move,
    Focus,
    Minimize,
    Maximize,
    Restore
};

/**
 * 窗口事件
 */
struct WindowEvent {
    WindowEventType type;
    int data1;
    int data2;
    
    WindowEvent()
        : type(WindowEventType::Close)
        , data1(0)
        , data2(0)
    {
    }
};

/**
 * 窗口回調類型
 */
using WindowCallback = std::function<void(const WindowEvent&)>;

/**
 * 窗口接口
 */
class IWindow {
public:
    virtual ~IWindow() = default;
    
    virtual bool Initialize(const WindowConfig& config) = 0;
    virtual void Shutdown() = 0;
    
    virtual void Update() = 0;
    
    virtual void Show() = 0;
    virtual void Hide() = 0;
    virtual void Minimize() = 0;
    virtual void Maximize() = 0;
    virtual void Restore() = 0;
    
    virtual void SetTitle(const std::string& title) = 0;
    virtual std::string GetTitle() const = 0;
    
    virtual void SetSize(int width, int height) = 0;
    virtual void GetSize(int& width, int& height) const = 0;
    
    virtual void SetPosition(int x, int y) = 0;
    virtual void GetPosition(int& x, int& y) const = 0;
    
    virtual void SetMode(WindowMode mode) = 0;
    virtual WindowMode GetMode() const = 0;
    
    virtual bool ShouldClose() const = 0;
    virtual void* GetNativeHandle() const = 0;
    
    virtual void RegisterCallback(WindowCallback callback) = 0;
};

/**
 * 線程接口
 */
class IThread {
public:
    virtual ~IThread() = default;
    
    virtual void Start() = 0;
    virtual void Join() = 0;
    virtual void Detach() = 0;
    
    virtual bool IsRunning() const = 0;
    virtual void Sleep(uint32 milliseconds) = 0;
};

/**
 * 互斥鎖接口
 */
class IMutex {
public:
    virtual ~IMutex() = default;
    
    virtual void Lock() = 0;
    virtual void Unlock() = 0;
    virtual bool TryLock() = 0;
};

/**
 * 線程條件變量接口
 */
class IConditionVariable {
public:
    virtual ~IConditionVariable() = default;
    
    virtual void Wait(IMutex& mutex) = 0;
    virtual void NotifyOne() = 0;
    virtual void NotifyAll() = 0;
    virtual bool WaitFor(IMutex& mutex, uint32 milliseconds) = 0;
};

/**
 * 平台信息
 */
struct PlatformInfo {
    Platform platform;
    std::string platformName;
    std::string osVersion;
    std::string architecture;
    int cpuCount;
    size_t totalMemory;
    size_t availableMemory;
    
    PlatformInfo()
        : platform(Platform::Unknown)
        , cpuCount(0)
        , totalMemory(0)
        , availableMemory(0)
    {
    }
};

/**
 * 平台管理器接口
 */
class IPlatformManager {
public:
    virtual ~IPlatformManager() = default;
    
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    
    virtual PlatformInfo GetPlatformInfo() const = 0;
    
    virtual IWindow* CreateWindow(const WindowConfig& config) = 0;
    virtual void DestroyWindow(IWindow* window) = 0;
    
    virtual IThread* CreateThread(std::function<void()> task) = 0;
    virtual void DestroyThread(IThread* thread) = 0;
    
    virtual IMutex* CreateMutex() = 0;
    virtual void DestroyMutex(IMutex* mutex) = 0;
    
    virtual IConditionVariable* CreateConditionVariable() = 0;
    virtual void DestroyConditionVariable(IConditionVariable* cv) = 0;
    
    virtual void* LoadLibrary(const std::string& path) = 0;
    virtual void UnloadLibrary(void* handle) = 0;
    virtual void* GetProcAddress(void* handle, const std::string& name) = 0;
    
    virtual std::string GetExecutablePath() const = 0;
    virtual std::string GetWorkingDirectory() const = 0;
    virtual bool SetWorkingDirectory(const std::string& path) = 0;
    
    virtual uint64 GetTickCount() const = 0;
    virtual uint64 GetPerformanceCounter() const = 0;
    virtual uint64 GetPerformanceFrequency() const = 0;
};

/**
 * GLFW 窗口實現
 */
class GLFWWindow : public IWindow {
public:
    GLFWWindow();
    ~GLFWWindow() override;
    
    bool Initialize(const WindowConfig& config) override;
    void Shutdown() override;
    
    void Update() override;
    
    void Show() override;
    void Hide() override;
    void Minimize() override;
    void Maximize() override;
    void Restore() override;
    
    void SetTitle(const std::string& title) override;
    std::string GetTitle() const override;
    
    void SetSize(int width, int height) override;
    void GetSize(int& width, int& height) const override;
    
    void SetPosition(int x, int y) override;
    void GetPosition(int& x, int& y) const override;
    
    void SetMode(WindowMode mode) override;
    WindowMode GetMode() const override;
    
    bool ShouldClose() const override;
    void* GetNativeHandle() const override;
    
    void RegisterCallback(WindowCallback callback) override;
    
private:
    void SetupGLFWCallbacks();
    
private:
    void* windowHandle;
    WindowConfig config;
    WindowMode currentMode;
    std::vector<WindowCallback> callbacks;
    bool initialized;
};

/**
 * 標準線程實現
 */
class StandardThread : public IThread {
public:
    StandardThread(std::function<void()> task);
    ~StandardThread() override;
    
    void Start() override;
    void Join() override;
    void Detach() override;
    
    bool IsRunning() const override;
    void Sleep(uint32 milliseconds) override;
    
private:
    std::function<void()> task;
    std::thread thread;
    bool running;
};

/**
 * 標準互斥鎖實現
 */
class StandardMutex : public IMutex {
public:
    StandardMutex();
    ~StandardMutex() override;
    
    void Lock() override;
    void Unlock() override;
    bool TryLock() override;
    
private:
    std::mutex mutex;
};

/**
 * 標準條件變量實現
 */
class StandardConditionVariable : public IConditionVariable {
public:
    StandardConditionVariable();
    ~StandardConditionVariable() override;
    
    void Wait(IMutex& mutex) override;
    void NotifyOne() override;
    void NotifyAll() override;
    bool WaitFor(IMutex& mutex, uint32 milliseconds) override;
    
private:
    std::condition_variable cv;
};

/**
 * 標準平台管理器實現
 */
class StandardPlatformManager : public IPlatformManager {
public:
    StandardPlatformManager();
    ~StandardPlatformManager() override;
    
    bool Initialize() override;
    void Shutdown() override;
    
    PlatformInfo GetPlatformInfo() const override;
    
    IWindow* CreateWindow(const WindowConfig& config) override;
    void DestroyWindow(IWindow* window) override;
    
    IThread* CreateThread(std::function<void()> task) override;
    void DestroyThread(IThread* thread) override;
    
    IMutex* CreateMutex() override;
    void DestroyMutex(IMutex* mutex) override;
    
    IConditionVariable* CreateConditionVariable() override;
    void DestroyConditionVariable(IConditionVariable* cv) override;
    
    void* LoadLibrary(const std::string& path) override;
    void UnloadLibrary(void* handle) override;
    void* GetProcAddress(void* handle, const std::string& name) override;
    
    std::string GetExecutablePath() const override;
    std::string GetWorkingDirectory() const override;
    bool SetWorkingDirectory(const std::string& path) override;
    
    uint64 GetTickCount() const override;
    uint64 GetPerformanceCounter() const override;
    uint64 GetPerformanceFrequency() const override;
    
private:
    PlatformInfo platformInfo;
    std::vector<IWindow*> windows;
    std::vector<IThread*> threads;
    std::vector<IMutex*> mutexes;
    std::vector<IConditionVariable*> conditionVariables;
    
    bool initialized;
};

/**
 * 平台管理器（全局單例）
 */
class PlatformManager {
public:
    static PlatformManager& GetInstance();
    
    IPlatformManager* GetImplementation() { return platformManager.get(); }
    void SetImplementation(UniquePtr<IPlatformManager> impl);
    
    // 便捷方法
    bool Initialize();
    void Shutdown();
    
    PlatformInfo GetPlatformInfo();
    
    IWindow* CreateWindow(const WindowConfig& config);
    IThread* CreateThread(std::function<void()> task);
    IMutex* CreateMutex();
    IConditionVariable* CreateConditionVariable();
    
private:
    PlatformManager();
    ~PlatformManager();
    
    UniquePtr<IPlatformManager> platformManager;
};

// 全局平台管理器
extern PlatformManager* gPlatformManager;

/**
 * 初始化全局平台管理器
 */
bool InitializePlatformManager();

/**
 * 關閉全局平台管理器
 */
void ShutdownPlatformManager();

/**
 * 獲取全局平台管理器
 */
PlatformManager* GetPlatformManager();

} // namespace Potato

// 便捷宏
#define GET_PLATFORM() Potato::GetPlatformManager()