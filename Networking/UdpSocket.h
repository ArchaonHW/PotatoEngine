#pragma once

/**
 * UdpSocket - 跨平台 UDP 傳輸薄封裝
 *
 * 範圍：單一 datagram socket,non-blocking 收、阻塞式送。
 * 不含可靠性/重傳/連線概念——lockstep/狀態同步由上層協議處理,
 * 需要可靠傳輸時上層自行加序號與 ack(或後續在此層上實作)。
 *
 * Windows 走 Winsock2（首個 socket 建立時 WSAStartup,最後一個關閉時
 * WSACleanup，以原子計數管理）;POSIX 走 BSD sockets。
 */

#include <cstddef>
#include <cstdint>
#include <string>

namespace Potato {

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // port=0 由系統分配 ephemeral port;失敗回 false
    bool Bind(uint16_t port);
    void Close();
    bool IsOpen() const { return fd != kInvalidFd; }
    // 實際綁定的 port(Bind(0) 後查詢用)
    uint16_t LocalPort() const;

    // 傳送 datagram 到 addr:port（IPv4 點分字串或 "localhost"）
    bool SendTo(const std::string& addr, uint16_t port,
                const void* data, size_t len);

    // 非阻塞接收。回傳收到位元組數；無資料回 0,錯誤回 -1。
    // fromAddr/fromPort 可為 nullptr 不取。
    int RecvFrom(void* buf, size_t len,
                 std::string* fromAddr = nullptr,
                 uint16_t* fromPort = nullptr);

private:
    static constexpr intptr_t kInvalidFd = -1;
    intptr_t fd = kInvalidFd;  // SOCKET/int 統一用 intptr_t 存
};

} // namespace Potato
