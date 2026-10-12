#include "PlatformSystem.h"
#include "GLFWSharedContext.h"
#include "Input/InputManager.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <libloaderapi.h>
// Win32 宏會改寫下方同名成員函數定義,必須取消
#undef CreateWindow
#undef CreateMutex
#undef LoadLibrary
#elif __linux__
#include <unistd.h>
#include <sys/sysinfo.h>
#include <dlfcn.h>
#elif __APPLE__
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <dlfcn.h>
#endif

namespace Potato {

// ============================================================================
// GLFWWindow 實現
// ============================================================================

GLFWWindow::GLFWWindow()
    : windowHandle(nullptr)
    , currentMode(WindowMode::Windowed)
    , initialized(false)
{
}

GLFWWindow::~GLFWWindow() {
    Shutdown();
}

bool GLFWWindow::Initialize(const WindowConfig& cfg) {
    if (initialized) {
        return true;
    }
    
    config = cfg;
    
    // 設置 GLFW 窗口提示
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, config.decorated ? GLFW_TRUE : GLFW_FALSE);
    
    // 設置窗口模式
    GLFWmonitor* monitor = nullptr;
    if (config.mode == WindowMode::Fullscreen) {
        monitor = glfwGetPrimaryMonitor();
    }
    
    // 創建窗口
    windowHandle = glfwCreateWindow(
        config.width,
        config.height,
        config.title.c_str(),
        monitor,
        nullptr
    );
    
    if (!windowHandle) {
        std::cerr << "錯誤: Failed to create GLFW window" << std::endl;
        return false;
    }
    
    // 設置 VSync
    if (config.vsync) {
        glfwSwapInterval(1);
    }
    
    SetupGLFWCallbacks();
    
    initialized = true;
    std::cout << "✓ GLFW window initialized: " << config.title << std::endl;
    
    return true;
}

void GLFWWindow::Shutdown() {
    if (!initialized) {
        return;
    }
    
    if (windowHandle) {
        // 統一走 DestroyGLFWWindow：通知 input owner、清 user pointer、
        // 釋放共享 context,避免 raw glfwDestroyWindow 留下 stale entry
        DestroyGLFWWindow(static_cast<GLFWwindow*>(windowHandle));
        windowHandle = nullptr;
    }
    
    initialized = false;
    std::cout << "✓ GLFW window shutdown" << std::endl;
}

void GLFWWindow::Update() {
    if (windowHandle) {
        glfwPollEvents();
    }
}

void GLFWWindow::Show() {
    if (windowHandle) {
        glfwShowWindow(static_cast<GLFWwindow*>(windowHandle));
    }
}

void GLFWWindow::Hide() {
    if (windowHandle) {
        glfwHideWindow(static_cast<GLFWwindow*>(windowHandle));
    }
}

void GLFWWindow::Minimize() {
    if (windowHandle) {
        glfwIconifyWindow(static_cast<GLFWwindow*>(windowHandle));
    }
}

void GLFWWindow::Maximize() {
    if (windowHandle) {
        glfwMaximizeWindow(static_cast<GLFWwindow*>(windowHandle));
    }
}

void GLFWWindow::Restore() {
    if (windowHandle) {
        glfwRestoreWindow(static_cast<GLFWwindow*>(windowHandle));
    }
}

void GLFWWindow::SetTitle(const std::string& title) {
    config.title = title;
    if (windowHandle) {
        glfwSetWindowTitle(static_cast<GLFWwindow*>(windowHandle), title.c_str());
    }
}

std::string GLFWWindow::GetTitle() const {
    return config.title;
}

void GLFWWindow::SetSize(int width, int height) {
    config.width = width;
    config.height = height;
    if (windowHandle) {
        glfwSetWindowSize(static_cast<GLFWwindow*>(windowHandle), width, height);
    }
}

void GLFWWindow::GetSize(int& width, int& height) const {
    if (windowHandle) {
        glfwGetWindowSize(static_cast<GLFWwindow*>(windowHandle), &width, &height);
    } else {
        width = config.width;
        height = config.height;
    }
}

void GLFWWindow::SetPosition(int x, int y) {
    if (windowHandle) {
        glfwSetWindowPos(static_cast<GLFWwindow*>(windowHandle), x, y);
    }
}

void GLFWWindow::GetPosition(int& x, int& y) const {
    if (windowHandle) {
        glfwGetWindowPos(static_cast<GLFWwindow*>(windowHandle), &x, &y);
    } else {
        x = 0;
        y = 0;
    }
}

void GLFWWindow::SetMode(WindowMode mode) {
    config.mode = mode;
    // 簡化實現：需要重新創建窗口
}

WindowMode GLFWWindow::GetMode() const {
    return currentMode;
}

