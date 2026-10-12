#pragma once

// 兼容 shim:Rendering/*.cpp 使用未定義的 LOG_* 宏(引擎已知問題)。
// 編譯渲染示例時以 -include compat_log.h 注入到 stderr/stdout 的實現。
#include <iostream>

#ifndef LOG_INFO
#define LOG_INFO(msg)    do { std::cout << "[INFO] "  << msg << std::endl; } while (0)
#endif
#ifndef LOG_WARNING
#define LOG_WARNING(msg) do { std::cout << "[WARN] "  << msg << std::endl; } while (0)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(msg)   do { std::cerr << "[ERROR] " << msg << std::endl; } while (0)
#endif
#ifndef LOG_DEBUG
#define LOG_DEBUG(msg)   do { std::cout << "[DEBUG] " << msg << std::endl; } while (0)
#endif
