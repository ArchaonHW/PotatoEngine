#include "HttpClient.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace Potato {

namespace {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                        static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string WinErr(const char* what) {
    DWORD e = GetLastError();
    return std::string(what) + " (win32=" + std::to_string(e) + ")";
}

} // namespace

HttpResponse HttpClient::Post(const std::string& url, const std::string& body,
                              const Headers& headers, uint32_t timeoutMs) {
    return Request("POST", url, body, headers, timeoutMs);
}

HttpResponse HttpClient::Get(const std::string& url, const Headers& headers,
                             uint32_t timeoutMs) {
    return Request("GET", url, "", headers, timeoutMs);
}

HttpResponse HttpClient::Request(const char* method, const std::string& url,
                                 const std::string& body,
                                 const Headers& headers, uint32_t timeoutMs) {
    HttpResponse out;

    // --- URL 拆解 ---
    std::wstring wurl = Utf8ToWide(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{};
    wchar_t path[2048]{};
    uc.lpszHostName = host;
    uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        out.error = WinErr("URL 解析失敗");
        return out;
    }
    bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET hSession = WinHttpOpen(L"PotatoEngine/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        out.error = WinErr("WinHttpOpen 失敗");
        return out;
    }
    struct Guard {
        HINTERNET h;
        ~Guard() { if (h) WinHttpCloseHandle(h); }
    };
    Guard gSession{hSession};

    WinHttpSetTimeouts(hSession,
        static_cast<int>(timeoutMs), static_cast<int>(timeoutMs),
        static_cast<int>(timeoutMs), static_cast<int>(timeoutMs));

    HINTERNET hConn = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConn) {
        out.error = WinErr("WinHttpConnect 失敗");
        return out;
    }
    Guard gConn{hConn};

    HINTERNET hReq = WinHttpOpenRequest(hConn, Utf8ToWide(method).c_str(),
        path, nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0);
    if (!hReq) {
        out.error = WinErr("WinHttpOpenRequest 失敗");
        return out;
    }
    Guard gReq{hReq};

    // --- 自訂標頭（合併成單一區塊一次帶入）---
    std::wstring headerBlock;
    for (const auto& [k, v] : headers) {
        headerBlock += Utf8ToWide(k);
        headerBlock += L": ";
        headerBlock += Utf8ToWide(v);
        headerBlock += L"\r\n";
    }
    if (!body.empty() &&
        headerBlock.find(L"Content-Type") == std::wstring::npos) {
        headerBlock += L"Content-Type: application/json\r\n";
    }

    std::wstring extraHeaders = headerBlock.empty()
        ? std::wstring(WINHTTP_NO_ADDITIONAL_HEADERS) : headerBlock;

    if (!WinHttpSendRequest(hReq,
            extraHeaders.c_str(),
            static_cast<DWORD>(extraHeaders.size()),
            const_cast<char*>(body.data()),
            static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()), 0)) {
        out.error = WinErr("WinHttpSendRequest 失敗");
        return out;
    }
    if (!WinHttpReceiveResponse(hReq, nullptr)) {
        out.error = WinErr("WinHttpReceiveResponse 失敗");
        return out;
    }

    // --- 狀態碼 ---
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(hReq,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    out.status = static_cast<int>(status);

    // --- 本文 ---
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hReq, &avail)) {
            out.error = WinErr("WinHttpQueryDataAvailable 失敗");
            return out;
        }
        if (avail == 0) break;
        size_t off = out.body.size();
        out.body.resize(off + avail);
        DWORD got = 0;
        if (!WinHttpReadData(hReq, out.body.data() + off, avail, &got)) {
            out.error = WinErr("WinHttpReadData 失敗");
            return out;
        }
        out.body.resize(off + got);
    }
    return out;
}

} // namespace Potato

#else // 非 Windows：stub

namespace Potato {

HttpResponse HttpClient::Post(const std::string&, const std::string&,
                              const Headers&, uint32_t) {
    return {0, "", "HttpClient 僅實作 Windows（WinHTTP）後端"};
}

HttpResponse HttpClient::Get(const std::string&, const Headers&, uint32_t) {
    return {0, "", "HttpClient 僅實作 Windows（WinHTTP）後端"};
}

HttpResponse HttpClient::Request(const char*, const std::string&,
                                 const std::string&, const Headers&, uint32_t) {
    return {0, "", "HttpClient 僅實作 Windows（WinHTTP）後端"};
}

} // namespace Potato

#endif
