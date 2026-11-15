#include "net/http.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <winhttp.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
#  include <cstdint>
#  include <cstdlib>
#endif

namespace smz {

#if defined(_WIN32)

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string last_error(const char* what) {
    return std::string(what) + " failed (Windows error " + std::to_string(GetLastError()) + ")";
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h != nullptr) {
            WinHttpCloseHandle(h);
        }
    }
};

}  // namespace

HttpResponse http_post(const HttpRequest& request) {
    HttpResponse response;
    const std::wstring url = widen(request.url);

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        response.error = "invalid URL";
        return response;
    }
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    const std::wstring host_name(host);
    const bool local = host_name == L"127.0.0.1" || host_name == L"localhost" || host_name == L"::1";
    if (!secure && !local) {
        response.error = "plain http is only allowed to this computer";
        return response;
    }
    const std::wstring path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
                              std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    // Requests to this computer must not go through a proxy.
    Handle session{WinHttpOpen(L"Summarizer/0", local ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    if (session.h == nullptr) {
        response.error = last_error("WinHttpOpen");
        return response;
    }
    const int ms = request.timeout_seconds * 1000;
    WinHttpSetTimeouts(session.h, ms, ms, ms, ms);

    Handle connection{WinHttpConnect(session.h, host, parts.nPort, 0)};
    if (connection.h == nullptr) {
        response.error = last_error("WinHttpConnect");
        return response;
    }
    Handle req{WinHttpOpenRequest(connection.h, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)};
    if (req.h == nullptr) {
        response.error = last_error("WinHttpOpenRequest");
        return response;
    }

    std::wstring headers;
    for (const auto& [name, value] : request.headers) {
        headers += widen(name) + L": " + widen(value) + L"\r\n";
    }
    if (!WinHttpSendRequest(req.h, headers.c_str(), static_cast<DWORD>(-1L),
                            const_cast<char*>(request.body.data()), static_cast<DWORD>(request.body.size()),
                            static_cast<DWORD>(request.body.size()), 0) ||
        !WinHttpReceiveResponse(req.h, nullptr)) {
        const DWORD code = GetLastError();
        if (code == ERROR_WINHTTP_TIMEOUT) {
            response.error = "the request timed out";
        } else if (code == ERROR_WINHTTP_NAME_NOT_RESOLVED) {
            response.error = "no internet connection (name not resolved)";
        } else if (code == ERROR_WINHTTP_CANNOT_CONNECT) {
            response.error = local ? "the service on this computer is not running"
                                   : "no internet connection (cannot connect)";
        } else {
            response.error = "request failed (Windows error " + std::to_string(code) + ")";
        }
        return response;
    }

    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    response.status = static_cast<int>(status);

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(req.h, &available)) {
            response.error = last_error("reading the response");
            break;
        }
        if (available == 0) {
            break;
        }
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req.h, chunk.data(), available, &read)) {
            response.error = last_error("reading the response");
            break;
        }
        response.body.append(chunk.data(), read);
    }
    return response;
}

#else

// Elsewhere only plain HTTP to this computer is implemented (local model servers, tests).
HttpResponse http_post(const HttpRequest& request) {
    HttpResponse response;
    const std::string prefix = "http://127.0.0.1:";
    if (request.url.rfind(prefix, 0) != 0) {
        response.error = "HTTPS requests are only implemented on Windows so far";
        return response;
    }
    const std::size_t slash = request.url.find('/', prefix.size());
    const int port = std::atoi(request.url.substr(prefix.size(), slash - prefix.size()).c_str());
    const std::string path = slash == std::string::npos ? "/" : request.url.substr(slash);

    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        response.error = "socket failed";
        return response;
    }
    timeval tv{request.timeout_seconds, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        response.error = "the service on this computer is not running";
        return response;
    }
    std::string head = "POST " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nContent-Length: " +
                       std::to_string(request.body.size()) + "\r\n";
    for (const auto& [name, value] : request.headers) {
        head += name + ": " + value + "\r\n";
    }
    const std::string message = head + "\r\n" + request.body;
    for (std::size_t sent = 0; sent < message.size();) {
        const ssize_t n = send(fd, message.data() + sent, message.size() - sent, 0);
        if (n <= 0) {
            close(fd);
            response.error = "send failed";
            return response;
        }
        sent += static_cast<std::size_t>(n);
    }
    std::string raw;
    char buffer[65536];
    for (ssize_t n; (n = recv(fd, buffer, sizeof(buffer), 0)) > 0;) {
        raw.append(buffer, static_cast<std::size_t>(n));
    }
    close(fd);
    const std::size_t space = raw.find(' ');
    const std::size_t body_at = raw.find("\r\n\r\n");
    if (space == std::string::npos || body_at == std::string::npos) {
        response.error = "invalid HTTP response";
        return response;
    }
    response.status = std::atoi(raw.c_str() + space + 1);
    response.body = raw.substr(body_at + 4);  // the test servers answer without chunked encoding
    return response;
}

#endif

}  // namespace smz
