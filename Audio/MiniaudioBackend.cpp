#include "Audio/MiniaudioBackend.h"
#include "Logging/Logger.h"

#include "miniaudio.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace Potato {

namespace {

AudioFormat FormatFromPath(const std::string& p) {
    const auto dot = p.find_last_of('.');
    if (dot == std::string::npos) return AudioFormat::Unknown;
    const std::string ext = p.substr(dot + 1);
    if (ext == "wav" || ext == "WAV") return AudioFormat::WAV;
    if (ext == "mp3" || ext == "MP3") return AudioFormat::MP3;
    if (ext == "ogg" || ext == "OGG") return AudioFormat::OGG;
    if (ext == "flac" || ext == "FLAC") return AudioFormat::FLAC;
    return AudioFormat::Unknown;
}

} // namespace

MiniaudioAudioManager::MiniaudioAudioManager() = default;

MiniaudioAudioManager::~MiniaudioAudioManager() {
    Shutdown();
}

bool MiniaudioAudioManager::InitEngine(bool nullBackend) {
    if (available_) return true;

    ma_engine* eng = new (std::nothrow) ma_engine;
    if (!eng) return false;

    ma_context* ctx = nullptr;
    if (nullBackend) {
        // ma_backend_null：輸出管線完整但丟棄 PCM——無頭/CI 可測
        ma_context* c = new (std::nothrow) ma_context;
        if (!c) { delete eng; return false; }
        ma_backend backends[] = { ma_backend_null };
        if (ma_context_init(backends, 1, nullptr, c) != MA_SUCCESS) {
            delete c;
            delete eng;
            return false;
        }
        ctx = c;
        contextOwned_ = true;
    }

    ma_engine_config cfg = ma_engine_config_init();
    cfg.pContext = ctx;
    if (ma_engine_init(&cfg, eng) != MA_SUCCESS) {
        LOG_WARNING("miniaudio engine init failed — 靜音降級");
        if (ctx) { ma_context_uninit(ctx); delete ctx; contextOwned_ = false; }
        delete eng;
        return false;
    }

    engine_ = eng;
    context_ = ctx;
    available_ = true;
    return true;
}

bool MiniaudioAudioManager::Initialize() {
    if (!InitEngine(false)) {
        // 無裝置不視為致命——遊戲照跑,音訊層靜音
        return true;
    }
    LOG_INFO("Miniaudio audio manager initialized");
    return true;
}

bool MiniaudioAudioManager::InitializeNullBackend() {
    if (!InitEngine(true)) {
        LOG_WARNING("miniaudio null backend init failed");
        return false;
    }
    return true;
}

void MiniaudioAudioManager::Shutdown() {
    // 清掉活體聲音與滲透層（無 engine 時是 no-op）
    for (auto& [id, snd] : liveSounds_) {
        ma_sound* s = static_cast<ma_sound*>(snd);
        ma_sound_stop(s);
        ma_sound_uninit(s);
        delete s;
    }
    liveSounds_.clear();
    lastStates_.clear();
    soundFailed_.clear();

    if (seepageBed_) {
        ma_sound_uninit(static_cast<ma_sound*>(seepageBed_));
        delete static_cast<ma_sound*>(seepageBed_);
        seepageBed_ = nullptr;
    }
    if (seepageLayer_) {
        ma_sound_uninit(static_cast<ma_sound*>(seepageLayer_));
        delete static_cast<ma_sound*>(seepageLayer_);
        seepageLayer_ = nullptr;
    }
    seepageLevel_ = 0;

    DestroyAllSources();
    UnloadAllAudio();

    if (engine_) {
        ma_engine_uninit(static_cast<ma_engine*>(engine_));
        delete static_cast<ma_engine*>(engine_);
        engine_ = nullptr;
    }
    if (contextOwned_ && context_) {
        ma_context_uninit(static_cast<ma_context*>(context_));
        delete static_cast<ma_context*>(context_);
    }
    context_ = nullptr;
    contextOwned_ = false;
    available_ = false;
}

