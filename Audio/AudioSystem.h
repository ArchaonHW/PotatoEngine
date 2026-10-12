#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace Potato {

/**
 * 音頻格式
 */
enum class AudioFormat {
    Unknown,
    WAV,
    MP3,
    OGG,
    FLAC
};

/**
 * 音頻播放狀態
 */
enum class AudioState {
    Stopped,
    Playing,
    Paused
};

/**
 * 音頻音效類型
 */
enum class SoundType {
    SFX,          // 音效
    Music,        // 音樂
    Ambient,      // 環境音
    Voice         // 語音
};

/**
 * 音頻數據
 */
struct AudioData {
    std::string filePath;
    AudioFormat format;
    std::vector<uint8> data;
    int channels;
    int sampleRate;
    int bitsPerSample;
    int duration; // 毫秒
    
    AudioData()
        : format(AudioFormat::Unknown)
        , channels(0)
        , sampleRate(0)
        , bitsPerSample(0)
        , duration(0)
    {
    }
};

/**
 * 音頻緩衝區
 * 已載入的音頻資源,供資源系統與音頻源引用
 */
class AudioBuffer {
public:
    AudioBuffer() = default;
    explicit AudioBuffer(const AudioData& data) : data(data) {}
    
    const AudioData& GetData() const { return data; }
    AudioData& GetData() { return data; }
    
    bool IsValid() const { return !data.data.empty(); }
    int GetDuration() const { return data.duration; }
    int GetChannels() const { return data.channels; }
    int GetSampleRate() const { return data.sampleRate; }
    
private:
    AudioData data;
};

/**
 * 音頻源配置
 */
struct AudioSourceConfig {
    bool loop;
    float volume;
    float pitch;
    bool spatial;
    float minDistance;
    float maxDistance;
    float rolloffFactor;
    
    AudioSourceConfig()
        : loop(false)
        , volume(1.0f)
        , pitch(1.0f)
        , spatial(false)
        , minDistance(1.0f)
        , maxDistance(100.0f)
        , rolloffFactor(1.0f)
    {
    }
};

/**
 * 聽者配置
 */
struct ListenerConfig {
    Vector3 position;
    Vector3 velocity;
    Vector3 forward;
    Vector3 up;
    
    ListenerConfig()
        : position(Vector3::Zero())
        , velocity(Vector3::Zero())
        , forward(Vector3(0.0f, 0.0f, -1.0f))
        , up(Vector3(0.0f, 1.0f, 0.0f))
    {
    }
};

/**
 * 音頻回調類型
 */
using AudioCallback = std::function<void(AudioState)>;

/**
 * 音頻源
 */
class AudioSource {
public:
    AudioSource();
    ~AudioSource();
    
    // 音頻數據
    void SetAudioData(const AudioData& data);
    const AudioData& GetAudioData() const { return audioData; }
    
    // 播放控制
    void Play();
    void Pause();
    void Stop();
    void Restart();
    
    AudioState GetState() const { return state; }
    
    // 配置
    void SetConfig(const AudioSourceConfig& config);
    AudioSourceConfig GetConfig() const { return config; }
    
    // 音量控制
    void SetVolume(float volume);
    float GetVolume() const { return config.volume; }
    
    void SetPitch(float pitch);
    float GetPitch() const { return config.pitch; }
    
    // 空間音頻
    void SetPosition(const Vector3& position);
    Vector3 GetPosition() const { return position; }
    
    void SetVelocity(const Vector3& velocity);
    Vector3 GetVelocity() const { return velocity; }
    
    void EnableSpatial(bool enable);
    bool IsSpatial() const { return config.spatial; }
    
    // 回調
    void SetFinishedCallback(AudioCallback callback);
    
    // 唯一標識
    int GetSourceID() const { return sourceID; }
    
private:
    void UpdateSpatialPosition();
    
private:
    int sourceID;
    AudioData audioData;
    AudioSourceConfig config;
    AudioState state;
    
    Vector3 position;
    Vector3 velocity;
    
