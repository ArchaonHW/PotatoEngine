#pragma once

/**
 * LockstepSync - 決定性 lockstep 指令同步
 *
 * RTS/模擬同步模型：所有 peer 在相同 tick 執行相同指令集即可決定性重演。
 * 每 tick 各端廣播一個指令包（可為空）；本端收齊所有 peer 對 tick T
 * 的指令包才准推進 T。
 *
 * inputDelayTicks：本地輸入排到 currentTick+delay 生效，留網路往返
 * 時間。tick < delay 視為全員空指令（所有 peer 必須用相同
 * numPeers/inputDelayTicks，否則同步語意不保證）。
 *
 * 典型每幀流程：
 *   sync.QueueLocalInput(bytes);            // 0..n 次
 *   for (auto& pkt : sync.SealLocalPackets()) transport.send(pkt);
 *   while (recv) sync.OnPeerPacket(bytes);  // 收遠端包
 *   TickCommands tc;
 *   while (sync.TryAdvance(tc)) sim(tc);    // 能推就推
 *
 * 本層不含可靠性/重傳——UDP 掉包會造成 stall（ReadyToAdvance 為 false),
 * 由上層決定重送 LastLocalPacket 或斷線處理。
 */

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Potato {

class LockstepSync {
public:
    struct Config {
        int numPeers = 2;
        int localPeerId = 0;
        int inputDelayTicks = 2;
    };

    struct TickCommands {
        int tick = 0;
        // (peerId, blob) 列表，blob 為該 peer 當 tick 的一條指令
        std::vector<std::pair<int, std::vector<uint8_t>>> inputs;
    };

    explicit LockstepSync(const Config& cfg);

    // 當前 tick 視窗內排入一條本地指令（SealLocalPackets 時裝包）
    void QueueLocalInput(const std::vector<uint8_t>& data);
    void QueueLocalInput(const std::string& data) {
        QueueLocalInput(std::vector<uint8_t>(data.begin(), data.end()));
    }

    // 封裝本地指令包。補齊 lastSealed+1 ~ currentTick+delay 之間所有
    // 缺口（中間補空包），最後一包帶走已排入的輸入；回傳待廣播的
    // wire packets（正常每 tick 一包，跳 tick 時會多包）。
    std::vector<std::vector<uint8_t>> SealLocalPackets();

    // 收到遠端 wire packet（指令包或 checksum 包，自動按 magic 分派）。
    // 格式錯誤/非法 peerId/tick < delay 的指令包一律丟棄回 false。
    bool OnPeerPacket(const std::vector<uint8_t>& packet);

    // 所有 peer 對 currentTick 都有指令包？
    bool ReadyToAdvance() const;

    // 推出 currentTick 指令集並推進；未齊回 false 不動 out
    bool TryAdvance(TickCommands& out);

    int CurrentTick() const { return currentTick; }
    // 各 peer 已見到的最高指令包 tick（stall 診斷用）
    int PeerMaxTick(int peerId) const;

    // ---- 決定性校驗（desync 偵測）----
    // 記錄本端 tick 的狀態校驗值（CRC32 或自訂 hash）
    void RecordLocalChecksum(int tick, uint32_t crc);
    // 產生 checksum wire packet 供廣播
    static std::vector<uint8_t> EncodeChecksumPacket(int peerId, int tick,
                                                     uint32_t crc);
    // 比較 tick 的各方校驗值；全部一致（或缺資料無法判定）回空
    std::vector<int> ChecksumMismatches(int tick) const;

    // wire 格式：
    //   指令: 'L''K' | ver(1) | peerId(1) | tick(4 LE) | n(2 LE)
    //         | [len(2 LE) | bytes]×n
    //   校驗: 'C''K' | ver(1) | peerId(1) | tick(4 LE) | crc(4 LE)
    static std::vector<uint8_t> EncodeCommandPacket(
        int peerId, int tick,
        const std::vector<std::vector<uint8_t>>& blobs);

private:
    static constexpr uint8_t kWireVersion = 1;

    bool DecodeCommandPacket(const std::vector<uint8_t>& pkt);
    bool DecodeChecksumPacket(const std::vector<uint8_t>& pkt);

    Config cfg;
    int currentTick = 0;
    int lastSealedTick;  // 已封口到的本地 tick

    // peer -> tick -> 該 tick 的指令 blob 列表
    std::vector<std::map<int, std::vector<std::vector<uint8_t>>>> bundles;
    std::vector<int> peerMaxTick;
    std::vector<std::vector<uint8_t>> pendingLocal;  // 待封口輸入

    // peer -> tick -> crc
    std::vector<std::map<int, uint32_t>> checksums;
};

} // namespace Potato
