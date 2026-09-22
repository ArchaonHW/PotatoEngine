#include "AudioSystem.h"
#include "MiniaudioBackend.h"
#include "Logging/Logger.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <new>

namespace Potato {

// ============================================================================
// AudioSource 實現
// ============================================================================

int AudioSource::nextSourceID = 0;

AudioSource::AudioSource()
    : sourceID(nextSourceID++)
    , state(AudioState::Stopped)
    , position(Vector3::Zero())
    , velocity(Vector3::Zero())
{
}

AudioSource::~AudioSource() {
}

void AudioSource::SetAudioData(const AudioData& data) {
    audioData = data;
}

void AudioSource::Play() {
    state = AudioState::Playing;
}

void AudioSource::Pause() {
    if (state == AudioState::Playing) {
        state = AudioState::Paused;
    }
}

void AudioSource::Stop() {
    state = AudioState::Stopped;
}

void AudioSource::Restart() {
    state = AudioState::Playing;
}

void AudioSource::SetConfig(const AudioSourceConfig& cfg) {
    config = cfg;
}

void AudioSource::SetVolume(float volume) {
    config.volume = volume;
}

void AudioSource::SetPitch(float pitch) {
    config.pitch = pitch;
}

void AudioSource::SetPosition(const Vector3& pos) {
    position = pos;
    UpdateSpatialPosition();
}

void AudioSource::SetVelocity(const Vector3& vel) {
    velocity = vel;
}

void AudioSource::EnableSpatial(bool enable) {
    config.spatial = enable;
}

void AudioSource::SetFinishedCallback(AudioCallback callback) {
    finishedCallback = callback;
}

void AudioSource::UpdateSpatialPosition() {
    // OpenAL 實現會在 OpenALAudioManager 中處理
}

// ============================================================================
// OpenALAudioManager 實現
// ============================================================================

OpenALAudioManager::OpenALAudioManager()
    : device(nullptr)
    , context(nullptr)
    , masterVolume(1.0f)
    , musicVolume(1.0f)
    , sfxVolume(1.0f)
    , initialized(false)
{
}

OpenALAudioManager::~OpenALAudioManager() {
    Shutdown();
}

bool OpenALAudioManager::Initialize() {
    if (initialized) {
        return true;
    }
    
    LOG_INFO("Initializing OpenAL Audio Manager...");
    
    SetupOpenAL();
    
    initialized = true;
    LOG_INFO("OpenAL Audio Manager initialized successfully");
    
    return true;
}

void OpenALAudioManager::Shutdown() {
    if (!initialized) {
        return;
    }
    
    LOG_INFO("Shutting down OpenAL Audio Manager...");
    
    DestroyAllSources();
    UnloadAllAudio();
    CleanupOpenAL();
    
    initialized = false;
    LOG_INFO("OpenAL Audio Manager shutdown complete");
}

void OpenALAudioManager::Update() {
    // 更新音頻源狀態
    for (auto source : sources) {
        if (source->GetState() == AudioState::Playing) {
            // 檢查是否播放完成
            // 簡化實現
        }
    }
}

AudioData* OpenALAudioManager::LoadAudio(const std::string& filePath) {
    // 檢查緩存
    auto it = audioCache.find(filePath);
    if (it != audioCache.end()) {
        return it->second.get();
    }
    
    // 加載音頻文件
    auto audioData = MakeUnique<AudioData>();
    
    // 根據擴展名選擇加載器
    if (filePath.find(".wav") != std::string::npos) {
        if (!LoadWAV(filePath, *audioData)) {
            LOG_ERROR("Failed to load WAV file: " + filePath);
            return nullptr;
        }
    } else {
        LOG_WARNING("Unsupported audio format: " + filePath);
        return nullptr;
    }
    
    audioData->filePath = filePath;
    AudioData* dataPtr = audioData.get();
    audioCache[filePath] = std::move(audioData);
    
    LOG_INFO("Loaded audio: " + filePath);
    return dataPtr;
}

void OpenALAudioManager::UnloadAudio(const std::string& filePath) {
    auto it = audioCache.find(filePath);
    if (it != audioCache.end()) {
        audioCache.erase(it);
        LOG_INFO("Unloaded audio: " + filePath);
    }
}

void OpenALAudioManager::UnloadAllAudio() {
    audioCache.clear();
    LOG_INFO("Unloaded all audio");
}

AudioSource* OpenALAudioManager::CreateSource() {
    AudioSource* sourcePtr = new AudioSource();
    sources.push_back(sourcePtr);
    
    LOG_INFO("Created audio source with ID: " + std::to_string(sourcePtr->GetSourceID()));
    
    return sourcePtr;
}

void OpenALAudioManager::DestroySource(AudioSource* source) {
    auto it = std::find(sources.begin(), sources.end(), source);
    if (it != sources.end()) {
        int sourceID = source->GetSourceID(); // 先取出 ID：delete 後再讀是 UAF
        sources.erase(it);
        delete source;
        LOG_INFO("Destroyed audio source with ID: " + std::to_string(sourceID));
    }
}

void OpenALAudioManager::DestroyAllSources() {
    for (auto source : sources) {
        delete source;
    }
    sources.clear();
    LOG_INFO("Destroyed all audio sources");
}

void OpenALAudioManager::SetListenerPosition(const Vector3& position) {
    listener.position = position;
}

void OpenALAudioManager::SetListenerVelocity(const Vector3& velocity) {
    listener.velocity = velocity;
}

void OpenALAudioManager::SetListenerOrientation(const Vector3& forward, const Vector3& up) {
    listener.forward = forward;
    listener.up = up;
}

void OpenALAudioManager::SetMasterVolume(float volume) {
    masterVolume = volume;
}

float OpenALAudioManager::GetMasterVolume() const {
    return masterVolume;
}

void OpenALAudioManager::SetMusicVolume(float volume) {
    musicVolume = volume;
}

float OpenALAudioManager::GetMusicVolume() const {
    return musicVolume;
}

void OpenALAudioManager::SetSFXVolume(float volume) {
    sfxVolume = volume;
}

float OpenALAudioManager::GetSFXVolume() const {
    return sfxVolume;
}

void OpenALAudioManager::SetupOpenAL() {
    // 簡化實現：實際需要 OpenAL 庫
    LOG_INFO("OpenAL setup (simplified implementation)");
}

void OpenALAudioManager::CleanupOpenAL() {
    LOG_INFO("OpenAL cleanup");
}

bool OpenALAudioManager::LoadWAV(const std::string& filePath, AudioData& data) {
    // 簡化 WAV 加載器
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open WAV file: " + filePath);
        return false;
    }
    
