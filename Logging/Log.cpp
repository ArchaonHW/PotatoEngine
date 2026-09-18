#include "Logging/Log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <mutex>

namespace Potato {
namespace Log {

namespace {

std::mutex g_mutex;
std::ofstream g_file;
std::atomic<Level> g_min{Level::Info}; // 讀取在鎖外，atomic 防撕裂

const char* Tag(Level lv) {
    switch (lv) {
    case Level::Debug: return "DEBUG";
    case Level::Info:  return "INFO";
    case Level::Warn:  return "WARN";
    case Level::Error: return "ERROR";
    }
    return "?";
}

void Emit(Level lv, const char* fmt, va_list args) {
    char buf[1024];
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    buf[sizeof(buf) - 1] = '\0';

    std::lock_guard<std::mutex> lock(g_mutex);
    std::fprintf(stderr, "[%s] %s\n", Tag(lv), buf);
    if (g_file.is_open()) {
        g_file << '[' << Tag(lv) << "] " << buf << '\n';
        g_file.flush();
    }
}

} // namespace

void Error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Emit(Level::Error, fmt, args);
    va_end(args);
}

void Warn(const char* fmt, ...) {
    if (g_min > Level::Warn) return;
    va_list args;
    va_start(args, fmt);
    Emit(Level::Warn, fmt, args);
    va_end(args);
}

void Info(const char* fmt, ...) {
    if (g_min > Level::Info) return;
    va_list args;
    va_start(args, fmt);
    Emit(Level::Info, fmt, args);
    va_end(args);
}

#ifndef NDEBUG
void Debug(const char* fmt, ...) {
    if (g_min > Level::Debug) return;
    va_list args;
    va_start(args, fmt);
    Emit(Level::Debug, fmt, args);
    va_end(args);
}
#endif

void SetMinLevel(Level level) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_min = level;
}

bool SetFile(const char* path) {
    if (!path || !*path) {
        return false;
    }
    std::ofstream f(path, std::ios::app);
    if (!f) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file.is_open()) {
        g_file.close();
    }
    g_file = std::move(f);
    return true;
}

void CloseFile() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file.is_open()) {
        g_file.close();
    }
}

} // namespace Log
} // namespace Potato
