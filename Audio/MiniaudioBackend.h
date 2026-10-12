#pragma once

#include "Audio/AudioSystem.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Potato {

/**
 * MiniaudioAudioManager — miniaudio 後端的 IAudioManager 實作（F-4）。
 *
 * 降級契約（驗收：無音檔/無裝置不崩潰）：
 *   - 無輸出裝置或初始化失敗 → available_=false，所有操作靜音 no-op，
 *     Initialize() 仍回 true（音訊失敗不擋遊戲）
 *   - 音檔缺失/解碼失敗 → LoadAudio 回 nullptr、播放請求記為 failed
 *     不再重試
 *   - Shutdown 冪等；失敗後可再 Initialize 重試
 *
 * 公開標頭不依賴 miniaudio.h（opaque 指標）；實作巨集集中
 * MiniaudioImpl.cpp 單一 TU（vendored 規則）。
 *
 * 事件層（IAudioManager 之外的 F-4 最小介面）：
 *   - PlayCue(path)：fire-and-forget SFX
 *   - SetSeepageLevel(0/1/2)：滲透音景循環——1 級低音景 bed，
 *     2 級疊高頻層（UX「滲透聲先於形」）
 */
class MiniaudioAudioManager : public IAudioManager {
public:
    MiniaudioAudioManager();
    ~MiniaudioAudioManager() override;

    bool Initialize() override;
    // 測試/無頭環境：強制 ma_backend_null——有輸出管線但不吃真裝置
    bool InitializeNullBackend();
    void Shutdown() override;
    void Update() override;

    AudioData* LoadAudio(const std::string& filePath) override;
    void UnloadAudio(const std::string& filePath) override;
    void UnloadAllAudio() override;

    AudioSource* CreateSource() override;
    void DestroySource(AudioSource* source) override;
    void DestroyAllSources() override;

    void SetListenerPosition(const Vector3& position) override;
    void SetListenerVelocity(const Vector3& velocity) override;
    void SetListenerOrientation(const Vector3& forward,
                              const Vector3& up) override;

    void SetMasterVolume(float volume) override;
    float GetMasterVolume() const override;
    void SetMusicVolume(float volume) override;
    float GetMusicVolume() const override;
    void SetSFXVolume(float volume) override;
    float GetSFXVolume() const override;

    // ---- F-4 事件層 ----
    bool IsAvailable() const { return available_; }
    void PlayCue(const std::string& filePath);
    // 滲透音景的兩個 loop 層路徑；缺檔照樣可設，到等級時靜音
    void SetSeepagePaths(const std::string& bed, const std::string& layer);
    void SetSeepageLevel(int level);
    int GetSeepageLevel() const { return seepageLevel_; }

private:
    bool InitEngine(bool nullBackend);
    void StopLiveSound(int sourceID);
    void StartSource(AudioSource* s);
    void ApplySeepageVolumes();

    void* engine_ = nullptr;   // ma_engine*
    void* context_ = nullptr;  // ma_context*（僅自管 context 時非空）
    bool available_ = false;
    bool contextOwned_ = false;

    std::unordered_map<std::string, UniquePtr<AudioData>> audioCache_;
    std::vector<AudioSource*> sources_;
    // sourceID → 活體 ma_sound(void*)；依 AudioSource 狀態機驅動
    std::unordered_map<int, void*> liveSounds_;
    std::unordered_map<int, AudioState> lastStates_;
    std::unordered_set<int> soundFailed_;  // 缺檔等——不再重試

    void* seepageBed_ = nullptr;    // ma_sound*（lazy init）
    void* seepageLayer_ = nullptr;
    std::string seepageBedPath_, seepageLayerPath_;
    int seepageLevel_ = 0;

    float masterVolume_ = 1.0f;
    float musicVolume_ = 1.0f;
    float sfxVolume_ = 1.0f;
    ListenerConfig listener_;
};

} // namespace Potato