    // 讀取 WAV 頭部
    char header[44];
    file.read(header, 44);
    if (!file.good()) {
        LOG_ERROR("WAV file too small or unreadable: " + filePath);
        return false;
    }

    // 驗證 RIFF/WAVE magic——非 WAV 檔（或截斷檔）直接拒絕,
    // 否則會把任意二進位資料當音訊 payload
    if (std::memcmp(header, "RIFF", 4) != 0 ||
        std::memcmp(header + 8, "WAVE", 4) != 0) {
        LOG_ERROR("Not a RIFF/WAVE file: " + filePath);
        return false;
    }
    // fmt chunk magic（PCM WAV 標準佈局,offset 12 = "fmt "）
    if (std::memcmp(header + 12, "fmt ", 4) != 0) {
        LOG_ERROR("WAV missing fmt chunk: " + filePath);
        return false;
    }

    // 簡化解析
    data.format = AudioFormat::WAV;
    data.channels = 2;
    data.sampleRate = 44100;
    data.bitsPerSample = 16;

    // 讀取音頻數據
    file.seekg(0, std::ios::end);
    std::streampos endPos = file.tellg();
    if (endPos < 44 || endPos == std::streampos(-1)) {
        LOG_ERROR("WAV file too small: " + filePath);
        return false;
    }
    file.seekg(44, std::ios::beg);