bool GLFWWindow::ShouldClose() const {
    if (windowHandle) {
        return glfwWindowShouldClose(static_cast<GLFWwindow*>(windowHandle));
    }
    return false;
}

void* GLFWWindow::GetNativeHandle() const {
    return windowHandle;
}

void GLFWWindow::RegisterCallback(WindowCallback callback) {
    callbacks.push_back(callback);
}

void GLFWWindow::OnCloseEvent() {
    WindowEvent event;
    event.type = WindowEventType::Close;
    for (const auto& callback : callbacks) {
        callback(event);
    }
}

void GLFWWindow::OnResizeEvent(int width, int height) {
    WindowEvent event;
    event.type = WindowEventType::Resize;
    event.data1 = width;
    event.data2 = height;
    for (const auto& callback : callbacks) {
        callback(event);
    }
}

void GLFWWindow::SetupGLFWCallbacks() {
    GLFWwindow* window = static_cast<GLFWwindow*>(windowHandle);
    
    // 註冊到共享 context 並安裝統一 dispatch 回調：
    // 與 GLFWInputManager 共用同一個 window user pointer，
    // 且 glfwSetWindowSizeCallback 只能設一次，兩邊都需收到事件
    GetOrCreateGLFWContext(window).windowOwner = this;
    InstallGLFWDispatchCallbacks(window);
}

// ============================================================================
// StandardThread 實現
// ============================================================================

StandardThread::StandardThread(std::function<void()> t)
    : task(t)
    , running(false)
{
}

StandardThread::~StandardThread() {
    if (thread.joinable()) {
        thread.join();
    }
}

void StandardThread::Start() {
    running = true;
    thread = std::thread([this]() {
        task();
        running = false;
    });
}

void StandardThread::Join() {
    if (thread.joinable()) {
        thread.join();
    }
}

void StandardThread::Detach() {
    thread.detach();
}

bool StandardThread::IsRunning() const {
    return running;
}

