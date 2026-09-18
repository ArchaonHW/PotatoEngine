// LogTest — B.1 Potato::Log 無頭驗證
// 覆蓋：stderr 恆輸出（捕獲驗證）、printf 格式化、檔案 sink、
// 級別過濾、Debug 存在性（NDEBUG 下編譯期剔除由 build 保證）。

#include "Logging/Log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#define POTATO_DUP _dup
#define POTATO_DUP2 _dup2
#define POTATO_CLOSE _close
#define POTATO_FILENO _fileno
static int OpenTmp(const char* p) {
    int fd = -1;
    _sopen_s(&fd, p, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY,
             _SH_DENYNO, _S_IREAD | _S_IWRITE);
    return fd;
}
#else
#include <unistd.h>
#include <sys/stat.h>
#define POTATO_DUP dup
#define POTATO_DUP2 dup2
#define POTATO_CLOSE close
#define POTATO_FILENO fileno
static int OpenTmp(const char* p) {
    return open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
}
#endif

static int failures = 0;
static void Check(bool cond, const char* name) {
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        printf("  [FAIL] %s\n", name);
        ++failures;
    }
}

// 把 stderr 的底層 fd 重導到暫存檔，跑 fn()，讀回內容後還原。
// fd 層 dup2 保留 stderr FILE* 本體——比 freopen("CON") 跨平台。
template <typename F>
static std::string CaptureStderr(const char* tmp, F&& fn) {
    std::fflush(stderr);
    int saved = POTATO_DUP(POTATO_FILENO(stderr));
    int sinkFd = OpenTmp(tmp);
    if (sinkFd >= 0) {
        POTATO_DUP2(sinkFd, POTATO_FILENO(stderr));
        POTATO_CLOSE(sinkFd);
    }
    fn();
    std::fflush(stderr);
    if (saved >= 0) {
        POTATO_DUP2(saved, POTATO_FILENO(stderr));
        POTATO_CLOSE(saved);
    }

    std::ifstream f(tmp, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    printf("=== Potato::Log Tests (B.1) ===\n");
    const char* capPath = "potato_log_capture.tmp";
    const char* sinkPath = "potato_log_sink.tmp";

    // ---- [1] stderr 恆輸出 + 級別標籤 ----
    printf("\n[1] stderr 輸出\n");
    {
        std::string out = CaptureStderr(capPath, [] {
            Potato::Log::Warn("warn %d", 42);
            Potato::Log::Error("err %s", "x");
        });
        Check(out.find("[WARN] warn 42") != std::string::npos,
              "Warn 帶標籤+格式化");
        Check(out.find("[ERROR] err x") != std::string::npos,
              "Error 帶標籤+格式化");
        Check(out.find("warn 42") != std::string::npos &&
              out.find("%d") == std::string::npos,
              "printf 參數正確替換");
    }

    // ---- [2] 級別過濾 ----
    printf("\n[2] SetMinLevel 過濾\n");
    {
        Potato::Log::SetMinLevel(Potato::Log::Level::Error);
        std::string out = CaptureStderr(capPath, [] {
            Potato::Log::Info("hidden info");
            Potato::Log::Warn("hidden warn");
            Potato::Log::Error("shown err");
        });
        Check(out.find("hidden") == std::string::npos,
              "Info/Warn 被 Error 地板濾掉");
        Check(out.find("shown err") != std::string::npos,
              "Error 恆過");
        Potato::Log::SetMinLevel(Potato::Log::Level::Info); // 還原
    }

    // ---- [3] 檔案 sink ----
    printf("\n[3] 檔案 sink\n");
    {
        Check(Potato::Log::SetFile(sinkPath), "SetFile 開檔成功");
        Potato::Log::Warn("to file %d", 7);
        Potato::Log::CloseFile();
        std::ifstream f(sinkPath, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        const std::string content = ss.str();
        Check(content.find("[WARN] to file 7") != std::string::npos,
              "檔案收到同格式訊息");
        Check(!Potato::Log::SetFile(""), "空路徑拒絕");
        Check(!Potato::Log::SetFile("no_such_dir_xyz/f.log"),
              "壞路徑回 false 不崩潰");
    }

    // ---- [4] Debug 存在性（Debug build 有輸出）----
    printf("\n[4] Debug 級\n");
    {
        Potato::Log::SetMinLevel(Potato::Log::Level::Debug);
        std::string out = CaptureStderr(capPath, [] {
            Potato::Log::Debug("dbg %d", 1);
        });
        Potato::Log::SetMinLevel(Potato::Log::Level::Info); // 還原
#ifdef NDEBUG
        Check(out.find("dbg") == std::string::npos,
              "NDEBUG 下 Debug 剔除為 no-op");
#else
        Check(out.find("[DEBUG] dbg 1") != std::string::npos,
              "Debug build 下 Debug 有輸出");
#endif
    }

    std::remove(capPath);
    std::remove(sinkPath);
    printf("\n=== 結果: %s ===\n", failures == 0 ? "全部 PASS" : "有 FAIL");
    return failures == 0 ? 0 : 1;
}
