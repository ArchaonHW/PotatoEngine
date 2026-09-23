#include "Networking/LockstepSync.h"

#include <cstring>

namespace Potato {

namespace {

void PutU16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}

void PutU32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

bool GetU16(const uint8_t*& p, const uint8_t* end, uint16_t& x) {
    if (end - p < 2) return false;
    x = static_cast<uint16_t>(p[0] | (p[1] << 8));
    p += 2;
    return true;
}

bool GetU32(const uint8_t*& p, const uint8_t* end, uint32_t& x) {
    if (end - p < 4) return false;
    x = static_cast<uint32_t>(p[0]) | (p[1] << 8) |
        (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
    p += 4;
    return true;
}

} // anonymous namespace

LockstepSync::LockstepSync(const Config& c) : cfg(c) {
    if (cfg.numPeers < 1) cfg.numPeers = 1;
    if (cfg.numPeers > 255) cfg.numPeers = 255;
    if (cfg.localPeerId < 0 || cfg.localPeerId >= cfg.numPeers)
        cfg.localPeerId = 0;
    if (cfg.inputDelayTicks < 1) cfg.inputDelayTicks = 1;

    bundles.resize(cfg.numPeers);
    peerMaxTick.assign(cfg.numPeers, -1);
    checksums.resize(cfg.numPeers);
    lastSealedTick = cfg.inputDelayTicks - 1;  // < delay 的 tick 無需發包

    // tick < delay 恆為全員空指令——直接預填（含遠端），
    // 這些 tick 永遠不會有合法指令（輸入最早也只能落在 tick delay）
    for (int t = 0; t < cfg.inputDelayTicks; ++t) {
        for (auto& m : bundles) m[t] = {};
    }
}

void LockstepSync::QueueLocalInput(const std::vector<uint8_t>& data) {
    pendingLocal.push_back(data);
}

std::vector<uint8_t> LockstepSync::EncodeCommandPacket(
    int peerId, int tick,
    const std::vector<std::vector<uint8_t>>& blobs) {
    std::vector<uint8_t> pkt;
    pkt.reserve(9 + blobs.size() * 2);
    pkt.push_back('L'); pkt.push_back('K');
    pkt.push_back(kWireVersion);
    pkt.push_back(static_cast<uint8_t>(peerId));
    PutU32(pkt, static_cast<uint32_t>(tick));
    PutU16(pkt, static_cast<uint16_t>(blobs.size()));
    for (const auto& b : blobs) {
        PutU16(pkt, static_cast<uint16_t>(b.size()));
        pkt.insert(pkt.end(), b.begin(), b.end());
    }
    return pkt;
}

std::vector<uint8_t> LockstepSync::EncodeChecksumPacket(int peerId,
                                                      int tick,
                                                      uint32_t crc) {
    std::vector<uint8_t> pkt;
    pkt.reserve(10);
    pkt.push_back('C'); pkt.push_back('K');
    pkt.push_back(kWireVersion);
    pkt.push_back(static_cast<uint8_t>(peerId));
    PutU32(pkt, static_cast<uint32_t>(tick));
    PutU32(pkt, crc);
    return pkt;
}

std::vector<std::vector<uint8_t>> LockstepSync::SealLocalPackets() {
    std::vector<std::vector<uint8_t>> out;
    const int target = currentTick + cfg.inputDelayTicks;
    while (lastSealedTick < target) {
        const int t = ++lastSealedTick;
        // 最後一包帶走排入的輸入，補齊的中間包為空
        const bool last = (lastSealedTick == target);
        auto& blobList = bundles[cfg.localPeerId][t];
        if (last) {
            blobList = pendingLocal;
            pendingLocal.clear();
        }
        out.push_back(EncodeCommandPacket(cfg.localPeerId, t, blobList));
        if (t > peerMaxTick[cfg.localPeerId]) {
            peerMaxTick[cfg.localPeerId] = t;
        }
    }
    return out;
}

bool LockstepSync::OnPeerPacket(const std::vector<uint8_t>& pkt) {
    if (pkt.size() < 4) return false;
    if (pkt[0] == 'L' && pkt[1] == 'K') return DecodeCommandPacket(pkt);
    if (pkt[0] == 'C' && pkt[1] == 'K') return DecodeChecksumPacket(pkt);
    return false;
}

bool LockstepSync::DecodeCommandPacket(const std::vector<uint8_t>& pkt) {
    const uint8_t* p = pkt.data() + 2;
    const uint8_t* end = pkt.data() + pkt.size();
    if (*p++ != kWireVersion) return false;
    const int peerId = *p++;
    if (peerId < 0 || peerId >= cfg.numPeers ||
        peerId == cfg.localPeerId) {
        return false;  // 本地包不應從網路回來
    }
    uint32_t tick;
    uint16_t n;
    if (!GetU32(p, end, tick) || !GetU16(p, end, n)) return false;
    if (static_cast<int>(tick) < cfg.inputDelayTicks) return false;
    if (static_cast<int>(tick) < currentTick) return false;  // 已過期

    std::vector<std::vector<uint8_t>> blobs;
    blobs.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t len;
        if (!GetU16(p, end, len) || end - p < len) return false;
        blobs.emplace_back(p, p + len);
        p += len;
    }
    bundles[peerId][static_cast<int>(tick)] = std::move(blobs);
    if (static_cast<int>(tick) > peerMaxTick[peerId]) {
        peerMaxTick[peerId] = static_cast<int>(tick);
    }
    return true;
}