void StandardThread::Sleep(uint32 milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

// ============================================================================
// StandardMutex 實現
// ============================================================================

StandardMutex::StandardMutex() {
}

StandardMutex::~StandardMutex() {
}

void StandardMutex::Lock() {
    mutex.lock();
}

void StandardMutex::Unlock() {
    mutex.unlock();
}

bool StandardMutex::TryLock() {
    return mutex.try_lock();
}

// ============================================================================
// StandardConditionVariable 實現
// ============================================================================

StandardConditionVariable::StandardConditionVariable() {
}

StandardConditionVariable::~StandardConditionVariable() {
}

void StandardConditionVariable::Wait(IMutex& mutex) {
    StandardMutex* stdMutex = static_cast<StandardMutex*>(&mutex);
    // adopt_lock：呼叫者依 cv 契約已持有 mutex；
    // 用預設建構的 unique_lock 會對已鎖定的 mutex 再次 lock() 造成自我死結
    std::unique_lock<std::mutex> lock(stdMutex->mutex, std::adopt_lock);
    cv.wait(lock);
    // wait 返回後 mutex 已重新持有；release 讓 unique_lock 解構時不 unlock,
    // 所有權交還呼叫者(否則呼叫者稍後 Unlock() 會變成 double-unlock UB)
    lock.release();
}

void StandardConditionVariable::NotifyOne() {
    cv.notify_one();
}

void StandardConditionVariable::NotifyAll() {
    cv.notify_all();
}

bool StandardConditionVariable::WaitFor(IMutex& mutex, uint32 milliseconds) {
    StandardMutex* stdMutex = static_cast<StandardMutex*>(&mutex);
    std::unique_lock<std::mutex> lock(stdMutex->mutex, std::adopt_lock);
    bool noTimeout = cv.wait_for(lock, std::chrono::milliseconds(milliseconds)) == std::cv_status::no_timeout;
    lock.release(); // 同上：mutex 所有權交還呼叫者
    return noTimeout;
}

// ============================================================================
// StandardPlatformManager 實現
// ============================================================================

StandardPlatformManager::StandardPlatformManager()
    : initialized(false)
{
}

StandardPlatformManager::~StandardPlatformManager() {
    Shutdown();
}

bool StandardPlatformManager::Initialize() {
    if (initialized) {
        return true;
    }
    
    std::cout << "初始化 Standard Platform Manager..." << std::endl;
    
    // 初始化 GLFW
    if (!glfwInit()) {
        std::cerr << "錯誤: Failed to initialize GLFW" << std::endl;
        return false;
    }
    
    // 獲取平台信息
    platformInfo.platform = Platform::Unknown;
    
#ifdef _WIN32
    platformInfo.platform = Platform::Windows;
    platformInfo.platformName = "Windows";
    platformInfo.osVersion = "10.0";
    
    // 獲取 CPU 信息
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    platformInfo.cpuCount = sysInfo.dwNumberOfProcessors;
    
    // 獲取內存信息
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    platformInfo.totalMemory = memInfo.ullTotalPhys;
    platformInfo.availableMemory = memInfo.ullAvailPhys;
    
    platformInfo.architecture = "x64";
    
#elif __linux__
    platformInfo.platform = Platform::Linux;
    platformInfo.platformName = "Linux";
    
    // 獲取 CPU 信息
    platformInfo.cpuCount = sysconf(_SC_NPROCESSORS_ONLN);
    
    // 獲取內存信息
    struct sysinfo sysInfo;
    sysinfo(&sysInfo);
    platformInfo.totalMemory = sysInfo.totalram * sysInfo.mem_unit;
    platformInfo.availableMemory = sysInfo.freeram * sysInfo.mem_unit;
    
    platformInfo.architecture = "x64";
    
#elif __APPLE__
    platformInfo.platform = Platform::macOS;
    platformInfo.platformName = "macOS";
    
    // 獲取 CPU 信息
    int numCPU;
    size_t size = sizeof(numCPU);
    sysctlbyname("hw.ncpu", &numCPU, &size, nullptr, 0);
    platformInfo.cpuCount = numCPU;
    
    // 獲取內存信息
    int64_t memSize;
    size = sizeof(memSize);
    sysctlbyname("hw.memsize", &memSize, &size, nullptr, 0);
    platformInfo.totalMemory = memSize;
    
    platformInfo.architecture = "x64";
#endif
    
    initialized = true;
    std::cout << "✓ Standard Platform Manager initialized successfully" << std::endl;
    std::cout << "  Platform: " << platformInfo.platformName << std::endl;
    std::cout << "  CPU Count: " << platformInfo.cpuCount << std::endl;
    std::cout << "  Total Memory: " << (platformInfo.totalMemory / (1024 * 1024)) << " MB" << std::endl;
    
    return true;
}

void StandardPlatformManager::Shutdown() {
    if (!initialized) {
        return;
    }
    
    std::cout << "關閉 Standard Platform Manager..." << std::endl;
    
    // 清理所有資源
    for (auto window : windows) {
        delete window;
    }
    windows.clear();
    
    for (auto thread : threads) {
        delete thread;
    }
    threads.clear();
    
    for (auto mutex : mutexes) {
        delete mutex;
    }
    mutexes.clear();
    
    for (auto cv : conditionVariables) {
        delete cv;
    }
    conditionVariables.clear();
    
    glfwTerminate();
    
    initialized = false;
    std::cout << "✓ Standard Platform Manager shutdown complete" << std::endl;
}

PlatformInfo StandardPlatformManager::GetPlatformInfo() const {
    return platformInfo;
}

IWindow* StandardPlatformManager::CreateWindow(const WindowConfig& config) {
    auto window = new GLFWWindow();
    if (window->Initialize(config)) {
        windows.push_back(window);
        return window;
    }
    delete window;
    return nullptr;
}

void StandardPlatformManager::DestroyWindow(IWindow* window) {
    auto it = std::find(windows.begin(), windows.end(), window);
    if (it != windows.end()) {
        windows.erase(it);
        delete window;
    }
}

IThread* StandardPlatformManager::CreateThread(std::function<void()> task) {
    auto thread = new StandardThread(task);
    threads.push_back(thread);
    return thread;
}

void StandardPlatformManager::DestroyThread(IThread* thread) {
    auto it = std::find(threads.begin(), threads.end(), thread);
    if (it != threads.end()) {
        threads.erase(it);
        delete thread;
    }
}

IMutex* StandardPlatformManager::CreateMutex() {
    auto mutex = new StandardMutex();
    mutexes.push_back(mutex);
    return mutex;
}

void StandardPlatformManager::DestroyMutex(IMutex* mutex) {
    auto it = std::find(mutexes.begin(), mutexes.end(), mutex);
    if (it != mutexes.end()) {
        mutexes.erase(it);
        delete mutex;
    }
}

IConditionVariable* StandardPlatformManager::CreateConditionVariable() {
    auto cv = new StandardConditionVariable();
    conditionVariables.push_back(cv);
    return cv;
}

void StandardPlatformManager::DestroyConditionVariable(IConditionVariable* cv) {
    auto it = std::find(conditionVariables.begin(), conditionVariables.end(), cv);
    if (it != conditionVariables.end()) {
        conditionVariables.erase(it);
        delete cv;
    }
}

void* StandardPlatformManager::LoadLibrary(const std::string& path) {
    if (path.empty()) {
        return nullptr;
    }
#ifdef _WIN32
    // 限制 DLL 搜尋路徑，防止 DLL 搜尋順序劫持 (DLL search-order hijacking)
    return LoadLibraryExA(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#elif __linux__
    return dlopen(path.c_str(), RTLD_LAZY);
#elif __APPLE__
    return dlopen(path.c_str(), RTLD_LAZY);
#else
    return nullptr;
#endif
}

void StandardPlatformManager::UnloadLibrary(void* handle) {
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#elif __linux__ || __APPLE__
    dlclose(handle);
#endif
}

void* StandardPlatformManager::GetProcAddress(void* handle, const std::string& name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(handle), name.c_str()));
#elif __linux__ || __APPLE__
    return dlsym(handle, name.c_str());
#else
    return nullptr;
#endif
}

