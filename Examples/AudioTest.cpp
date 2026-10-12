// AudioTest - WAV 載入器加固驗證（無需 OpenAL 裝置,LoadWAV 為純檔案解析）

#include "Audio/AudioSystem.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

using namespace Potato;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

static std::string WriteTemp(const char* name, const char* bytes, size_t n) {
    std::string path = std::string("audiotest_") + name;
    std::ofstream f(path, std::ios::binary);
    f.write(bytes, static_cast<std::streamsize>(n));
    return path;
}

int main() {
    printf("=== Audio WAV Loader Tests ===\n\n");

    OpenALAudioManager mgr; // LoadWAV 不依賴 OpenAL 初始化

    // [1] 過小檔案（10 bytes）→ nullptr
    {
        std::string p = WriteTemp("tiny.wav", "RIFFxxxxxx", 10);
        Check(mgr.LoadAudio(p) == nullptr, "10-byte 檔案被拒絕");
        std::remove(p.c_str());
    }

    // [2] 非 RIFF 內容但夠長 → nullptr
    {
        char junk[64] = {};
        std::memcpy(junk, "NOPE_NOT_A_WAVE", 15);
        std::string p = WriteTemp("junk.wav", junk, sizeof(junk));
        Check(mgr.LoadAudio(p) == nullptr, "非 RIFF magic 被拒絕");
        std::remove(p.c_str());
    }

    // [3] RIFF 但非 WAVE → nullptr
    {
        char h[64] = {};
        std::memcpy(h, "RIFF", 4);
        std::memcpy(h + 8, "XXXX", 4);
        std::string p = WriteTemp("notwave.wav", h, sizeof(h));
        Check(mgr.LoadAudio(p) == nullptr, "RIFF 但非 WAVE 被拒絕");
        std::remove(p.c_str());
    }

    // [4] 合法最小 WAV（44-byte header + 8 bytes payload）→ 成功
    {
        char h[52] = {};
        std::memcpy(h, "RIFF", 4);
        std::memcpy(h + 8, "WAVE", 4);
        std::memcpy(h + 12, "fmt ", 4);
        std::memcpy(h + 36, "data", 4);
        std::string p = WriteTemp("ok.wav", h, sizeof(h));
        AudioData* d = mgr.LoadAudio(p);
        Check(d != nullptr && d->data.size() == 8, "合法 WAV 載入成功（payload=8B）");
        std::remove(p.c_str());
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
