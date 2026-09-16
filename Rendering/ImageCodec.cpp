#include "Rendering/ImageCodec.h"
#include "Logging/Logger.h"

#include <cstring>
#include <fstream>

namespace Potato {
namespace ImageCodec {

namespace {

// ---------------------------------------------------------------------------
// BitReader / Huffman / Inflate（RFC 1950 zlib + RFC 1951 deflate）
// ---------------------------------------------------------------------------

struct BitReader {
    const uint8* data = nullptr;
    size_t size = 0;
    size_t bytePos = 0;
    uint32 bitBuf = 0;
    int bitCnt = 0;

    // LSB-first 取 n bits；資料用盡時補 0（呼叫端以輸出長度驗證完整性）
    uint32 GetBits(int n) {
        if (n == 0) return 0;
        while (bitCnt < n) {
            uint32 byte = (bytePos < size) ? data[bytePos] : 0;
            ++bytePos;
            bitBuf |= byte << bitCnt;
            bitCnt += 8;
        }
        uint32 v = bitBuf & ((1u << n) - 1u);
        bitBuf >>= n;
        bitCnt -= n;
        return v;
    }

    // 對齊到下一個位元組邊界（stored block 用）
    void AlignByte() {
        int drop = bitCnt & 7;
        bitBuf >>= drop;
        bitCnt -= drop;
        // bitBuf 內剩餘的整位元組推回資料流
        while (bitCnt >= 8) {
            --bytePos;
            bitCnt -= 8;
        }
        bitBuf = 0;
        bitCnt = 0;
    }

    bool ReadByte(uint8& out) {
        if (bytePos >= size) return false;
        out = data[bytePos++];
        return true;
    }
};

// Canonical Huffman（puff 風格：counts + 按 (長度,符號序) 排序的符號表）
struct Huffman {
    uint16 counts[16] = {};
    std::vector<uint16> symbols;

    bool Build(const uint8* lengths, int n) {
        std::memset(counts, 0, sizeof(counts));
        for (int i = 0; i < n; ++i) {
            if (lengths[i] > 15) return false;
            ++counts[lengths[i]];
        }
        counts[0] = 0;
        symbols.resize(static_cast<size_t>(n));
        int offs[16] = {};
        for (int len = 1; len < 16; ++len)
            offs[len] = offs[len - 1] + counts[len - 1];
        for (int i = 0; i < n; ++i)
            if (lengths[i] != 0)
                symbols[static_cast<size_t>(offs[lengths[i]]++)] = static_cast<uint16>(i);
        return true;
    }