std::string StandardPlatformManager::GetExecutablePath() const {
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    return std::string(path);
#elif __linux__
    char path[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
    return std::string(path, (count > 0) ? count : 0);
#elif __APPLE__
    char path[PATH_MAX];
    uint32_t size = PATH_MAX;
    if (_NSGetExecutablePath(path, &size) == 0) {
        return std::string(path);
    }
    return "";
#else
    return "";
#endif
}

std::string StandardPlatformManager::GetWorkingDirectory() const {
    std::filesystem::path cwd = std::filesystem::current_path();
    return cwd.string();
}

bool StandardPlatformManager::SetWorkingDirectory(const std::string& path) {
    try {
        std::filesystem::current_path(path);
        return true;
    } catch (...) {
        return false;
    }
}

uint64 StandardPlatformManager::GetTickCount() const {
    return static_cast<uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count());
}

uint64 StandardPlatformManager::GetPerformanceCounter() const {
    return static_cast<uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count());
}

uint64 StandardPlatformManager::GetPerformanceFrequency() const {
    return 1000000000; // 納秒
}

// ============================================================================
// PlatformManager 實現
// ============================================================================

PlatformManager::PlatformManager() {
}

PlatformManager::~PlatformManager() {
    if (platformManager) {
        platformManager->Shutdown();
    }
}

PlatformManager& PlatformManager::GetInstance() {
    static PlatformManager instance;
    return instance;
}

void PlatformManager::SetImplementation(std::unique_ptr<IPlatformManager> impl) {
    platformManager = std::move(impl);
}

bool PlatformManager::Initialize() {
    if (!platformManager) {
        std::cerr << "錯誤: No platform manager implementation set" << std::endl;
        return false;
    }
    return platformManager->Initialize();
}

void PlatformManager::Shutdown() {
    if (platformManager) {
        platformManager->Shutdown();
    }
}

PlatformInfo PlatformManager::GetPlatformInfo() {
    if (platformManager) {
        return platformManager->GetPlatformInfo();
    }
    return PlatformInfo();
}

IWindow* PlatformManager::CreateWindow(const WindowConfig& config) {
    if (platformManager) {
        return platformManager->CreateWindow(config);
    }
    return nullptr;
}

void PlatformManager::DestroyWindow(IWindow* window) {
    if (platformManager) {
        platformManager->DestroyWindow(window);
    }
}

IThread* PlatformManager::CreateThread(std::function<void()> task) {
    if (platformManager) {
        return platformManager->CreateThread(task);
    }
    return nullptr;
}

void PlatformManager::DestroyThread(IThread* thread) {
    if (platformManager) {
        platformManager->DestroyThread(thread);
    }
}

IMutex* PlatformManager::CreateMutex() {
    if (platformManager) {
        return platformManager->CreateMutex();
    }
    return nullptr;
}

void PlatformManager::DestroyMutex(IMutex* mutex) {
    if (platformManager) {
        platformManager->DestroyMutex(mutex);
    }
}

IConditionVariable* PlatformManager::CreateConditionVariable() {
    if (platformManager) {
        return platformManager->CreateConditionVariable();
    }
    return nullptr;
}

void PlatformManager::DestroyConditionVariable(IConditionVariable* cv) {
    if (platformManager) {
        platformManager->DestroyConditionVariable(cv);
    }
}

// ============================================================================
// 全局函數
// ============================================================================

PlatformManager* gPlatformManager = nullptr;

bool InitializePlatformManager() {
    if (!gPlatformManager) {
        gPlatformManager = &PlatformManager::GetInstance();
    }
    
    // 設置標準平台管理器實現
    auto standardManager = std::make_unique<StandardPlatformManager>();
    gPlatformManager->SetImplementation(std::move(standardManager));
    
    return gPlatformManager->Initialize();
}

void ShutdownPlatformManager() {
    if (gPlatformManager) {
        gPlatformManager->Shutdown();
    }
}

PlatformManager* GetPlatformManager() {
    if (!gPlatformManager) {
        gPlatformManager = &PlatformManager::GetInstance();
    }
    return gPlatformManager;
}

} // namespace Potato