    // 上限 512MB：防止畸形/超大檔案讓 resize 丟 bad_alloc
    constexpr std::streamoff kMaxWAVPayload = 512ull * 1024 * 1024;
    if (endPos - std::streampos(44) > kMaxWAVPayload) {
        LOG_ERROR("WAV payload too large: " + filePath);
        return false;
    }

    size_t dataSize = static_cast<size_t>(endPos) - 44;
    try {
        data.data.resize(dataSize);
    } catch (const std::bad_alloc&) {
        LOG_ERROR("WAV payload allocation failed: " + filePath);
        return false;
    }
    if (dataSize > 0) {
        file.read(reinterpret_cast<char*>(data.data.data()), dataSize);
        if (!file) {
            LOG_ERROR("WAV payload read failed: " + filePath);
            data.data.clear();
            return false;
        }
    }
    
    // duration（毫秒）= 資料大小 / 每秒位元組數 * 1000
    size_t bytesPerSecond = static_cast<size_t>(data.channels) * data.sampleRate * (data.bitsPerSample / 8);
    data.duration = (bytesPerSecond > 0)
        ? static_cast<int>((dataSize * 1000) / bytesPerSecond)
        : 0;
    
    return true;
}

// ============================================================================
// AudioManager 實現
// ============================================================================

AudioManager::AudioManager() {
}

AudioManager::~AudioManager() {
    if (audioManager) {
        audioManager->Shutdown();
    }
}

AudioManager& AudioManager::GetInstance() {
    static AudioManager instance;
    return instance;
}

void AudioManager::SetImplementation(UniquePtr<IAudioManager> impl) {
    audioManager = std::move(impl);
}

bool AudioManager::Initialize() {
    if (!audioManager) {
        LOG_ERROR("No audio manager implementation set");
        return false;
    }
    return audioManager->Initialize();
}

void AudioManager::Shutdown() {
    if (audioManager) {
        audioManager->Shutdown();
    }
}

void AudioManager::Update() {
    if (audioManager) {
        audioManager->Update();
    }
}

AudioData* AudioManager::LoadAudio(const std::string& filePath) {
    if (audioManager) {
        return audioManager->LoadAudio(filePath);
    }
    return nullptr;
}

AudioSource* AudioManager::CreateSource() {
    if (audioManager) {
        return audioManager->CreateSource();
    }
    return nullptr;
}

void AudioManager::SetMasterVolume(float volume) {
    if (audioManager) {
        audioManager->SetMasterVolume(volume);
    }
}

void AudioManager::SetMusicVolume(float volume) {
    if (audioManager) {
        audioManager->SetMusicVolume(volume);
    }
}

void AudioManager::SetSFXVolume(float volume) {
    if (audioManager) {
        audioManager->SetSFXVolume(volume);
    }
}

// ============================================================================
// 全局函數
// ============================================================================

AudioManager* gAudioManager = nullptr;

bool InitializeAudioManager() {
    if (!gAudioManager) {
        gAudioManager = &AudioManager::GetInstance();
    }
    
    // F-4：預設 miniaudio 實後端；無裝置時自行靜音降級,Initialize 不擋遊戲
    auto miniaudioManager = MakeUnique<MiniaudioAudioManager>();
    gAudioManager->SetImplementation(std::move(miniaudioManager));
    
    return gAudioManager->Initialize();
}

void ShutdownAudioManager() {
    if (gAudioManager) {
        gAudioManager->Shutdown();
    }
}

AudioManager* GetAudioManager() {
    if (!gAudioManager) {
        gAudioManager = &AudioManager::GetInstance();
    }
    return gAudioManager;
}

} // namespace Potato