    // 回傳符號；失敗 -1。資料用盡時 GetBits 補 0,可能誤解碼——由上層以輸出長度判定
    int Decode(BitReader& br) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            code |= static_cast<int>(br.GetBits(1));
            int count = counts[len];
            if (code - first < count)
                return symbols[static_cast<size_t>(index + (code - first))];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return -1;
    }
};

const int kLenBase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
const int kLenExtra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
const int kDistBase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
const int kDistExtra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
const uint8 kCLenOrder[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

bool BuildFixedTables(Huffman& lit, Huffman& dist) {
    uint8 lens[288];
    for (int i = 0; i < 144; ++i) lens[i] = 8;
    for (int i = 144; i < 256; ++i) lens[i] = 9;
    for (int i = 256; i < 280; ++i) lens[i] = 7;
    for (int i = 280; i < 288; ++i) lens[i] = 8;
    if (!lit.Build(lens, 288)) return false;
    uint8 dlens[30];
    std::memset(dlens, 5, sizeof(dlens));
    return dist.Build(dlens, 30);
}

// 解一個 deflate block 的 literal/length 序列
bool InflateCodes(BitReader& br, const Huffman& lit, const Huffman& dist,
                  std::vector<uint8>& out) {
    for (;;) {
        int sym = lit.Decode(br);
        if (sym < 0) return false;
        if (sym < 256) {
            out.push_back(static_cast<uint8>(sym));
            continue;
        }
        if (sym == 256) return true;  // block 結束
        sym -= 257;
        if (sym >= 29) return false;
        size_t length = static_cast<size_t>(kLenBase[sym]) + br.GetBits(kLenExtra[sym]);

        int dsym = dist.Decode(br);
        if (dsym < 0 || dsym >= 30) return false;
        size_t distance = static_cast<size_t>(kDistBase[dsym]) + br.GetBits(kDistExtra[dsym]);
        if (distance == 0 || distance > out.size()) return false;

        // 重疊複製需逐位元組
        size_t src = out.size() - distance;
        for (size_t i = 0; i < length; ++i)
            out.push_back(out[src + i]);
    }
}

// 解析 raw deflate 串流（不含 zlib header）
bool InflateRaw(const uint8* data, size_t size, std::vector<uint8>& out) {
    BitReader br;
    br.data = data;
    br.size = size;
    out.clear();

    for (;;) {
        uint32 bfinal = br.GetBits(1);
        uint32 btype = br.GetBits(2);

        if (btype == 0) {
            br.AlignByte();
            uint8 b[4];
            for (int i = 0; i < 4; ++i)
                if (!br.ReadByte(b[i])) return false;
            uint32 len = static_cast<uint32>(b[0]) | (static_cast<uint32>(b[1]) << 8);
            uint32 nlen = static_cast<uint32>(b[2]) | (static_cast<uint32>(b[3]) << 8);
            if ((len & 0xFFFFu) != ((~nlen) & 0xFFFFu)) return false;
            for (uint32 i = 0; i < len; ++i) {
                uint8 v;
                if (!br.ReadByte(v)) return false;
                out.push_back(v);
            }
        } else if (btype == 1) {
            Huffman lit, dist;
            if (!BuildFixedTables(lit, dist)) return false;
            if (!InflateCodes(br, lit, dist, out)) return false;
        } else if (btype == 2) {
            int hlit = static_cast<int>(br.GetBits(5)) + 257;
            int hdist = static_cast<int>(br.GetBits(5)) + 1;
            int hclen = static_cast<int>(br.GetBits(4)) + 4;
            if (hlit > 286 || hdist > 30) return false;

            uint8 clens[19] = {};
            for (int i = 0; i < hclen; ++i)
                clens[kCLenOrder[i]] = static_cast<uint8>(br.GetBits(3));
            Huffman clHuff;
            if (!clHuff.Build(clens, 19)) return false;

            std::vector<uint8> lens(static_cast<size_t>(hlit + hdist), 0);
            int i = 0;
            while (i < hlit + hdist) {
                int sym = clHuff.Decode(br);
                if (sym < 0 || sym > 18) return false;
                if (sym < 16) {
                    lens[static_cast<size_t>(i++)] = static_cast<uint8>(sym);
                } else {
                    int repeat, value;
                    if (sym == 16) {
                        if (i == 0) return false;
                        value = lens[static_cast<size_t>(i - 1)];
                        repeat = 3 + static_cast<int>(br.GetBits(2));
                    } else if (sym == 17) {
                        value = 0;
                        repeat = 3 + static_cast<int>(br.GetBits(3));
                    } else {
                        value = 0;
                        repeat = 11 + static_cast<int>(br.GetBits(7));
                    }
                    if (i + repeat > hlit + hdist) return false;
                    while (repeat-- > 0)
                        lens[static_cast<size_t>(i++)] = static_cast<uint8>(value);
                }
            }
            if (lens[256] == 0) return false;  // 缺 end-of-block 符號

            Huffman lit, dist;
            if (!lit.Build(lens.data(), hlit)) return false;
            if (!dist.Build(lens.data() + hlit, hdist)) return false;
            if (!InflateCodes(br, lit, dist, out)) return false;
        } else {
            return false;  // btype==3 保留
        }

        if (bfinal) break;
    }
    return true;
}

// zlib 包裝：CMF/FLG + deflate + adler32
bool InflateZlib(const uint8* data, size_t size, std::vector<uint8>& out,
                 std::string* err) {
    if (size < 6) {
        if (err) *err = "zlib stream too short";
        return false;
    }
    uint8 cmf = data[0], flg = data[1];
    if ((cmf & 0x0F) != 8 || (((static_cast<int>(cmf) << 8) + flg) % 31) != 0) {
        if (err) *err = "invalid zlib header";
        return false;
    }
    if (flg & 0x20) {
        if (err) *err = "preset dictionary not supported";
        return false;
    }
    if (!InflateRaw(data + 2, size - 2, out)) {
        if (err) *err = "deflate inflate failed";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// CRC32（PNG chunk 驗證 + 寫檔用）
// ---------------------------------------------------------------------------

uint32 Crc32(const uint8* data, size_t size, uint32 crc = 0xFFFFFFFFu) {
    static uint32 table[256];
    static bool init = false;
    if (!init) {
        for (uint32 i = 0; i < 256; ++i) {
            uint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

uint32 ReadBE32(const uint8* p) {
    return (static_cast<uint32>(p[0]) << 24) | (static_cast<uint32>(p[1]) << 16) |
           (static_cast<uint32>(p[2]) << 8) | static_cast<uint32>(p[3]);
}

// ---------------------------------------------------------------------------
// PNG 解碼
// ---------------------------------------------------------------------------

const uint8 kPngSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

int ChannelsForColorType(int ct) {
    switch (ct) {
        case 0: return 1;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        case 6: return 4;
        default: return 0;
    }
}

// 8-bit 掃描列反濾波；lineSz = w*channels
bool Unfilter(uint8* raw, int w, int h, int channels, std::string* err) {
    size_t lineSz = static_cast<size_t>(w) * channels;
    size_t stride = lineSz + 1;  // 每列前有 1 byte 濾波型別
    uint8* prev = nullptr;

    for (int y = 0; y < h; ++y) {
        uint8* line = raw + static_cast<size_t>(y) * stride;
        uint8 filter = line[0];
        uint8* px = line + 1;
        switch (filter) {
            case 0: break;
            case 1:
                for (size_t x = static_cast<size_t>(channels); x < lineSz; ++x)
                    px[x] = static_cast<uint8>(px[x] + px[x - channels]);
                break;
            case 2:
                if (prev) {
                    for (size_t x = 0; x < lineSz; ++x)
                        px[x] = static_cast<uint8>(px[x] + prev[x]);
                }
                break;
            case 3:
                for (size_t x = 0; x < lineSz; ++x) {
                    int a = (x >= static_cast<size_t>(channels)) ? px[x - channels] : 0;
                    int b = prev ? prev[x] : 0;
                    px[x] = static_cast<uint8>(px[x] + ((a + b) >> 1));
                }
                break;
            case 4: {
                for (size_t x = 0; x < lineSz; ++x) {
                    int a = (x >= static_cast<size_t>(channels)) ? px[x - channels] : 0;
                    int b = prev ? prev[x] : 0;
                    int c = (prev && x >= static_cast<size_t>(channels)) ? prev[x - channels] : 0;
                    int p = a + b - c;
                    int pa = p - a; if (pa < 0) pa = -pa;
                    int pb = p - b; if (pb < 0) pb = -pb;
                    int pc = p - c; if (pc < 0) pc = -pc;
                    int pred = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                    px[x] = static_cast<uint8>(px[x] + pred);
                }
                break;
            }
            default:
                if (err) *err = "unknown PNG filter type";
                return false;
        }
        prev = px;
    }
    return true;
}

} // namespace

bool DecodePNG(const uint8* data, size_t size,
               std::vector<uint8>& outRGBA, int& outW, int& outH,
               std::string* err) {
    auto fail = [&](const char* msg) {
        if (err) *err = msg;
        return false;
    };

    if (!data || size < 8 || std::memcmp(data, kPngSig, 8) != 0)
        return fail("not a PNG file");

    int w = 0, h = 0, bitDepth = 0, colorType = -1, interlace = -1;
    std::vector<uint8> idat;
    std::vector<uint8> palette;      // RGB, 每項 3 bytes
    std::vector<uint8> paletteAlpha; // tRNS（可選）
    uint32 trnsGray = 0x10000, trnsR = 0x10000, trnsG = 0x10000, trnsB = 0x10000;
    bool seenIEND = false;

    size_t pos = 8;
    while (pos + 12 <= size && !seenIEND) {
        uint32 len = ReadBE32(data + pos);
        const uint8* type = data + pos + 4;
        const uint8* payload = data + pos + 8;
        if (pos + 12 + len > size) return fail("truncated PNG chunk");
        pos += 8;

        uint32 crcExpect = ReadBE32(payload + len);
        uint32 crc = Crc32(type, 4, 0xFFFFFFFFu);
        crc = Crc32(payload, len, crc);
        if ((crc ^ 0xFFFFFFFFu) != crcExpect)
            return fail("PNG chunk CRC mismatch");

        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len != 13) return fail("bad IHDR length");
            w = static_cast<int>(ReadBE32(payload));
            h = static_cast<int>(ReadBE32(payload + 4));
            bitDepth = payload[8];
            colorType = payload[9];
            interlace = payload[12];
            if (w <= 0 || h <= 0 || w > 16384 || h > 16384)
                return fail("invalid PNG dimensions");
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            if (len % 3 != 0 || len > 768) return fail("bad PLTE");
            palette.assign(payload, payload + len);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            if (colorType == 3) {
                paletteAlpha.assign(payload, payload + len);
            } else if (len >= 2) {
                if (colorType == 0) {
                    trnsGray = ReadBE32(payload) >> 16;  // tRNS 是 16-bit 值
                    trnsGray = (ReadBE32(payload) >> 16);  // 取高 16 位的前 2 bytes
                }
                if (len >= 6 && colorType == 2) {
                    trnsR = (payload[0] << 8) | payload[1];
                    trnsG = (payload[2] << 8) | payload[3];
                    trnsB = (payload[4] << 8) | payload[5];
                }
            }
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), payload, payload + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            seenIEND = true;
        }
        pos += len + 4;  // payload + CRC
    }

    if (!seenIEND) return fail("missing IEND");
    if (idat.empty()) return fail("missing IDAT");
    if (bitDepth != 8) return fail("only 8-bit PNG supported");
    if (interlace != 0) return fail("interlaced PNG not supported");
    int channels = ChannelsForColorType(colorType);
    if (channels == 0) return fail("unsupported color type");
    if (colorType == 3 && palette.empty()) return fail("palette image without PLTE");

    std::vector<uint8> raw;
    if (!InflateZlib(idat.data(), idat.size(), raw, err))
        return false;

    size_t lineSz = static_cast<size_t>(w) * channels;
    size_t expect = (lineSz + 1) * static_cast<size_t>(h);
    if (raw.size() != expect) return fail("PNG decompressed size mismatch");

    if (!Unfilter(raw.data(), w, h, channels, err))
        return false;

    outRGBA.resize(static_cast<size_t>(w) * h * 4);
    outW = w;
    outH = h;

    size_t stride = lineSz + 1;
    for (int y = 0; y < h; ++y) {
        const uint8* px = raw.data() + static_cast<size_t>(y) * stride + 1;
        uint8* dst = outRGBA.data() + static_cast<size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            const uint8* s = px + static_cast<size_t>(x) * channels;
            uint8* d = dst + static_cast<size_t>(x) * 4;
            switch (colorType) {
                case 0: {
                    uint8 g = s[0];
                    d[0] = d[1] = d[2] = g;
                    d[3] = (trnsGray <= 0xFF && g == (trnsGray & 0xFF)) ? 0 : 255;
                    break;
                }
                case 2:
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                    d[3] = (s[0] == (trnsR & 0xFF) && s[1] == (trnsG & 0xFF) &&
                            s[2] == (trnsB & 0xFF)) ? 0 : 255;
                    break;
                case 3: {
                    size_t idx = s[0];
                    if (idx * 3 + 2 >= palette.size()) return fail("palette index OOB");
                    d[0] = palette[idx * 3];
                    d[1] = palette[idx * 3 + 1];
                    d[2] = palette[idx * 3 + 2];
                    d[3] = (idx < paletteAlpha.size()) ? paletteAlpha[idx] : 255;
                    break;
                }
                case 4:
                    d[0] = d[1] = d[2] = s[0];
                    d[3] = s[1];
                    break;
                case 6:
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                    break;
            }
        }
    }
    return true;
}

bool DecodeImage(const uint8* data, size_t size,
                 std::vector<uint8>& outRGBA, int& outW, int& outH,
                 std::string* err) {
    if (data && size >= 8 && std::memcmp(data, kPngSig, 8) == 0)
        return DecodePNG(data, size, outRGBA, outW, outH, err);
    if (data && size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        if (err) *err = "JPEG decoding not supported (use PNG textures)";
        return false;
    }
    if (err) *err = "unrecognized image format";
    return false;
}

// ---------------------------------------------------------------------------
// PNG 編碼（無壓縮 zlib stored blocks）
// ---------------------------------------------------------------------------

namespace {

void AppendBE32(std::vector<uint8>& out, uint32 v) {
    out.push_back(static_cast<uint8>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8>(v & 0xFF));
}

void AppendChunk(std::vector<uint8>& out, const char* type,
                 const uint8* payload, size_t len) {
    AppendBE32(out, static_cast<uint32>(len));
    size_t typePos = out.size();
    out.insert(out.end(), reinterpret_cast<const uint8*>(type),
               reinterpret_cast<const uint8*>(type) + 4);
    if (payload && len)
        out.insert(out.end(), payload, payload + len);
    uint32 crc = Crc32(out.data() + typePos, out.size() - typePos);
    AppendBE32(out, crc ^ 0xFFFFFFFFu);
}

uint32 Adler32(const uint8* data, size_t size) {
    uint32 a = 1, b = 0;
    for (size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

} // namespace

bool EncodePNG(int w, int h, const uint8* rgba,
               std::vector<uint8>& outPNG, std::string* err) {
    if (w <= 0 || h <= 0 || !rgba) {
        if (err) *err = "invalid image parameters";
        return false;
    }

    // scanline 資料：每列加 1 byte filter=0
    size_t lineSz = static_cast<size_t>(w) * 4;
    std::vector<uint8> scan(static_cast<size_t>(h) * (lineSz + 1));
    for (int y = 0; y < h; ++y) {
        uint8* line = scan.data() + static_cast<size_t>(y) * (lineSz + 1);
        line[0] = 0;
        std::memcpy(line + 1, rgba + static_cast<size_t>(y) * lineSz, lineSz);
    }

    // zlib：header 0x78 0x01 + stored blocks(<=65535) + adler32
    std::vector<uint8> idat;
    idat.push_back(0x78);
    idat.push_back(0x01);
    size_t remaining = scan.size();
    const uint8* sp = scan.data();
    while (remaining > 0) {
        uint32 blk = static_cast<uint32>(remaining > 65535 ? 65535 : remaining);
        bool final = (remaining <= 65535);
        idat.push_back(final ? 1 : 0);
        idat.push_back(static_cast<uint8>(blk & 0xFF));
        idat.push_back(static_cast<uint8>((blk >> 8) & 0xFF));
        uint32 nlen = (~blk) & 0xFFFFu;
        idat.push_back(static_cast<uint8>(nlen & 0xFF));
        idat.push_back(static_cast<uint8>((nlen >> 8) & 0xFF));
        idat.insert(idat.end(), sp, sp + blk);
        sp += blk;
        remaining -= blk;
    }
    uint32 adler = Adler32(scan.data(), scan.size());
    AppendBE32(idat, adler);

    outPNG.clear();
    outPNG.insert(outPNG.end(), kPngSig, kPngSig + 8);

    uint8 ihdr[13];
    ihdr[0] = static_cast<uint8>((w >> 24) & 0xFF);
    ihdr[1] = static_cast<uint8>((w >> 16) & 0xFF);
    ihdr[2] = static_cast<uint8>((w >> 8) & 0xFF);
    ihdr[3] = static_cast<uint8>(w & 0xFF);
    ihdr[4] = static_cast<uint8>((h >> 24) & 0xFF);
    ihdr[5] = static_cast<uint8>((h >> 16) & 0xFF);
    ihdr[6] = static_cast<uint8>((h >> 8) & 0xFF);
    ihdr[7] = static_cast<uint8>(h & 0xFF);
    ihdr[8] = 8;   // bit depth
    ihdr[9] = 6;   // RGBA
    ihdr[10] = 0;  // compression
    ihdr[11] = 0;  // filter
    ihdr[12] = 0;  // interlace
    AppendChunk(outPNG, "IHDR", ihdr, 13);
    AppendChunk(outPNG, "IDAT", idat.data(), idat.size());
    AppendChunk(outPNG, "IEND", nullptr, 0);
    return true;
}

bool WritePNGFile(const std::string& path, int w, int h, const uint8* rgba,
                  std::string* err) {
    std::vector<uint8> png;
    if (!EncodePNG(w, h, rgba, png, err))
        return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open output file: " + path;
        return false;
    }
    f.write(reinterpret_cast<const char*>(png.data()),
            static_cast<std::streamsize>(png.size()));
    if (!f) {
        if (err) *err = "write failed: " + path;
        return false;
    }
    return true;
}

} // namespace ImageCodec
} // namespace Potato
