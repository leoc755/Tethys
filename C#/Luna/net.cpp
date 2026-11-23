/* Luna network core. Windows: WinHTTP (TLS 1.2/1.3 via the OS, system cert store).
 * Other platforms get a stub so the parser/CSS code can be built and tested anywhere. */
#include "luna_internal.hpp"
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1   0x00000080
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 0x00000200
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif
#ifndef SECURITY_FLAG_IGNORE_UNKNOWN_CA
#define SECURITY_FLAG_IGNORE_UNKNOWN_CA        0x00000100
#endif
#ifndef SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE
#define SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE  0x00000200
#endif
#ifndef SECURITY_FLAG_IGNORE_CERT_CN_INVALID
#define SECURITY_FLAG_IGNORE_CERT_CN_INVALID   0x00001000
#endif
#ifndef SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
#define SECURITY_FLAG_IGNORE_CERT_DATE_INVALID 0x00002000
#endif

namespace luna {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const wchar_t* w, size_t len) {
    if (len == 0) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, (int)len, NULL, 0, NULL, NULL);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, (int)len, &s[0], n, NULL, NULL);
    return s;
}

struct Handle {
    HINTERNET h;
    Handle() : h(NULL) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
private:
    Handle(const Handle&);
    Handle& operator=(const Handle&);
};

bool is_security_error(DWORD e) {
    switch (e) {
        case ERROR_WINHTTP_SECURE_FAILURE:
        case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
        case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
        case ERROR_WINHTTP_SECURE_INVALID_CA:
        case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
        case ERROR_WINHTTP_SECURE_INVALID_CERT:
        case ERROR_WINHTTP_SECURE_CERT_REVOKED:
        case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE:
            return true;
        default:
            return false;
    }
}

std::string error_text(DWORD e) {
    char buf[512];
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             GetModuleHandleA("winhttp.dll"), e, 0, buf, sizeof(buf), NULL);
    if (n == 0) return "Network error (code " + std::to_string((unsigned long)e) + ")";
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' ' || buf[n - 1] == '.')) --n;
    return std::string(buf, n);
}

} // namespace

HttpResult http_get(const std::string& url_in, const std::string& user_agent,
                    bool ignore_security, int timeout_ms, size_t max_bytes) {
    HttpResult r;
    std::string url = url_in;
    size_t frag = url.find('#');
    if (frag != std::string::npos) url.erase(frag);

    std::wstring wurl = widen(url);
    wchar_t host[512]; wchar_t path[8192]; wchar_t extra[8192];
    URL_COMPONENTSW uc;
    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;      uc.dwHostNameLength = 512;
    uc.lpszUrlPath = path;       uc.dwUrlPathLength = 8192;
    uc.lpszExtraInfo = extra;    uc.dwExtraInfoLength = 8192;
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc) ||
        (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS)) {
        r.error = "Invalid URL";
        return r;
    }
    bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    std::wstring hostw(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring obj(uc.lpszUrlPath, uc.dwUrlPathLength);
    obj.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (obj.empty()) obj = L"/";

    Handle ses;
    ses.h = WinHttpOpen(widen(user_agent).c_str(), WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    if (!ses.h) { r.error = error_text(GetLastError()); return r; }
    WinHttpSetTimeouts(ses.h, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    if (secure) {
        DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        /* ignore_security also admits legacy TLS 1.0/1.1 servers (old keys) */
        if (ignore_security) protos |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1;
        if (!WinHttpSetOption(ses.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos))) {
            /* older Windows without TLS 1.3 */
            protos &= ~(DWORD)WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
            WinHttpSetOption(ses.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos));
        }
    }

    Handle con;
    con.h = WinHttpConnect(ses.h, hostw.c_str(), uc.nPort, 0);
    if (!con.h) { r.error = error_text(GetLastError()); return r; }

    Handle req;
    req.h = WinHttpOpenRequest(con.h, L"GET", obj.c_str(), NULL, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req.h) { r.error = error_text(GetLastError()); return r; }

    /* redirects are followed by the engine so the final URL is known */
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(req.h, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));

    if (secure && ignore_security) {
        DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
                      SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(req.h, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
    }

    if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req.h, NULL)) {
        DWORD e = GetLastError();
        if (secure && !ignore_security && is_security_error(e)) r.security_error = true;
        else r.error = error_text(e);
        return r;
    }

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    r.status = (int)status;

    wchar_t loc[4096]; DWORD lsz = sizeof(loc);
    if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                            loc, &lsz, WINHTTP_NO_HEADER_INDEX))
        r.location = trim(narrow(loc, lsz / sizeof(wchar_t)));

    wchar_t hv[1024]; DWORD hsz = sizeof(hv);
    if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
                            hv, &hsz, WINHTTP_NO_HEADER_INDEX))
        r.content_type = trim(narrow(hv, hsz / sizeof(wchar_t)));
    hsz = sizeof(hv);
    if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_DISPOSITION, WINHTTP_HEADER_NAME_BY_INDEX,
                            hv, &hsz, WINHTTP_NO_HEADER_INDEX))
        r.content_disposition = trim(narrow(hv, hsz / sizeof(wchar_t)));

    std::vector<char> buf;
    DWORD last_err = 0;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail)) { last_err = GetLastError(); break; }
        if (avail == 0) break;
        size_t old = buf.size();
        if (old >= max_bytes) { r.truncated = true; break; }
        if (old + avail > max_bytes) avail = (DWORD)(max_bytes - old);
        buf.resize(old + avail);
        DWORD got = 0;
        if (!WinHttpReadData(req.h, &buf[old], avail, &got)) { last_err = GetLastError(); buf.resize(old); break; }
        buf.resize(old + got);
        if (got == 0) break;
    }
    if (last_err != 0 && buf.empty()) { r.error = error_text(last_err); return r; }

    r.body.assign(buf.begin(), buf.end());
    r.ok = true;
    return r;
}

