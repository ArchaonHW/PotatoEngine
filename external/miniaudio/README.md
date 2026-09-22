# miniaudio

單標頭音訊播放/擷取函式庫——F-4 音訊里程碑的後端。

- 來源：https://github.com/mackron/miniaudio
- 版本：0.11.22（tag 固定，`miniaudio.h` 單檔）
- License：public domain 或 MIT-0（任選，見標頭檔尾部授權聲明）

## 使用方式

- 實作巨集 `MINIAUDIO_IMPLEMENTATION` 集中在 `Audio/MiniaudioImpl.cpp`
  單一 TU——其他檔案不得重複定義。
- 後端封裝在 `Audio/MiniaudioBackend.{h,cpp}`（`MiniaudioAudioManager`）；
  公開介面走 `Audio/AudioSystem.h` 的 `IAudioManager`，外部不直接依賴
  本標頭。
- 無音檔/無輸出裝置時降級靜音，不崩潰（F-4 驗收）。