AudioData* MiniaudioAudioManager::LoadAudio(const std::string& filePath) {
    auto it = audioCache_.find(filePath);
    if (it != audioCache_.end()) return it->second.get();

    std::ifstream probe(filePath, std::ios::binary);
    if (!probe.is_open()) {
        LOG_WARNING("Audio file missing (silent): " + filePath);
        return nullptr;
    }
    probe.close();

    // ma_decode_file 直接給 f32 交錯 frames;WAV/MP3/OGG/FLAC 皆通吃
    ma_decoder_config dcfg =
        ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_uint64 frames = 0;
    void* pcm = nullptr;
    if (ma_decode_file(filePath.c_str(), &dcfg, &frames, &pcm) !=
            MA_SUCCESS ||
        !pcm) {
        LOG_WARNING("Audio decode failed (silent): " + filePath);
        return nullptr;
    }

    auto data = MakeUnique<AudioData>();
    data->filePath = filePath;
    data->format = FormatFromPath(filePath);
    data->channels = static_cast<int>(dcfg.channels);
    data->sampleRate = static_cast<int>(dcfg.sampleRate);
    data->bitsPerSample = 32;
    data->duration = dcfg.sampleRate > 0
        ? static_cast<int>((frames * 1000ull) / dcfg.sampleRate)
        : 0;
    const size_t bytes =
        static_cast<size_t>(frames) * dcfg.channels * sizeof(float);
    try {
        data->data.assign(static_cast<uint8*>(pcm),
                          static_cast<uint8*>(pcm) + bytes);
    } catch (const std::bad_alloc&) {
        ma_free(pcm, nullptr);
        LOG_ERROR("Audio payload allocation failed: " + filePath);
        return nullptr;
    }
    ma_free(pcm, nullptr);

    AudioData* ptr = data.get();
    audioCache_[filePath] = std::move(data);
    return ptr;
}

void MiniaudioAudioManager::UnloadAudio(const std::string& filePath) {
    audioCache_.erase(filePath);
}

void MiniaudioAudioManager::UnloadAllAudio() {
    audioCache_.clear();
}

AudioSource* MiniaudioAudioManager::CreateSource() {
    AudioSource* s = new (std::nothrow) AudioSource();
    if (s) {
        sources_.push_back(s);
        lastStates_[s->GetSourceID()] = s->GetState();
    }
    return s;
}

void MiniaudioAudioManager::DestroySource(AudioSource* source) {
    if (!source) return;
    auto it = std::find(sources_.begin(), sources_.end(), source);
    if (it == sources_.end()) return;
    const int id = source->GetSourceID(); // delete 前先取 ID
    sources_.erase(it);
    StopLiveSound(id);
    lastStates_.erase(id);
    soundFailed_.erase(id);
    delete source;
}

void MiniaudioAudioManager::DestroyAllSources() {
    for (AudioSource* s : sources_) {
        if (s) StopLiveSound(s->GetSourceID());
        delete s;
    }
    sources_.clear();
    lastStates_.clear();
    soundFailed_.clear();
}

void MiniaudioAudioManager::StopLiveSound(int sourceID) {
    auto it = liveSounds_.find(sourceID);
    if (it == liveSounds_.end()) return;
    ma_sound* s = static_cast<ma_sound*>(it->second);
    ma_sound_stop(s);
    ma_sound_uninit(s);
    delete s;
    liveSounds_.erase(it);
}

