#pragma once

/**
 * HttpClient - HTTP(S) 請求薄封裝
 *
 * 範圍：同步單發請求（LLM API、webhook 等短連線場景）。
 * 不 pooling/不 streaming——需要串流回應時上層自行擴充。
 * Windows 走 WinHTTP；其他平台回傳 error 的 stub。
 */

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Potato {

struct HttpResponse {
    int status = 0;         // HTTP status code（0 = 傳輸層失敗未收到回應）
    std::string body;       // 回應本文（原樣位元組，通常 UTF-8）
    std::string error;      // 傳輸層錯誤描述；空 = 傳輸成功（HTTP 錯誤碼仍可能非 2xx）

    bool TransportOk() const { return error.empty() && status > 0; }
    bool Ok() const { return error.empty() && status >= 200 && status < 300; }
};

class HttpClient {
public:
    using Headers = std::vector<std::pair<std::string, std::string>>;

    // url 例："http://localhost:11434/v1/chat/completions"
    //      "https://api.openai.com/v1/chat/completions"
    static HttpResponse Post(const std::string& url, const std::string& body,
                             const Headers& headers = {},
                             uint32_t timeoutMs = 60000);
    static HttpResponse Get(const std::string& url,
                            const Headers& headers = {},
                            uint32_t timeoutMs = 30000);

private:
    static HttpResponse Request(const char* method, const std::string& url,
                                const std::string& body,
                                const Headers& headers, uint32_t timeoutMs);
};

} // namespace Potato
