#include "net/http.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <winhttp.h>
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
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        response.error = "invalid https URL";
        return response;
    }
    const std::wstring path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
                              std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    Handle session{WinHttpOpen(L"Summarizer/0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
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
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
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
        response.error = code == ERROR_WINHTTP_TIMEOUT             ? "the request timed out"
                         : code == ERROR_WINHTTP_NAME_NOT_RESOLVED ? "no internet connection (name not resolved)"
                         : code == ERROR_WINHTTP_CANNOT_CONNECT    ? "no internet connection (cannot connect)"
                                                                   : "request failed (Windows error " +
                                                                         std::to_string(code) + ")";
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

HttpResponse http_post(const HttpRequest&) {
    HttpResponse response;
    response.error = "HTTPS requests are only implemented on Windows so far";
    return response;
}

#endif

}  // namespace smz