void MiniaudioAudioManager::StartSource(AudioSource* s) {
    if (!available_ || !s) return;
    const int id = s->GetSourceID();

    auto live = liveSounds_.find(id);
    if (live != liveSounds_.end()) {
        ma_sound* snd = static_cast<ma_sound*>(live->second);
        ma_sound_seek_to_pcm_frame(snd, 0);
        ma_sound_start(snd);
        return;
    }
    if (soundFailed_.count(id)) return;

    const std::string& path = s->GetAudioData().filePath;
    if (path.empty()) {
        soundFailed_.insert(id);
        return;
    }

    ma_sound* snd = new (std::nothrow) ma_sound;
    if (!snd) return;
    // 小檔直接解碼進記憶體;loop 需要可 seek 的 decoded sound
    const ma_uint32 flags = MA_SOUND_FLAG_DECODE;
    if (ma_sound_init_from_file(static_cast<ma_engine*>(engine_),
                                path.c_str(), flags, nullptr, nullptr,
                                snd) != MA_SUCCESS) {
        delete snd;
        soundFailed_.insert(id);
        LOG_WARNING("Audio source file unplayable (silent): " + path);
        return;
    }
    ma_sound_set_looping(snd, s->GetConfig().loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(snd,
                        masterVolume_ * sfxVolume_ * s->GetVolume());
    const Vector3& p = s->GetPosition();
    ma_sound_set_position(snd, p.x, p.y, p.z);
    ma_sound_set_spatialization_enabled(
        snd, s->IsSpatial() ? MA_TRUE : MA_FALSE);
    ma_sound_start(snd);
    liveSounds_[id] = snd;
}

void MiniaudioAudioManager::Update() {
    if (!available_) return;
    for (AudioSource* s : sources_) {
        if (!s) continue;
        const int id = s->GetSourceID();
        const AudioState cur = s->GetState();
        AudioState prev = AudioState::Stopped;
        auto pit = lastStates_.find(id);
        if (pit != lastStates_.end()) prev = pit->second;
        if (cur == prev) continue;
        lastStates_[id] = cur;

        if (cur == AudioState::Playing) {
            StartSource(s);
        } else if (cur == AudioState::Paused) {
            auto it = liveSounds_.find(id);
            if (it != liveSounds_.end())
                ma_sound_stop(static_cast<ma_sound*>(it->second));
        } else { // Stopped
            StopLiveSound(id);
        }
    }
}

void MiniaudioAudioManager::PlayCue(const std::string& filePath) {
    if (!available_ || filePath.empty()) return;
    ma_engine* eng = static_cast<ma_engine*>(engine_);
    // fire-and-forget;缺檔回錯誤但不崩潰
    if (ma_engine_play_sound(eng, filePath.c_str(), nullptr) !=
        MA_SUCCESS) {
        LOG_WARNING("Cue unplayable (silent): " + filePath);
    }
}

void MiniaudioAudioManager::SetSeepagePaths(const std::string& bed,
                                            const std::string& layer) {
    seepageBedPath_ = bed;
    seepageLayerPath_ = layer;
}

void MiniaudioAudioManager::ApplySeepageVolumes() {
    if (!available_) return;
    const float m = masterVolume_ * musicVolume_;
    if (seepageBed_) {
        ma_sound_set_volume(static_cast<ma_sound*>(seepageBed_),
                            seepageLevel_ >= 1 ? 0.6f * m : 0.0f);
    }
    if (seepageLayer_) {
        ma_sound_set_volume(static_cast<ma_sound*>(seepageLayer_),
                            seepageLevel_ >= 2 ? 0.35f * m : 0.0f);
    }
}

void MiniaudioAudioManager::SetSeepageLevel(int level) {
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    seepageLevel_ = level;
    if (!available_) return;

    ma_engine* eng = static_cast<ma_engine*>(engine_);
    auto ensureLoop = [&](void*& slot, const std::string& path) {
        if (slot || path.empty()) return;
        ma_sound* snd = new (std::nothrow) ma_sound;
        if (!snd) return;
        if (ma_sound_init_from_file(eng, path.c_str(),
                                    MA_SOUND_FLAG_DECODE, nullptr,
                                    nullptr, snd) != MA_SUCCESS) {
            delete snd;
            LOG_WARNING("Seepage bed missing (silent): " + path);
            return;
        }
        ma_sound_set_looping(snd, MA_TRUE);
        slot = snd;
    };
    ensureLoop(seepageBed_, seepageBedPath_);
    if (level >= 2) ensureLoop(seepageLayer_, seepageLayerPath_);

    ApplySeepageVolumes();
    // 0 級停掉所有層;>=1 只留 bed 發聲,2 加 layer
    if (seepageBed_) {
        ma_sound* b = static_cast<ma_sound*>(seepageBed_);
        if (level >= 1 && !ma_sound_is_playing(b)) ma_sound_start(b);
        if (level == 0 && ma_sound_is_playing(b)) ma_sound_stop(b);
    }
    if (seepageLayer_) {
        ma_sound* l = static_cast<ma_sound*>(seepageLayer_);
        if (level >= 2 && !ma_sound_is_playing(l)) ma_sound_start(l);
        if (level < 2 && ma_sound_is_playing(l)) ma_sound_stop(l);
    }
}

void MiniaudioAudioManager::SetListenerPosition(const Vector3& position) {
    listener_.position = position;
    if (available_) {
        ma_engine_listener_set_position(
            static_cast<ma_engine*>(engine_), 0,
            position.x, position.y, position.z);
    }
}

void MiniaudioAudioManager::SetListenerVelocity(const Vector3& velocity) {
    listener_.velocity = velocity;
    if (available_) {
        ma_engine_listener_set_velocity(
            static_cast<ma_engine*>(engine_), 0,
            velocity.x, velocity.y, velocity.z);
    }
}

void MiniaudioAudioManager::SetListenerOrientation(const Vector3& forward,
                                                   const Vector3& up) {
    listener_.forward = forward;
    listener_.up = up;
    if (available_) {
        ma_engine* eng = static_cast<ma_engine*>(engine_);
        ma_engine_listener_set_direction(eng, 0, forward.x, forward.y,
                                         forward.z);
        ma_engine_listener_set_world_up(eng, 0, up.x, up.y, up.z);
    }
}

void MiniaudioAudioManager::SetMasterVolume(float volume) {
    masterVolume_ = volume;
    if (available_) {
        ma_engine_set_volume(static_cast<ma_engine*>(engine_), volume);
    }
    ApplySeepageVolumes();
}

float MiniaudioAudioManager::GetMasterVolume() const { return masterVolume_; }
void MiniaudioAudioManager::SetMusicVolume(float volume) {
    musicVolume_ = volume;
    ApplySeepageVolumes();
}
float MiniaudioAudioManager::GetMusicVolume() const { return musicVolume_; }
void MiniaudioAudioManager::SetSFXVolume(float volume) {
    sfxVolume_ = volume;
}
float MiniaudioAudioManager::GetSFXVolume() const { return sfxVolume_; }

} // namespace Potato