static std::string env_utf8(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, NULL, 0);
    if (n == 0) return std::string();
    std::wstring v((size_t)n, L'\0');
    DWORD got = GetEnvironmentVariableW(name, &v[0], n);
    return narrow(v.c_str(), got);
}

std::string default_settings_path() {
    std::string a = env_utf8(L"APPDATA");
    return a.empty() ? std::string() : a + "\\Tethys\\luna.ini";
}

std::string default_download_dir() {
    std::string u = env_utf8(L"USERPROFILE");
    return u.empty() ? std::string() : u + "\\Downloads";
}

char path_sep() { return '\\'; }

bool make_dirs(const std::string& dir) {
    std::wstring w = widen(dir);
    if (w.empty()) return false;
    for (size_t i = 1; i <= w.size(); ++i)
        if (i == w.size() || w[i] == L'\\' || w[i] == L'/') CreateDirectoryW(w.substr(0, i).c_str(), NULL);
    DWORD a = GetFileAttributesW(w.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool file_exists(const std::string& path) {
    return GetFileAttributesW(widen(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool write_file(const std::string& path, const std::string& data) {
    FILE* f = _wfopen(widen(path).c_str(), L"wb");
    if (!f) return false;
    bool ok = data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size();
    return (fclose(f) == 0) && ok;
}

bool read_file(const std::string& path, std::string& out) {
    FILE* f = _wfopen(widen(path).c_str(), L"rb");
    if (!f) return false;
    char buf[4096]; size_t got;
    out.clear();
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, got);
    fclose(f);
    return true;
}

std::string os_info() {
    typedef struct { ULONG size, major, minor, build, platform; WCHAR csd[128]; } OsVer;
    typedef LONG (WINAPI *RtlGetVersionFn)(OsVer*);
    char out[128];
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    void* fp = nt ? (void*)GetProcAddress(nt, "RtlGetVersion") : NULL;
    if (!fp) return "Windows NT 10.0 [UNK]; Win64; x64";
    RtlGetVersionFn fn = (RtlGetVersionFn)fp;
    OsVer v; ZeroMemory(&v, sizeof(v)); v.size = sizeof(v);
    if (fn(&v) != 0) return "Windows NT 10.0 [UNK]; Win64; x64";
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    bool is64 = si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
                si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64;
    wsprintfA(out, "Windows NT %lu.%lu [%lu]; %s", v.major, v.minor, v.build, is64 ? "Win64; x64" : "Win32; x86");
    return out;
}

} // namespace luna

#else  /* !_WIN32 : stub */

#include <sys/stat.h>
#include <sys/types.h>

namespace luna {

HttpResult http_get(const std::string&, const std::string&, bool, int, size_t) {
    HttpResult r;
    r.error = "Networking is only implemented on Windows (WinHTTP)";
    return r;
}

std::string os_info() { return "Windows NT 10.0 [UNK]; Win64; x64"; }

std::string default_settings_path() { return std::string(); }

std::string default_download_dir() {
    const char* h = getenv("HOME");
    return h ? std::string(h) + "/Downloads" : std::string();
}

char path_sep() { return '/'; }

bool make_dirs(const std::string& dir) {
    if (dir.empty()) return false;
    for (size_t i = 1; i <= dir.size(); ++i)
        if (i == dir.size() || dir[i] == '/') mkdir(dir.substr(0, i).c_str(), 0755);
    struct stat st;
    return stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool write_file(const std::string& path, const std::string& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size();
    return (fclose(f) == 0) && ok;
}

bool read_file(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096]; size_t got;
    out.clear();
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, got);
    fclose(f);
    return true;
}

} // namespace luna

#endif

namespace luna {

std::string path_join(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\') return dir + name;
    return dir + path_sep() + name;
}

} // namespace luna
