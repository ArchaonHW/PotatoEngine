#pragma once

#include "Audio/MiniaudioBackend.h"
#include <string>
#include <unordered_map>

namespace Potato {

/**
 * AudioCues — F-4 事件→音檔對照的薄分派層。
 *
 * 呼叫端用語意事件名（"doctrine"/"morale_rout"/"seepage_up"/"ui_click"），
 * 音檔路徑由表決定;無映射、檔案缺失、無裝置皆靜音 no-op——
 * 音效是可選增強,不參與遊戲邏輯成敗。
 *
 * 用法:
 *   AudioCues cues = AudioCues::Defaults(&audioMgr);
 *   cues.Play("doctrine");        // doctrine 觸發
 *   cues.SetSeepageLevel(2);      // 滲透 2 級音景循環
 */
class AudioCues {
public:
    explicit AudioCues(MiniaudioAudioManager* mgr) : mgr_(mgr) {}

    void Map(const std::string& event, const std::string& path) {
        table_[event] = path;
    }
    bool Has(const std::string& event) const {
        return table_.find(event) != table_.end();
    }
    const std::unordered_map<std::string, std::string>& Table() const {
        return table_;
    }

    // 播放事件音;未知事件/無管理器/無裝置 → no-op
    void Play(const std::string& event) const {
        if (!mgr_ || !mgr_->IsAvailable()) return;
        auto it = table_.find(event);
        if (it == table_.end()) return;
        mgr_->PlayCue(it->second);
    }

    void SetSeepageLevel(int level) const {
        if (mgr_) mgr_->SetSeepageLevel(level);
    }

    // 預設對照表——音檔放 <assetsRoot>/audio/,缺檔靜音降級。
    // assetsRoot 由呼叫端給定（例:DemoAssets::Root() 以 exe 定位
    // 資產根）;預設 "assets" 維持 cwd 相對的舊行為。
    static AudioCues Defaults(MiniaudioAudioManager* mgr,
                              const std::string& assetsRoot = "assets") {
        const std::string dir = assetsRoot + "/audio/";
        AudioCues c(mgr);
        c.Map("doctrine",    dir + "doctrine.wav");
        c.Map("morale_rout", dir + "rout.wav");
        c.Map("seepage_up",  dir + "seepage_up.wav");
        c.Map("ui_click",    dir + "ui_click.wav");
        if (mgr) {
            mgr->SetSeepagePaths(dir + "seepage_bed.wav",
                                 dir + "seepage_layer.wav");
        }
        return c;
    }

private:
    MiniaudioAudioManager* mgr_;
    std::unordered_map<std::string, std::string> table_;
};

} // namespace Potato
