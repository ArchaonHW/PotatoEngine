#pragma once

#include <string>

namespace Potato {

/**
 * 音頻系統接口
 * 定義音頻引擎必須實現的基本功能
 */
class IAudio {
public:
    virtual ~IAudio() = default;
    
    // 初始化和關閉
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    
    // 音頻更新
    virtual void Update(float deltaTime) = 0;
    
    // 聲音播放
    virtual bool PlaySound(const std::string& soundPath, bool loop = false) = 0;
    virtual void StopSound(const std::string& soundPath) = 0;
    virtual void StopAllSounds() = 0;
    
    // 音量控制
    virtual void SetMasterVolume(float volume) = 0;
    virtual void SetMusicVolume(float volume) = 0;
    virtual void SetSFXVolume(float volume) = 0;
    
    // 3D音頻
    virtual void SetListenerPosition(float x, float y, float z) = 0;
    virtual void PlaySound3D(const std::string& soundPath, 
                             float x, float y, float z, 
                             bool loop = false) = 0;
    
    // 音頻加載
    virtual bool LoadSound(const std::string& soundPath) = 0;
    virtual void UnloadSound(const std::string& soundPath) = 0;
};

} // namespace Potato