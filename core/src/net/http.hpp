#pragma once

// Minimal HTTPS client. Windows uses WinHTTP (part of Windows, no extra dependency).
// Other platforms have no implementation yet; tests use a fake transport instead.

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace smz {

struct HttpResponse {
    int status = 0;          // HTTP status; 0 if the request could not be sent
    std::string body;
    std::string error;       // transport error (no connection, timeout, ...)
};

struct HttpRequest {
    std::string url;         // https://host/path?query, or http://127.0.0.1:port/path
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int timeout_seconds = 120;
};

using HttpTransport = std::function<HttpResponse(const HttpRequest&)>;

// Sends a POST. HTTPS uses the system's proxy settings and certificate store; plain HTTP is
// allowed only to this computer (127.0.0.1 / localhost), for local model servers.
HttpResponse http_post(const HttpRequest& request);

}  // namespace smz
