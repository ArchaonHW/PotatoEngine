#include "Networking/UdpSocket.h"

#include <atomic>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Potato {

namespace {

#ifdef _WIN32
using SockT = SOCKET;
std::atomic<int> g_wsaUsers{0};

// Winsock 參考計數：第一個 socket 起 WSA,最後一個收掉
bool WsaAcquire() {
    if (g_wsaUsers.fetch_add(1) == 0) {
        WSADATA wd;
        if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) {
            g_wsaUsers.fetch_sub(1);
            return false;
        }
    }
    return true;
}

void WsaRelease() {
    if (g_wsaUsers.fetch_sub(1) == 1) WSACleanup();
}
#else
using SockT = int;
#endif

SockT ToSock(intptr_t fd) { return static_cast<SockT>(fd); }

// 解析 IPv4 字串/hostname → sockaddr_in;失敗回 false
bool ResolveAddr(const std::string& addr, uint16_t port,
                 sockaddr_in& out) {
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    if (inet_pton(AF_INET, addr.c_str(), &out.sin_addr) == 1) return true;
    // hostname 解析（localhost 等）
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(addr.c_str(), nullptr, &hints, &res) != 0 || !res)
        return false;
    out.sin_addr =
        reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return true;
}

} // anonymous namespace

UdpSocket::UdpSocket() = default;

UdpSocket::~UdpSocket() { Close(); }

bool UdpSocket::Bind(uint16_t port) {
    if (IsOpen()) return false;
#ifdef _WIN32
    if (!WsaAcquire()) return false;
    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { WsaRelease(); return false; }
    // 非阻塞
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return false;
    const int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
#ifdef _WIN32
        closesocket(s);
        WsaRelease();
#else
        close(s);
#endif
        return false;
    }
    fd = static_cast<intptr_t>(s);
    return true;
}

void UdpSocket::Close() {
    if (!IsOpen()) return;
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(fd));
    WsaRelease();
#else
    close(static_cast<int>(fd));
#endif
    fd = kInvalidFd;
}

uint16_t UdpSocket::LocalPort() const {
    if (!IsOpen()) return 0;
    sockaddr_in sa{};
    socklen_t len = sizeof(sa);
    if (getsockname(ToSock(fd), reinterpret_cast<sockaddr*>(&sa),
                    &len) != 0) {
        return 0;
    }
    return ntohs(sa.sin_port);
}

bool UdpSocket::SendTo(const std::string& addr, uint16_t port,
                       const void* data, size_t len) {
    if (!IsOpen()) return false;
    sockaddr_in sa;
    if (!ResolveAddr(addr, port, sa)) return false;
    const int sent = sendto(ToSock(fd), static_cast<const char*>(data),
                            static_cast<int>(len), 0,
                            reinterpret_cast<sockaddr*>(&sa),
                            sizeof(sa));
    return sent == static_cast<int>(len);
}

int UdpSocket::RecvFrom(void* buf, size_t len,
                        std::string* fromAddr, uint16_t* fromPort) {
    if (!IsOpen()) return -1;
    sockaddr_in sa{};
    socklen_t slen = sizeof(sa);
    const int n = recvfrom(ToSock(fd), static_cast<char*>(buf),
                           static_cast<int>(len), 0,
                           reinterpret_cast<sockaddr*>(&sa), &slen);
    if (n < 0) {
#ifdef _WIN32
        return (WSAGetLastError() == WSAEWOULDBLOCK) ? 0 : -1;
#else
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
#endif
    }
    if (fromAddr) {
        char ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
        *fromAddr = ip;
    }
    if (fromPort) *fromPort = ntohs(sa.sin_port);
    return n;
}

} // namespace Potato