    AudioCallback finishedCallback;
    
    static int nextSourceID;
};

/**
 * 音頻管理器接口
 */
class IAudioManager {
public:
    virtual ~IAudioManager() = default;
    
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    
    virtual void Update() = 0;
    
    // 音頻加載
    virtual AudioData* LoadAudio(const std::string& filePath) = 0;
    virtual void UnloadAudio(const std::string& filePath) = 0;
    virtual void UnloadAllAudio() = 0;
    
    // 音頻源管理
    virtual AudioSource* CreateSource() = 0;
    virtual void DestroySource(AudioSource* source) = 0;
    virtual void DestroyAllSources() = 0;
    
    // 聽者設置
    virtual void SetListenerPosition(const Vector3& position) = 0;
    virtual void SetListenerVelocity(const Vector3& velocity) = 0;
    virtual void SetListenerOrientation(const Vector3& forward, const Vector3& up) = 0;
    
    // 全局設置
    virtual void SetMasterVolume(float volume) = 0;
    virtual float GetMasterVolume() const = 0;
    
    virtual void SetMusicVolume(float volume) = 0;
    virtual float GetMusicVolume() const = 0;
    
    virtual void SetSFXVolume(float volume) = 0;
    virtual float GetSFXVolume() const = 0;
};

/**
 * OpenAL 音頻管理器實現
 */
class OpenALAudioManager : public IAudioManager {
public:
    OpenALAudioManager();
    ~OpenALAudioManager() override;
    
    bool Initialize() override;
    void Shutdown() override;
    
    void Update() override;
    
    // 音頻加載
    AudioData* LoadAudio(const std::string& filePath) override;
    void UnloadAudio(const std::string& filePath) override;
    void UnloadAllAudio() override;
    
    // 音頻源管理
    AudioSource* CreateSource() override;
    void DestroySource(AudioSource* source) override;
    void DestroyAllSources() override;
    
    // 聽者設置
    void SetListenerPosition(const Vector3& position) override;
    void SetListenerVelocity(const Vector3& velocity) override;
    void SetListenerOrientation(const Vector3& forward, const Vector3& up) override;
    
    // 全局設置
    void SetMasterVolume(float volume) override;
    float GetMasterVolume() const override;
    
    void SetMusicVolume(float volume) override;
    float GetMusicVolume() const override;
    
    void SetSFXVolume(float volume) override;
    float GetSFXVolume() const override;
    
private:
    void SetupOpenAL();
    void CleanupOpenAL();
    bool LoadWAV(const std::string& filePath, AudioData& data);
    
private:
    void* device;
    void* context;
    
    std::unordered_map<std::string, UniquePtr<AudioData>> audioCache;
    std::vector<AudioSource*> sources;
    
    float masterVolume;
    float musicVolume;
    float sfxVolume;
    
    ListenerConfig listener;
    
    bool initialized;
};

/**
 * 音頻管理器（全局單例）
 */
class AudioManager {
public:
    static AudioManager& GetInstance();
    
    IAudioManager* GetImplementation() { return audioManager.get(); }
    void SetImplementation(UniquePtr<IAudioManager> impl);
    
    // 便捷方法
    bool Initialize();
    void Shutdown();
    void Update();
    
    AudioData* LoadAudio(const std::string& filePath);
    AudioSource* CreateSource();
    
    void SetMasterVolume(float volume);
    void SetMusicVolume(float volume);
    void SetSFXVolume(float volume);
    
private:
    AudioManager();
    ~AudioManager();
    
    UniquePtr<IAudioManager> audioManager;
};

// 全局音頻管理器
extern AudioManager* gAudioManager;

/**
 * 初始化全局音頻管理器
 */
bool InitializeAudioManager();

/**
 * 關閉全局音頻管理器
 */
void ShutdownAudioManager();

/**
 * 獲取全局音頻管理器
 */
AudioManager* GetAudioManager();

} // namespace Potato

// 便捷宏
#define GET_AUDIO() Potato::GetAudioManager()