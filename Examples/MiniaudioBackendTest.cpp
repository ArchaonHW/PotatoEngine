// MiniaudioBackendTest - F-4 miniaudio 後端降級契約驗證
// 全部用 ma_backend_null（輸出管線完整但不吃真裝置）+ 未初始化路徑,
// 無頭 CI 可跑;驗收:無音檔/無裝置/初始化失敗皆靜音不崩潰

#include "Audio/MiniaudioBackend.h"
#include "Audio/AudioCues.h"
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

// 寫出合法 16-bit PCM WAV(miniaudio decoder 會真驗 chunk)
static std::string WriteWAV(const char* name, int channels,
                            int sampleRate, int frames) {
    std::string path = std::string("matest_") + name;
    const int dataBytes = frames * channels * 2;
    const int riffSize = 36 + dataBytes;

    std::ofstream f(path, std::ios::binary);
    auto w32 = [&](int v) {
        char b[4] = { static_cast<char>(v & 0xff),
                      static_cast<char>((v >> 8) & 0xff),
                      static_cast<char>((v >> 16) & 0xff),
                      static_cast<char>((v >> 24) & 0xff) };
        f.write(b, 4);
    };
    auto w16 = [&](int v) {
        char b[2] = { static_cast<char>(v & 0xff),
                      static_cast<char>((v >> 8) & 0xff) };
        f.write(b, 2);
    };
    f.write("RIFF", 4); w32(riffSize); f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16);
    w16(1);                 // PCM
    w16(channels);
    w32(sampleRate);
    w32(sampleRate * channels * 2);
    w16(channels * 2);
    w16(16);
    f.write("data", 4); w32(dataBytes);
    for (int i = 0; i < dataBytes; ++i) f.put('\0');
    f.close();
    return path;
}

int main() {
    printf("=== MiniaudioBackend Tests ===\n\n");

    // [1] 未初始化即操作——全 no-op 不崩潰
    {
        MiniaudioAudioManager mgr;
        mgr.PlayCue("nonexistent.wav");
        mgr.Update();
        mgr.SetSeepageLevel(2);
        mgr.SetMasterVolume(0.5f);
        Check(!mgr.IsAvailable(), "未初始化:操作靜音 no-op");
        Check(mgr.GetSeepageLevel() == 2, "未初始化:滲透等級仍記錄");
    }

    // [2] null backend 初始化成功——有裝置管線但無真硬體
    MiniaudioAudioManager mgr;
    Check(mgr.InitializeNullBackend(), "null backend 初始化");
    Check(mgr.IsAvailable(), "available_ 為真");

    // [3] 缺檔 → LoadAudio nullptr、不崩潰
    Check(mgr.LoadAudio("matest_missing.wav") == nullptr,
          "缺檔 LoadAudio 回 nullptr");

    // [4] 垃圾檔 → nullptr
    {
        std::string p = "matest_junk.wav";
        std::ofstream f(p, std::ios::binary);
        f.write("NOT_A_WAVE_AT_ALL_JUNK", 22);
        f.close();
        Check(mgr.LoadAudio(p) == nullptr, "解碼失敗回 nullptr");
        std::remove(p.c_str());
    }

    // [5] 合法 WAV → 載入 + 元資料正確 + cache 同指標
    std::string ok = WriteWAV("ok.wav", 2, 44100, 4410);
    AudioData* d = mgr.LoadAudio(ok);
    Check(d != nullptr, "合法 WAV 載入");
    if (d) {
        Check(d->channels == 2 && d->sampleRate == 44100,
              "WAV 元資料(ch=2, sr=44100)");
        Check(d->duration > 0, "duration > 0");
        Check(!d->data.empty(), "PCM payload 非空");
    }
    Check(mgr.LoadAudio(ok) == d, "cache 命中回同指標");

    // [6] Source 生命週期 + 狀態橋接
    {
        AudioSource* s = mgr.CreateSource();
        Check(s != nullptr, "CreateSource");
        if (s && d) {
            s->SetAudioData(*d);
            s->Play();
            mgr.Update();
            Check(s->GetState() == AudioState::Playing,
                  "source Play→Update 橋接不崩潰");
            s->Stop();
            mgr.Update();
            Check(s->GetState() == AudioState::Stopped, "source Stop");
        }
        mgr.DestroySource(s);
    }

    // [7] 缺檔 source——標記 failed 不再重試,Update 安全
    {
        AudioSource* s = mgr.CreateSource();
        AudioData ad;
        ad.filePath = "matest_no_such_source.wav";
        s->SetAudioData(ad);
        s->Play();
        mgr.Update();
        mgr.Update(); // 第二輪不重試、不崩潰
        Check(true, "缺檔 source Update 安全");
        mgr.DestroySource(s);
    }

    // [8] AudioCues 事件分派:未知事件 no-op、缺檔 cue 靜音
    {
        AudioCues cues = AudioCues::Defaults(&mgr);
        Check(cues.Has("doctrine") && cues.Has("ui_click") &&
                  cues.Has("morale_rout") && cues.Has("seepage_up"),
              "Defaults 對照表含四事件");
        cues.Play("no_such_event");   // 未映射
        cues.Play("doctrine");        // 已映射但檔案不存在
        Check(true, "未知事件/缺檔 cue 靜音 no-op");
    }

    // [9] 滲透兩級音景:路徑不存在也能切等級,靜音不崩潰
    {
        mgr.SetSeepagePaths("matest_bed.wav", "matest_layer.wav");
        mgr.SetSeepageLevel(0);
        mgr.SetSeepageLevel(1);
        mgr.SetSeepageLevel(2);
        Check(mgr.GetSeepageLevel() == 2, "滲透等級 0→2 切換");
        mgr.SetSeepageLevel(5); // clamp
        Check(mgr.GetSeepageLevel() == 2, "等級 clamp 到 2");
        mgr.SetSeepageLevel(0);
        Check(mgr.GetSeepageLevel() == 0, "降回 0");
    }

    // [10] 音量/聽者 setter 在 available 下不崩潰
    mgr.SetMasterVolume(0.7f);
    mgr.SetMusicVolume(0.5f);
    mgr.SetSFXVolume(0.9f);
    mgr.SetListenerPosition(Vector3(1, 2, 3));
    mgr.SetListenerOrientation(Vector3(0, 0, -1), Vector3(0, 1, 0));
    Check(mgr.GetMasterVolume() == 0.7f &&
              mgr.GetMusicVolume() == 0.5f && mgr.GetSFXVolume() == 0.9f,
          "音量 setter/getter");

    // [11] Shutdown 冪等 + 可用後再初始化
    mgr.Shutdown();
    mgr.Shutdown();
    Check(!mgr.IsAvailable(), "Shutdown 冪等");
    Check(mgr.InitializeNullBackend(), "Shutdown 後可重初始化");
    mgr.Shutdown();

    std::remove(ok.c_str());

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