bool LockstepSync::DecodeChecksumPacket(const std::vector<uint8_t>& pkt) {
    if (pkt.size() != 12) return false;  // 2 magic + ver + peerId + tick + crc
    const uint8_t* p = pkt.data() + 2;
    const uint8_t* end = pkt.data() + pkt.size();
    if (*p++ != kWireVersion) return false;
    const int peerId = *p++;
    if (peerId < 0 || peerId >= cfg.numPeers) return false;
    uint32_t tick, crc;
    if (!GetU32(p, end, tick) || !GetU32(p, end, crc)) return false;
    checksums[peerId][static_cast<int>(tick)] = crc;
    return true;
}

bool LockstepSync::ReadyToAdvance() const {
    for (int peer = 0; peer < cfg.numPeers; ++peer) {
        if (bundles[peer].find(currentTick) == bundles[peer].end()) {
            return false;
        }
    }
    return true;
}

bool LockstepSync::TryAdvance(TickCommands& out) {
    if (!ReadyToAdvance()) return false;
    out.tick = currentTick;
    out.inputs.clear();
    for (int peer = 0; peer < cfg.numPeers; ++peer) {
        for (auto& blob : bundles[peer][currentTick]) {
            out.inputs.emplace_back(peer, std::move(blob));
        }
        // 清掉已消費的歷史包
        auto& m = bundles[peer];
        m.erase(m.begin(), m.upper_bound(currentTick));
    }
    ++currentTick;
    return true;
}

int LockstepSync::PeerMaxTick(int peerId) const {
    if (peerId < 0 || peerId >= cfg.numPeers) return -1;
    return peerMaxTick[peerId];
}

void LockstepSync::RecordLocalChecksum(int tick, uint32_t crc) {
    checksums[cfg.localPeerId][tick] = crc;
}

std::vector<int> LockstepSync::ChecksumMismatches(int tick) const {
    std::vector<int> bad;
    const auto lit = checksums[cfg.localPeerId].find(tick);
    if (lit == checksums[cfg.localPeerId].end()) return bad;  // 無本地基準
    const uint32_t ref = lit->second;
    for (int peer = 0; peer < cfg.numPeers; ++peer) {
        if (peer == cfg.localPeerId) continue;
        const auto it = checksums[peer].find(tick);
        if (it != checksums[peer].end() && it->second != ref) {
            bad.push_back(peer);
        }
    }
    return bad;
}

} // namespace Potato
