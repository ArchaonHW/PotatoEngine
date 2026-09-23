/**
 * NetworkingTest - Networking 模組契約測試（headless,localhost only）
 *
 * 覆蓋：
 *   [UDP]     兩 socket localhost 互送、ephemeral port、無資料非阻塞
 *   [封包]    指令包/校驗包編解碼回圈、畸形包拒絕
 *   [Lockstep] 兩端記憶體直送:延遲生效、收齊才推進、指令一致
 *   [校驗]    checksum 一致/mismatch 偵測
 *   [整合]    LockstepSync × 真 UdpSocket loopback
 */

#include "Networking/UdpSocket.h"
#include "Networking/LockstepSync.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace Potato;

namespace {

int g_pass = 0;
int g_fail = 0;

void Expect(bool cond, const char* name, const std::string& detail = "") {
    if (cond) {
        g_pass++;
        printf("  [PASS] %s%s%s\n", name,
               detail.empty() ? "" : "  -- ", detail.c_str());
    } else {
        g_fail++;
        printf("  [FAIL] %s%s%s\n", name,
               detail.empty() ? "" : "  -- ", detail.c_str());
    }
}

// 收一封 datagram(重試 ~1s);失敗回 false
bool RecvOne(UdpSocket& sock, std::vector<uint8_t>& out,
             std::string* from = nullptr, uint16_t* port = nullptr) {
    uint8_t buf[2048];
    for (int i = 0; i < 200; ++i) {
        const int n = sock.RecvFrom(buf, sizeof(buf), from, port);
        if (n > 0) {
            out.assign(buf, buf + n);
            return true;
        }
        if (n < 0) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

} // anonymous namespace

int main() {
    printf("=== Networking 測試 ===\n\n");

    // ---- [UDP] ----
    printf("-- UdpSocket --\n");
    {
        UdpSocket a, b;
        Expect(a.Bind(0) && b.Bind(0), "兩 socket 綁 ephemeral port");
        Expect(a.LocalPort() != 0 && b.LocalPort() != 0 &&
               a.LocalPort() != b.LocalPort(),
               "取得不同實際 port",
               std::to_string(a.LocalPort()) + "/" +
               std::to_string(b.LocalPort()));

        const char msg[] = "potato-ping";
        Expect(a.SendTo("127.0.0.1", b.LocalPort(), msg, sizeof(msg)),
               "SendTo localhost");

        std::vector<uint8_t> got;
        std::string from;
        uint16_t fromPort = 0;
        Expect(RecvOne(b, got, &from, &fromPort), "收到 datagram");
        Expect(got.size() == sizeof(msg) &&
                   std::memcmp(got.data(), msg, sizeof(msg)) == 0,
               "payload 一致");
        Expect(from == "127.0.0.1" && fromPort == a.LocalPort(),
               "來源位址/port 正確");

        uint8_t tmp[64];
        Expect(b.RecvFrom(tmp, sizeof(tmp)) == 0,
               "無資料時非阻塞回 0");
    }

    // ---- [封包編解碼] ----
    printf("-- 封包編解碼 --\n");
    {
        std::vector<std::vector<uint8_t>> blobs = {
            {'m', 'o', 'v', 'e'}, {'a', 't', 't', 'a', 'c', 'k'}};
        auto pkt = LockstepSync::EncodeCommandPacket(1, 42, blobs);

        LockstepSync::Config cfg;
        cfg.numPeers = 2; cfg.localPeerId = 0; cfg.inputDelayTicks = 2;
        LockstepSync sync(cfg);
        Expect(sync.OnPeerPacket(pkt), "合法指令包接受");
        Expect(sync.PeerMaxTick(1) == 42, "peer1 maxTick=42");

        Expect(!sync.OnPeerPacket({}), "空包拒絕");
        Expect(!sync.OnPeerPacket({'X', 'Y', 1, 0}), "壞 magic 拒絕");
        auto truncated = pkt;
        truncated.resize(6);
        Expect(!sync.OnPeerPacket(truncated), "截斷包拒絕");
        // peerId=local 的包拒絕(不該從網路來)
        auto selfPkt = LockstepSync::EncodeCommandPacket(0, 10, {});
        Expect(!sync.OnPeerPacket(selfPkt), "localPeerId 包拒絕");
        // tick < delay 拒絕
        auto earlyPkt = LockstepSync::EncodeCommandPacket(1, 0, {});
        Expect(!sync.OnPeerPacket(earlyPkt), "tick<delay 包拒絕");
    }

    // ---- [Lockstep 記憶體直送] ----
    printf("-- Lockstep 兩端直送 --\n");
    {
        LockstepSync::Config c0, c1;
        c0.numPeers = 2; c0.localPeerId = 0; c0.inputDelayTicks = 2;
        c1.numPeers = 2; c1.localPeerId = 1; c1.inputDelayTicks = 2;
        LockstepSync s0(c0), s1(c1);
        LockstepSync::TickCommands tc;

        // tick 0/1 預填空包 → 無需網路立即可推
        Expect(s0.ReadyToAdvance(), "tick0 預填即可推進");
        Expect(s0.TryAdvance(tc) && tc.tick == 0 && tc.inputs.empty(),
               "tick0 空指令推進");
        s0.TryAdvance(tc);  // tick1 也是預填空
        s1.TryAdvance(tc); s1.TryAdvance(tc);  // s1 對齊到 tick2
        Expect(s0.CurrentTick() == 2 && s1.CurrentTick() == 2,
               "雙方對齊 tick2");

        // 對端包未到 → stall
        Expect(!s0.ReadyToAdvance(), "tick2 缺對端包 → 不可推進");

        // 同 tick 視窗排輸入(delay=2 → 同落 tick4),封口交換
        s0.QueueLocalInput(std::vector<uint8_t>{'u'});
        s1.QueueLocalInput(std::vector<uint8_t>{'v'});
        for (auto& p : s0.SealLocalPackets()) s1.OnPeerPacket(p);
        for (auto& p : s1.SealLocalPackets()) s0.OnPeerPacket(p);
        Expect(s0.ReadyToAdvance(), "tick2 收到對端包可推進");
        s0.TryAdvance(tc);  // tick2 空
        s0.TryAdvance(tc);  // tick3 空
        s1.TryAdvance(tc); s1.TryAdvance(tc);

        // tick4:雙方在 tick2 排的輸入落地
        Expect(s0.ReadyToAdvance() && s1.ReadyToAdvance(),
               "tick4 雙方指令齊");
        LockstepSync::TickCommands t0, t1;
        Expect(s0.TryAdvance(t0) && s1.TryAdvance(t1),
               "tick4 雙方同時推進");
        Expect(t0.inputs.size() == 2 && t1.inputs.size() == 2,
               "各含兩端指令",
               std::to_string(t0.inputs.size()));
        // 兩端看到的指令集必須一致(決定性)
        bool sameOrder = t0.inputs.size() == t1.inputs.size();
        if (sameOrder) {
            for (size_t i = 0; i < t0.inputs.size(); ++i) {
                if (t0.inputs[i].first != t1.inputs[i].first ||
                    t0.inputs[i].second != t1.inputs[i].second)
                    sameOrder = false;
            }
        }
        Expect(sameOrder, "兩端指令集逐位一致(決定性)");
        Expect(s0.CurrentTick() == 5 && s1.CurrentTick() == 5,
               "雙方同到 tick5");
    }

    // ---- [校驗] ----
    printf("-- checksum desync 偵測 --\n");
    {
        LockstepSync::Config cfg;
        cfg.numPeers = 2; cfg.localPeerId = 0; cfg.inputDelayTicks = 2;
        LockstepSync sync(cfg);
        sync.RecordLocalChecksum(10, 0xDEADBEEF);
        auto okPkt = LockstepSync::EncodeChecksumPacket(1, 10, 0xDEADBEEF);
        Expect(sync.OnPeerPacket(okPkt), "checksum 包接受");
        Expect(sync.ChecksumMismatches(10).empty(), "一致無 mismatch");
        auto badPkt = LockstepSync::EncodeChecksumPacket(1, 11, 0x1111);
        sync.RecordLocalChecksum(11, 0x2222);
        Expect(sync.OnPeerPacket(badPkt), "tick11 checksum 接受");
        auto bad = sync.ChecksumMismatches(11);
        Expect(bad.size() == 1 && bad[0] == 1, "crc 不符抓出 peer1");
    }

    // ---- [整合] Lockstep × UDP loopback ----
    printf("-- UDP loopback 整合 --\n");
    {
        UdpSocket sa, sb;
        sa.Bind(0); sb.Bind(0);
        LockstepSync::Config ca, cb;
        ca.numPeers = 2; ca.localPeerId = 0; ca.inputDelayTicks = 2;
        cb.numPeers = 2; cb.localPeerId = 1; cb.inputDelayTicks = 2;
        LockstepSync la(ca), lb(cb);

        // 跑 5 個 tick:每 tick 各端封口→UDP 互送→收包→推進
        int advanced = 0;
        for (int round = 0; round < 30 && advanced < 5; ++round) {
            la.QueueLocalInput(std::vector<uint8_t>{
                static_cast<uint8_t>('a' + round)});
            lb.QueueLocalInput(std::vector<uint8_t>{
                static_cast<uint8_t>('A' + round)});
            for (auto& p : la.SealLocalPackets())
                sa.SendTo("127.0.0.1", sb.LocalPort(), p.data(), p.size());
            for (auto& p : lb.SealLocalPackets())
                sb.SendTo("127.0.0.1", sa.LocalPort(), p.data(), p.size());
            std::vector<uint8_t> got;
            while (RecvOne(sa, got)) { la.OnPeerPacket(got); }
            while (RecvOne(sb, got)) { lb.OnPeerPacket(got); }
            LockstepSync::TickCommands tc;
            while (la.TryAdvance(tc)) ++advanced;
            while (lb.TryAdvance(tc)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Expect(advanced >= 5, "UDP loopback 推進 ≥5 tick",
               std::to_string(advanced));
        Expect(la.CurrentTick() == lb.CurrentTick(),
               "兩端 tick 同步",
               std::to_string(la.CurrentTick()) + "/" +
               std::to_string(lb.CurrentTick()));
    }

    printf("\n=== 結果: %d PASS / %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
