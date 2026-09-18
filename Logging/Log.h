#pragma once

// Potato::Log — B.1 統一日誌基建（架構 §Cross-cutting/Logging）。
//
// 與 Logging/Logger 的分工：Logger 是引擎/IDE 的重型系統
// （async queue、需 Initialize、未初始化時巨集靜默忽略）；
// Potato::Log 是恆可用的單 TU 輕量版——免初始化、stderr
// 恆輸出、可選檔案 sink，供 Gameplay/Campaign 層的降級與
// 拒絕路徑使用（Step 5 錯誤處理規則的記錄端點）。
//
// 級別語義：
//   Error — 致命：schema 不符/檔案損毀/版本未知（拒絕路徑）
//   Warn  — 可回復：未知列舉/缺可選欄位（降級預設值路徑）
//   Info  — 一般事件
//   Debug — 熱路徑除錯；NDEBUG 下編譯期剔除為 no-op
//
// 用法：
//   Potato::Log::Warn("template '%s': unknown class '%s'", id, cls);
//   Potato::Log::SetFile("run.log");   // 可選檔案 sink
//   Potato::Log::Error("save failed: %s", path);
//
// 執行緒安全（內部 mutex）；格式化走 vsnprintf 有界緩衝
// （禁用清單的 sprintf/vsprintf 不用）。

namespace Potato {
namespace Log {

enum class Level { Debug, Info, Warn, Error };

void Error(const char* fmt, ...);
void Warn(const char* fmt, ...);
void Info(const char* fmt, ...);

#ifdef NDEBUG
// Debug：release 建置編譯期剔除——熱路徑零成本
inline void Debug(const char*, ...) {}
#else
void Debug(const char* fmt, ...);
#endif

// 最小輸出級別（預設 Info——Warn/Error 恆過）
void SetMinLevel(Level level);

// 可選檔案 sink：開啟成功回 true；path 為空或開檔失敗回 false。
// 重複呼叫先關舊檔再開新檔；CloseFile 關閉之。
bool SetFile(const char* path);
void CloseFile();

} // namespace Log
} // namespace Potato
