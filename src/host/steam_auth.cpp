#include "steam_auth.h"
#include "debug_console.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#else
#include <arpa/inet.h>
#ifdef APP_HAVE_CURL
#include <curl/curl.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cwchar>
typedef int SOCKET;
typedef long LONG;
typedef struct sockaddr sockaddr;
typedef struct sockaddr_in sockaddr_in;
typedef struct timeval timeval;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
typedef pthread_mutex_t CRITICAL_SECTION;
#define InitializeCriticalSection(m) pthread_mutex_init((m), NULL)
#define DeleteCriticalSection pthread_mutex_destroy
#define EnterCriticalSection pthread_mutex_lock
#define LeaveCriticalSection pthread_mutex_unlock
#define InterlockedCompareExchange(p, a, b) __sync_val_compare_and_swap((p), (b), (a))
#define InterlockedExchange(p, v) __sync_lock_test_and_set((p), (v))
#define closesocket close
#define GetLastError() ((unsigned long)errno)
#define SecureZeroMemory(p, n) memset((p), 0, (n))
#define DeleteFileA(p) ((void)unlink(p))
#define GetFileAttributesA(p) (access((p), F_OK) == 0 ? 0u : 0xFFFFFFFFu)
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#ifndef DWORD
typedef unsigned int DWORD;
#endif
typedef pthread_t HANDLE;
#define WINAPI
typedef void *LPVOID;
#ifndef INTERNET_DEFAULT_HTTPS_PORT
#define INTERNET_DEFAULT_HTTPS_PORT 443
#endif
typedef unsigned short INTERNET_PORT;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
#endif

#include "shared/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#define MultiByteToWideChar(cp, flags, src, slen, dst, dmax) os_utf8_to_wide((src), (dst), (dmax))

#ifdef APP_HAVE_CURL
typedef struct SteamCurlBuf {
    char *out;
    int max;
    int used;
    FILE *file;
} SteamCurlBuf;

static size_t
steam_curl_write(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    SteamCurlBuf *b = (SteamCurlBuf *)userdata;
    size_t n = size * nmemb;
    if (b->file) {
        return fwrite(ptr, 1, n, b->file);
    }
    if (!b->out || b->used + (int)n >= b->max) {
        return 0;
    }
    memcpy(b->out + b->used, ptr, n);
    b->used += (int)n;
    b->out[b->used] = '\0';
    return n;
}
#else
static int
http_slurp_file(const char *path, char *out, int out_max)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    size_t n = fread(out, 1, (size_t)(out_max > 1 ? out_max - 1 : 0), file);
    fclose(file);
    if (out && out_max > 0) {
        out[n] = '\0';
    }
    return (int)n;
}

static int
http_run_curl(char *const argv[], const char *body_path, long *status_out)
{
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return 0;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return 0;
    }
    if (pid == 0) {
        int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) {
            dup2(nullfd, STDERR_FILENO);
            close(nullfd);
        }
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execvp("curl", argv);
        _exit(127);
    }
    close(pipefd[1]);
    char code[16];
    memset(code, 0, sizeof(code));
    ssize_t got = read(pipefd[0], code, sizeof(code) - 1);
    close(pipefd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    if (body_path) {
        unlink(body_path);
    }
    if (got <= 0 || !WIFEXITED(st) || WEXITSTATUS(st) == 127) {
        return 0;
    }
    if (status_out) {
        *status_out = strtol(code, NULL, 10);
    }
    return 1;
}
#endif
#endif

#define STEAM_ID_MAX 32
#define STEAM_NAME_MAX 64
#define STEAM_STATUS_MAX 160
#define STEAM_URL_MAX 2048
#define STEAM_KEY_MAX 80
#define BUNGIE_KEY_MAX 128
#define STEAM_DD_USER_MAX 64
#define D2_APP 1085660u
#define D2_FORSAKEN 1090150u
#define D2_FORSAKEN_PASS 1090200u
#define D2_VERSION_FORSAKEN 8
#define D2_VERSION_Y2_PASS 16

typedef enum SteamPhase {
    STEAM_IDLE = 0,
    STEAM_WAITING,
    STEAM_WORKING,
    STEAM_READY,
    STEAM_FAILED
} SteamPhase;

static CRITICAL_SECTION g_lock;
static int g_ready;
static int g_wsa;
static char g_root[MAX_PATH];
static char g_key[STEAM_KEY_MAX];
static char g_bungie_key[BUNGIE_KEY_MAX];
static char g_id[STEAM_ID_MAX];
static char g_name[STEAM_NAME_MAX];
static char g_avatar[MAX_PATH];
static char g_status[STEAM_STATUS_MAX];
static char g_dd_user[STEAM_DD_USER_MAX];
static int g_owns_known;
static int g_owns_d2;
static int g_owns_forsaken;
static SteamPhase g_phase;
static SOCKET g_listen = INVALID_SOCKET;
static HANDLE g_thread;
static volatile LONG g_cancel;

static void fetch_owned(const char *steamid);

static void
set_status(const char *text)
{
    EnterCriticalSection(&g_lock);
    snprintf(g_status, sizeof(g_status), "%s", text ? text : "");
    LeaveCriticalSection(&g_lock);
    if (text && text[0]) {
        debug_log("steam: %s", text);
    }
}

static void
copy_locked(char *dst, int max, const char *src)
{
    EnterCriticalSection(&g_lock);
    snprintf(dst, max, "%s", src ? src : "");
    LeaveCriticalSection(&g_lock);
}

static int
hex_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int
url_encode(const char *in, char *out, int max)
{
    static const char *hex = "0123456789ABCDEF";
    int n = 0;
    if (!in || !out || max <= 1) {
        return 0;
    }
    while (*in && n + 1 < max) {
        unsigned char c = (unsigned char)*in++;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[n++] = (char)c;
        } else if (n + 3 < max) {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        } else {
            break;
        }
    }
    out[n] = '\0';
    return n;
}

static int
url_decode(const char *in, char *out, int max)
{
    int n = 0;
    if (!in || !out || max <= 1) {
        return 0;
    }
    while (*in && n + 1 < max) {
        if (*in == '%' && hex_val(in[1]) >= 0 && hex_val(in[2]) >= 0) {
            out[n++] = (char)((hex_val(in[1]) << 4) | hex_val(in[2]));
            in += 3;
        } else {
            out[n++] = *in++;
        }
    }
    out[n] = '\0';
    return n;
}

static void
trim_line(char *text)
{
    int n = (int)strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r' || text[n - 1] == ' ' || text[n - 1] == '\t')) {
        text[--n] = '\0';
    }
}

static void
copy_env_value(char *out, int max, const char *text)
{
    if (!out || max < 2) {
        return;
    }
    snprintf(out, (size_t)max, "%s", text ? text : "");
    trim_line(out);
    int n = (int)strlen(out);
    if (n >= 2 && (out[0] == '"' || out[0] == '\'') && out[n - 1] == out[0]) {
        memmove(out, out + 1, (size_t)(n - 2));
        out[n - 2] = '\0';
    }
}

static void
load_key(const char *root)
{
    const char *steam = getenv("STEAM_API_KEY");
    const char *bungie = getenv("BUNGIE_API_KEY");

    g_key[0] = '\0';
    g_bungie_key[0] = '\0';
    if (steam && steam[0]) {
        copy_env_value(g_key, (int)sizeof(g_key), steam);
    }
    if (bungie && bungie[0]) {
        copy_env_value(g_bungie_key, (int)sizeof(g_bungie_key), bungie);
    }
    if (g_key[0] && g_bungie_key[0]) {
        return;
    }

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/.env", root ? root : ".");
    FILE *file = fopen(path, "rb");
    if (!file) {
        snprintf(path, sizeof(path), "%s\\.env", root ? root : ".");
        file = fopen(path, "rb");
    }
    if (!file) {
        return;
    }
    char line[384];
    while (fgets(line, sizeof(line), file)) {
        trim_line(line);
        if (!g_key[0] && strncmp(line, "STEAM_API_KEY=", 14) == 0) {
            copy_env_value(g_key, (int)sizeof(g_key), line + 14);
        } else if (!g_bungie_key[0] && strncmp(line, "BUNGIE_API_KEY=", 15) == 0) {
            copy_env_value(g_bungie_key, (int)sizeof(g_bungie_key), line + 15);
        }
    }
    fclose(file);
}

static void
session_paths(char *session, char *avatar, int max)
{
    char dawn[MAX_PATH];
    os_data_dir(dawn, sizeof(dawn));
    char cache[MAX_PATH];
    os_join(cache, sizeof(cache), dawn, "cache");
    os_mkdirs(cache);
    os_join(cache, sizeof(cache), cache, "steam");
    os_mkdirs(cache);
    if (session) {
        os_join(session, max, dawn, "steam_session.txt");
    }
    if (avatar) {
        os_join(avatar, max, cache, "avatar.jpg");
    }
}

static void
save_session(void)
{
    char path[MAX_PATH];
    session_paths(path, NULL, MAX_PATH);
    FILE *file = fopen(path, "wb");
    if (!file) {
        return;
    }
    fprintf(
        file,
        "id=%s\nname=%s\navatar=%s\nowns_d2=%d\nowns_forsaken=%d\nowns_known=%d\ndd_user=%s\n",
        g_id,
        g_name,
        g_avatar,
        g_owns_d2,
        g_owns_forsaken,
        g_owns_known,
        g_dd_user
    );
    fclose(file);
}

static void
dd_user_path(char *path, int max)
{
    char dawn[MAX_PATH];
    os_data_dir(dawn, sizeof(dawn));
    os_join(path, max, dawn, "depot_user.txt");
}

static void
save_dd_user_file(void)
{
    char path[MAX_PATH];
    dd_user_path(path, MAX_PATH);
    if (!g_dd_user[0]) {
        DeleteFileA(path);
        return;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        return;
    }
    fprintf(file, "%s\n", g_dd_user);
    fclose(file);
}

static void
load_dd_user_file(void)
{
    char path[MAX_PATH];
    dd_user_path(path, MAX_PATH);
    FILE *file = fopen(path, "rb");
    if (!file) {
        return;
    }
    char line[STEAM_DD_USER_MAX];
    if (fgets(line, sizeof(line), file)) {
        trim_line(line);
        snprintf(g_dd_user, sizeof(g_dd_user), "%s", line);
    }
    fclose(file);
}

static int
vdf_account_for_id(const char *text, const char *steamid, char *out, int max)
{
    const char *id;
    const char *key;
    const char *q;
    const char *end;
    size_t n;

    if (!text || !steamid || !steamid[0] || !out || max < 2) {
        return 0;
    }
    id = strstr(text, steamid);
    if (!id) {
        return 0;
    }
    key = strstr(id, "\"AccountName\"");
    if (!key || key > id + 900) {
        return 0;
    }
    q = strchr(key + 13, '"');
    if (!q) {
        return 0;
    }
    end = strchr(q + 1, '"');
    if (!end || end <= q + 1) {
        return 0;
    }
    n = (size_t)(end - (q + 1));
    if (n == 0 || n >= (size_t)max) {
        return 0;
    }
    memcpy(out, q + 1, n);
    out[n] = '\0';
    return out[0] != '\0';
}

static int
read_loginusers(const char *path, const char *steamid, char *out, int max)
{
    FILE *file;
    long size;
    char *buf;
    int ok;

    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0 || size > 512000) {
        fclose(file);
        return 0;
    }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(file);
        return 0;
    }
    if (fread(buf, 1, (size_t)size, file) != (size_t)size) {
        free(buf);
        fclose(file);
        return 0;
    }
    fclose(file);
    buf[size] = '\0';
    ok = vdf_account_for_id(buf, steamid, out, max);
    free(buf);
    return ok;
}

static int
steam_client_root(char *out, int max)
{
#ifdef _WIN32
    HKEY key = NULL;
    DWORD type = 0;
    DWORD size = (DWORD)max;

    if (max < 8) {
        return 0;
    }
    out[0] = '\0';
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExA(key, "SteamPath", NULL, &type, (LPBYTE)out, &size) != ERROR_SUCCESS) {
            out[0] = '\0';
        }
        RegCloseKey(key);
    }
    if (out[0]) {
        return 1;
    }
    snprintf(out, (size_t)max, "C:\\Program Files (x86)\\Steam");
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
#else
    const char *home = getenv("HOME");
    if (!home || !home[0] || max < 8) {
        return 0;
    }
    snprintf(out, (size_t)max, "%s/.local/share/Steam", home);
    if (access(out, F_OK) == 0) {
        return 1;
    }
    snprintf(out, (size_t)max, "%s/.steam/steam", home);
    if (access(out, F_OK) == 0) {
        return 1;
    }
    snprintf(out, (size_t)max, "%s/.steam/root", home);
    return access(out, F_OK) == 0;
#endif
}

static void
resolve_steam_account_name(void)
{
    char steamid[STEAM_ID_MAX];
    char root[MAX_PATH];
    char path[MAX_PATH];
    char name[STEAM_DD_USER_MAX];

    EnterCriticalSection(&g_lock);
    snprintf(steamid, sizeof(steamid), "%s", g_id);
    LeaveCriticalSection(&g_lock);
    if (!steamid[0] || !steam_client_root(root, (int)sizeof(root))) {
        return;
    }
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\config\\loginusers.vdf", root);
#else
    snprintf(path, sizeof(path), "%s/config/loginusers.vdf", root);
#endif
    name[0] = '\0';
    if (!read_loginusers(path, steamid, name, (int)sizeof(name))) {
        return;
    }
    steam_auth_set_dd_user(name);
}

static void
clear_session_file(void)
{
    char path[MAX_PATH];
    session_paths(path, NULL, MAX_PATH);
    DeleteFileA(path);
}

static void
load_session(void)
{
    char path[MAX_PATH];
    session_paths(path, NULL, MAX_PATH);
    FILE *file = fopen(path, "rb");
    if (!file) {
        return;
    }
    char line[MAX_PATH + 32];
    while (fgets(line, sizeof(line), file)) {
        trim_line(line);
        if (strncmp(line, "id=", 3) == 0) {
            snprintf(g_id, sizeof(g_id), "%s", line + 3);
        } else if (strncmp(line, "name=", 5) == 0) {
            snprintf(g_name, sizeof(g_name), "%s", line + 5);
        } else if (strncmp(line, "avatar=", 7) == 0) {
            snprintf(g_avatar, sizeof(g_avatar), "%s", line + 7);
        } else if (strncmp(line, "owns_d2=", 8) == 0) {
            g_owns_d2 = atoi(line + 8);
        } else if (strncmp(line, "owns_forsaken=", 14) == 0) {
            g_owns_forsaken = atoi(line + 14);
        } else if (strncmp(line, "owns_known=", 11) == 0) {
            g_owns_known = atoi(line + 11);
        } else if (strncmp(line, "dd_user=", 8) == 0) {
            snprintf(g_dd_user, sizeof(g_dd_user), "%s", line + 8);
        }
    }
    fclose(file);
    if (g_avatar[0] && GetFileAttributesA(g_avatar) == INVALID_FILE_ATTRIBUTES) {
        g_avatar[0] = '\0';
    }
    if (!g_dd_user[0]) {
        load_dd_user_file();
    }
    if (!g_dd_user[0] && g_id[0]) {
        resolve_steam_account_name();
    }
    if (g_id[0] && g_name[0]) {
        fetch_owned(g_id);
        g_phase = STEAM_READY;
        set_status("Signed in");
    }
}

static int
http_exchange_hdr(
    const wchar_t *host,
    INTERNET_PORT port,
    const wchar_t *method,
    const wchar_t *path,
    const char *body,
    DWORD body_len,
    const wchar_t *content_type,
    const char *extra_headers,
    char *out,
    int out_max,
    const char *file_path,
    DWORD *status_out
)
{
    if (status_out) {
        *status_out = 0;
    }
#ifdef _WIN32
    HINTERNET session = WinHttpOpen(
        L"Mozilla/5.0 Dawn",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!session) {
        debug_log("steam: winhttp open failed %lu", GetLastError());
        return 0;
    }
    DWORD proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    proto |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof(proto));

    HINTERNET connect = WinHttpConnect(session, host, port, 0);
    if (!connect) {
        debug_log("steam: winhttp connect failed %lu", GetLastError());
        WinHttpCloseHandle(session);
        return 0;
    }
    HINTERNET request = WinHttpOpenRequest(
        connect,
        method,
        path,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH
    );
    if (!request) {
        debug_log("steam: winhttp request failed %lu", GetLastError());
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return 0;
    }
    if (!body) {
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    }

    BOOL ok = FALSE;
    wchar_t headers[512];
    headers[0] = 0;
    if (body && body_len) {
        swprintf(
            headers,
            512,
            L"Content-Type: %s\r\n",
            content_type ? content_type : L"application/x-www-form-urlencoded"
        );
    }
    if (extra_headers && extra_headers[0]) {
        wchar_t extra_w[280];
        extra_w[0] = 0;
        MultiByteToWideChar(CP_UTF8, 0, extra_headers, -1, extra_w, 280);
        if (headers[0]) {
            wcsncat(headers, extra_w, 511 - wcslen(headers));
            wcsncat(headers, L"\r\n", 511 - wcslen(headers));
        } else {
            swprintf(headers, 512, L"%s\r\n", extra_w);
        }
    }
    ok = WinHttpSendRequest(
        request,
        headers[0] ? headers : WINHTTP_NO_ADDITIONAL_HEADERS,
        headers[0] ? (DWORD)-1 : 0,
        (body && body_len) ? (LPVOID)body : WINHTTP_NO_REQUEST_DATA,
        (body && body_len) ? body_len : 0,
        (body && body_len) ? body_len : 0,
        0
    );
    if (!ok || !WinHttpReceiveResponse(request, NULL)) {
        debug_log("steam: winhttp send failed %lu", GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return 0;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &status_size,
            WINHTTP_NO_HEADER_INDEX)) {
        if (status_out) {
            *status_out = status;
        }
    }

    FILE *file = NULL;
    if (file_path) {
        file = fopen(file_path, "wb");
        if (!file) {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return 0;
        }
    }
    int total = 0;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail)) {
            total = 0;
            break;
        }
        if (avail == 0) {
            break;
        }
        char chunk[4096];
        DWORD got = 0;
        DWORD take = avail > sizeof(chunk) ? (DWORD)sizeof(chunk) : avail;
        if (!WinHttpReadData(request, chunk, take, &got) || got == 0) {
            break;
        }
        if (file) {
            fwrite(chunk, 1, got, file);
            total += (int)got;
        } else if (out && total + (int)got < out_max) {
            memcpy(out + total, chunk, got);
            total += (int)got;
            out[total] = '\0';
        } else if (out) {
            break;
        }
    }
    if (file) {
        fclose(file);
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return total > 0 && (status == 0 || (status >= 200 && status < 300));
#else
    char host_utf8[256];
    char path_utf8[4096];
    char method_utf8[16];
    (void)content_type;
    wcstombs(host_utf8, host, sizeof(host_utf8));
    wcstombs(path_utf8, path, sizeof(path_utf8));
    wcstombs(method_utf8, method, sizeof(method_utf8));
    char url[4600];
    snprintf(url, sizeof(url), "https://%s:%u%s", host_utf8, (unsigned)port, path_utf8);

#ifdef APP_HAVE_CURL
    CURL *curl = curl_easy_init();
    if (!curl) {
        return 0;
    }
    SteamCurlBuf buf;
    buf.out = out;
    buf.max = out_max;
    buf.used = 0;
    buf.file = NULL;
    if (file_path) {
        buf.file = fopen(file_path, "wb");
        if (!buf.file) {
            curl_easy_cleanup(curl);
            return 0;
        }
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 Dawn");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, body ? 0L : 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, steam_curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    struct curl_slist *headers = NULL;
    if (body && body_len) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    }
    if (extra_headers && extra_headers[0]) {
        headers = curl_slist_append(headers, extra_headers);
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    if (strcmp(method_utf8, "GET") == 0) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }
    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (status_out) {
        *status_out = (DWORD)status;
    }
    if (headers) {
        curl_slist_free_all(headers);
    }
    if (buf.file) {
        fclose(buf.file);
    }
    curl_easy_cleanup(curl);
    return rc == CURLE_OK && buf.used + (buf.file ? 1 : 0) > 0 && (status == 0 || (status >= 200 && status < 300));
#else
    char out_file[MAX_PATH];
    char body_file[MAX_PATH];
    body_file[0] = '\0';
    if (file_path && file_path[0]) {
        snprintf(out_file, sizeof(out_file), "%s", file_path);
    } else {
        snprintf(out_file, sizeof(out_file), "/tmp/dawn-http-%d.out", (int)getpid());
    }
    if (body && body_len) {
        snprintf(body_file, sizeof(body_file), "/tmp/dawn-http-%d.in", (int)getpid());
        FILE *bf = fopen(body_file, "wb");
        if (!bf) {
            return 0;
        }
        fwrite(body, 1, body_len, bf);
        fclose(bf);
    }

    char *argv[24];
    int argc = 0;
    argv[argc++] = (char *)"curl";
    argv[argc++] = (char *)"-sS";
    argv[argc++] = (char *)"--max-time";
    argv[argc++] = (char *)"30";
    argv[argc++] = (char *)"-A";
    argv[argc++] = (char *)"Mozilla/5.0 Dawn";
    argv[argc++] = (char *)"-o";
    argv[argc++] = out_file;
    argv[argc++] = (char *)"-w";
    argv[argc++] = (char *)"%{http_code}";
    if (!body) {
        argv[argc++] = (char *)"-L";
    }
    if (body && body_len) {
        argv[argc++] = (char *)"-X";
        argv[argc++] = method_utf8;
        argv[argc++] = (char *)"-H";
        argv[argc++] = (char *)"Content-Type: application/x-www-form-urlencoded";
        argv[argc++] = (char *)"--data-binary";
        argv[argc++] = body_file;
    }
    if (extra_headers && extra_headers[0]) {
        argv[argc++] = (char *)"-H";
        argv[argc++] = (char *)extra_headers;
    }
    argv[argc++] = url;
    argv[argc] = NULL;
    (void)argc;

    long status = 0;
    int ran = http_run_curl(argv, body_file[0] ? body_file : NULL, &status);
    if (status_out) {
        *status_out = (DWORD)status;
    }
    int total = 0;
    if (ran) {
        if (out && out_max > 0) {
            total = http_slurp_file(out_file, out, out_max);
        } else if (file_path) {
            total = os_file_exists(file_path) ? 1 : 0;
        }
    }
    if (!file_path) {
        unlink(out_file);
    }
    return ran && total > 0 && (status == 0 || (status >= 200 && status < 300));
#endif
#endif
}

static int
http_exchange(
    const wchar_t *host,
    INTERNET_PORT port,
    const wchar_t *method,
    const wchar_t *path,
    const char *body,
    DWORD body_len,
    const wchar_t *content_type,
    char *out,
    int out_max,
    const char *file_path,
    DWORD *status_out
)
{
    return http_exchange_hdr(
        host,
        port,
        method,
        path,
        body,
        body_len,
        content_type,
        NULL,
        out,
        out_max,
        file_path,
        status_out
    );
}

static const char *
json_skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') {
        s++;
    }
    return s;
}

static int
json_string(const char *json, const char *key, char *out, int max)
{
    char needle[80];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *start = json;
    while ((start = strstr(start, needle)) != NULL) {
        start += strlen(needle);
        start = json_skip_ws(start);
        if (*start != ':') {
            continue;
        }
        start = json_skip_ws(start + 1);
        if (*start != '"') {
            continue;
        }
        start++;
        int n = 0;
        while (*start && *start != '"' && n + 1 < max) {
            if (*start == '\\' && start[1]) {
                start++;
                if (*start == 'n') {
                    out[n++] = '\n';
                } else if (*start == 't') {
                    out[n++] = '\t';
                } else {
                    out[n++] = *start;
                }
                start++;
                continue;
            }
            out[n++] = *start++;
        }
        out[n] = '\0';
        return n > 0;
    }
    out[0] = '\0';
    return 0;
}

static int
json_has_appid(const char *json, unsigned appid)
{
    char needle[48];
    snprintf(needle, sizeof(needle), "\"appid\":%u", appid);
    if (strstr(json, needle)) {
        return 1;
    }
    snprintf(needle, sizeof(needle), "\"appid\": %u", appid);
    if (strstr(json, needle)) {
        return 1;
    }
    snprintf(needle, sizeof(needle), "\"appid\":\"%u\"", appid);
    if (strstr(json, needle)) {
        return 1;
    }
    snprintf(needle, sizeof(needle), "\"appid\": \"%u\"", appid);
    return strstr(json, needle) != NULL;
}

static int
json_long(const char *json, const char *key, long *out)
{
    char needle[80];
    const char *start;

    if (!json || !key || !out) {
        return 0;
    }
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    start = json;
    while ((start = strstr(start, needle)) != NULL) {
        char *end = NULL;
        start += strlen(needle);
        start = json_skip_ws(start);
        if (*start != ':') {
            continue;
        }
        start = json_skip_ws(start + 1);
        if (*start != '-' && (*start < '0' || *start > '9')) {
            continue;
        }
        *out = strtol(start, &end, 10);
        return end != start;
    }
    return 0;
}

static int
extract_steamid(const char *claimed, char *out, int max)
{
    const char *id = strstr(claimed, "/openid/id/");
    if (!id) {
        return 0;
    }
    id += 11;
    int n = 0;
    while (id[n] >= '0' && id[n] <= '9') {
        n++;
    }
    if (n < 16 || n >= max) {
        return 0;
    }
    memcpy(out, id, (size_t)n);
    out[n] = '\0';
    return 1;
}

typedef struct QueryPair {
    char key[80];
    char value[2048];
} QueryPair;

static int
parse_query(const char *query, QueryPair *pairs, int max_pairs)
{
    int count = 0;
    const char *p = query;
    while (*p && count < max_pairs) {
        const char *amp = strchr(p, '&');
        char item[4096];
        int len = amp ? (int)(amp - p) : (int)strlen(p);
        if (len >= (int)sizeof(item)) {
            len = (int)sizeof(item) - 1;
        }
        memcpy(item, p, (size_t)len);
        item[len] = '\0';
        char *eq = strchr(item, '=');
        if (eq) {
            *eq = '\0';
            url_decode(item, pairs[count].key, (int)sizeof(pairs[count].key));
            url_decode(eq + 1, pairs[count].value, (int)sizeof(pairs[count].value));
            count += 1;
        }
        if (!amp) {
            break;
        }
        p = amp + 1;
    }
    return count;
}

static const char *
query_get(const QueryPair *pairs, int count, const char *key)
{
    for (int i = 0; i < count; ++i) {
        if (strcmp(pairs[i].key, key) == 0) {
            return pairs[i].value;
        }
    }
    return NULL;
}

static int
openid_is_valid(const char *response)
{
    if (!response) {
        return 0;
    }
    return strstr(response, "is_valid:true") != NULL || strstr(response, "is_valid: true") != NULL;
}

static int
openid_check_body(const char *query, char *out, int max)
{
    if (!query || !query[0] || !out || max < 32) {
        return 0;
    }
    const char *found = NULL;
    if (strncmp(query, "openid.mode=", 12) == 0) {
        found = query;
    } else {
        found = strstr(query, "&openid.mode=");
        if (found) {
            found += 1;
        }
    }
    if (!found) {
        return 0;
    }
    const char *val = found + 12;
    const char *rest = strchr(val, '&');
    int pre = (int)(found - query);
    if (pre >= max) {
        return 0;
    }
    memcpy(out, query, (size_t)pre);
    int n = snprintf(
        out + pre,
        max - pre,
        "openid.mode=check_authentication%s",
        rest ? rest : ""
    );
    if (n < 0 || pre + n >= max) {
        out[0] = '\0';
        return 0;
    }
    return 1;
}

static int
verify_openid(const char *query)
{
    char body[8192];
    if (!openid_check_body(query, body, (int)sizeof(body))) {
        set_status("Steam login payload missing");
        return 0;
    }

    char response[2048];
    response[0] = '\0';
    DWORD status = 0;
    int ok = http_exchange(
        L"steamcommunity.com",
        INTERNET_DEFAULT_HTTPS_PORT,
        L"POST",
        L"/openid/login",
        body,
        (DWORD)strlen(body),
        L"application/x-www-form-urlencoded",
        response,
        (int)sizeof(response),
        NULL,
        &status
    );
    if (ok && openid_is_valid(response)) {
        debug_log("steam: verify post http %lu valid=1", status);
        return 1;
    }

    char path_utf8[8500];
    snprintf(path_utf8, sizeof(path_utf8), "/openid/login?%s", body);
    wchar_t path[8500];
    if (MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, path, 8500) > 0) {
        response[0] = '\0';
        status = 0;
        ok = http_exchange(
            L"steamcommunity.com",
            INTERNET_DEFAULT_HTTPS_PORT,
            L"GET",
            path,
            NULL,
            0,
            NULL,
            response,
            (int)sizeof(response),
            NULL,
            &status
        );
        if (ok && openid_is_valid(response)) {
            debug_log("steam: verify get http %lu valid=1", status);
            return 1;
        }
    }

    debug_log("steam: verify failed http %lu body=%d", status, (int)strlen(response));
    if (status == 0) {
        set_status("Could not reach Steam to confirm login");
    }
    return 0;
}

static void
prefer_full_avatar(char *url, int max)
{
    if (!url || !url[0] || max < 16) {
        return;
    }
    char *medium = strstr(url, "_medium.");
    if (medium) {
        memmove(medium + 5, medium + 7, strlen(medium + 7) + 1);
        memcpy(medium, "_full", 5);
        return;
    }
    if (strstr(url, "_full.")) {
        return;
    }
    char *dot = strrchr(url, '.');
    if (!dot || dot == url) {
        return;
    }
    if ((int)strlen(url) + 5 >= max) {
        return;
    }
    memmove(dot + 5, dot, strlen(dot) + 1);
    memcpy(dot, "_full", 5);
}

static int
fetch_profile(const char *steamid, char *name, int name_max, char *avatar_url, int url_max)
{
    char key[STEAM_KEY_MAX];
    copy_locked(key, (int)sizeof(key), g_key);
    if (!key[0]) {
        return 0;
    }
    char path_utf8[400];
    snprintf(
        path_utf8,
        sizeof(path_utf8),
        "/ISteamUser/GetPlayerSummaries/v0002/?key=%s&steamids=%s",
        key,
        steamid
    );
    wchar_t path[400];
    MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, path, 400);
    char json[16384];
    json[0] = '\0';
    DWORD status = 0;
    int ok = http_exchange(
        L"api.steampowered.com",
        INTERNET_DEFAULT_HTTPS_PORT,
        L"GET",
        path,
        NULL,
        0,
        NULL,
        json,
        (int)sizeof(json),
        NULL,
        &status
    );
    SecureZeroMemory(key, sizeof(key));
    SecureZeroMemory(path_utf8, sizeof(path_utf8));
    SecureZeroMemory(path, sizeof(path));
    debug_log(
        "steam: profile http %lu ok=%d bytes=%d players=%d",
        status,
        ok,
        (int)strlen(json),
        strstr(json, "\"players\"") != NULL
    );
    if (!ok) {
        if (status == 403 || status == 401) {
            set_status("Steam API key was rejected");
        }
        return 0;
    }
    if (strstr(json, "\"players\":[]") || strstr(json, "\"players\": []")) {
        debug_log("steam: profile returned no players");
        return 0;
    }
    if (!json_string(json, "personaname", name, name_max)) {
        debug_log("steam: profile missing personaname");
        return 0;
    }
    if (!json_string(json, "avatarfull", avatar_url, url_max) &&
        !json_string(json, "avatarmedium", avatar_url, url_max)) {
        json_string(json, "avatar", avatar_url, url_max);
    }
    prefer_full_avatar(avatar_url, url_max);
    return name[0] != '\0';
}

static int
bungie_get(const char *path_utf8, char *json, int json_max, DWORD *status_out)
{
    char key[BUNGIE_KEY_MAX];
    char header[200];
    wchar_t path[512];

    copy_locked(key, (int)sizeof(key), g_bungie_key);
    if (!key[0] || !path_utf8 || !json || json_max < 8) {
        SecureZeroMemory(key, sizeof(key));
        return 0;
    }
    snprintf(header, sizeof(header), "X-API-Key: %s", key);
    if (MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, path, 512) <= 0) {
        SecureZeroMemory(key, sizeof(key));
        SecureZeroMemory(header, sizeof(header));
        return 0;
    }
    int ok = http_exchange_hdr(
        L"www.bungie.net",
        INTERNET_DEFAULT_HTTPS_PORT,
        L"GET",
        path,
        NULL,
        0,
        NULL,
        header,
        json,
        json_max,
        NULL,
        status_out
    );
    SecureZeroMemory(key, sizeof(key));
    SecureZeroMemory(header, sizeof(header));
    return ok;
}

static int
fetch_bungie_versions(const char *steamid, long *versions)
{
    char path[320];
    char json[32768];
    char membership_id[32];
    long err = 0;
    long membership_type = 0;
    DWORD status = 0;

    if (!versions || !steamid || !steamid[0]) {
        return 0;
    }
    *versions = 0;
    snprintf(
        path,
        sizeof(path),
        "/Platform/User/GetMembershipFromHardLinkedCredential/12/%s/",
        steamid
    );
    json[0] = '\0';
    if (!bungie_get(path, json, (int)sizeof(json), &status)) {
        debug_log("bungie: membership http %lu", status);
        return 0;
    }
    if (!json_long(json, "ErrorCode", &err) || err != 1) {
        debug_log("bungie: membership lookup failed");
        return 0;
    }
    if (!json_long(json, "membershipType", &membership_type) ||
        !json_string(json, "membershipId", membership_id, (int)sizeof(membership_id))) {
        debug_log("bungie: membership id missing");
        return 0;
    }
    snprintf(
        path,
        sizeof(path),
        "/Platform/Destiny2/%ld/Profile/%s/?components=100",
        membership_type,
        membership_id
    );
    json[0] = '\0';
    status = 0;
    if (!bungie_get(path, json, (int)sizeof(json), &status)) {
        debug_log("bungie: profile http %lu", status);
        return 0;
    }
    if (!json_long(json, "ErrorCode", &err) || err != 1) {
        debug_log("bungie: profile lookup failed");
        return 0;
    }
    if (!json_long(json, "versionsOwned", versions)) {
        debug_log("bungie: versionsOwned missing");
        return 0;
    }
    return 1;
}

static void
fetch_owned(const char *steamid)
{
    char key[STEAM_KEY_MAX];
    int steam_games = 0;
    int steam_d2 = 0;
    int steam_forsaken = 0;

    g_owns_known = 0;
    g_owns_d2 = -1;
    g_owns_forsaken = -1;

    copy_locked(key, (int)sizeof(key), g_key);
    if (key[0] && steamid && steamid[0]) {
        char json_in[320];
        snprintf(
            json_in,
            sizeof(json_in),
            "{\"steamid\":\"%s\",\"include_played_free_games\":true,\"include_free_sub\":true,"
            "\"appids_filter\":[%u,%u,%u]}",
            steamid,
            D2_APP,
            D2_FORSAKEN,
            D2_FORSAKEN_PASS
        );
        char enc[640];
        url_encode(json_in, enc, (int)sizeof(enc));
        char path_utf8[900];
        snprintf(
            path_utf8,
            sizeof(path_utf8),
            "/IPlayerService/GetOwnedGames/v0001/?key=%s&format=json&input_json=%s",
            key,
            enc
        );
        wchar_t path[900];
        MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, path, 900);
        char json[8192];
        json[0] = '\0';
        DWORD status = 0;
        int ok = http_exchange(
            L"api.steampowered.com",
            INTERNET_DEFAULT_HTTPS_PORT,
            L"GET",
            path,
            NULL,
            0,
            NULL,
            json,
            (int)sizeof(json),
            NULL,
            &status
        );
        SecureZeroMemory(path_utf8, sizeof(path_utf8));
        SecureZeroMemory(path, sizeof(path));
        debug_log("steam: owned http %lu ok=%d bytes=%d", status, ok, (int)strlen(json));
        if (ok) {
            steam_games = strstr(json, "\"games\"") != NULL || strstr(json, "game_count") != NULL;
            if (steam_games) {
                steam_d2 = json_has_appid(json, D2_APP);
                steam_forsaken = json_has_appid(json, D2_FORSAKEN) ||
                    json_has_appid(json, D2_FORSAKEN_PASS);
            } else {
                debug_log("steam: game details hidden or empty");
            }
        }
    }
    SecureZeroMemory(key, sizeof(key));

    if (steam_games) {
        g_owns_known = 1;
        g_owns_d2 = steam_d2 ? 1 : 0;
        if (steam_forsaken) {
            g_owns_forsaken = 1;
        }
    }

    if (g_bungie_key[0] && steamid && steamid[0]) {
        long versions = 0;
        if (fetch_bungie_versions(steamid, &versions)) {
            g_owns_known = 1;
            if ((versions & D2_VERSION_FORSAKEN) != 0 || (versions & D2_VERSION_Y2_PASS) != 0) {
                g_owns_forsaken = 1;
            } else if (g_owns_forsaken != 1) {
                g_owns_forsaken = 0;
            }
            if ((versions & 1) != 0) {
                g_owns_d2 = 1;
            }
            debug_log("bungie: versionsOwned=%ld forsaken=%d", versions, g_owns_forsaken);
        } else {
            debug_log("bungie: expansion check failed");
        }
    } else if (g_owns_forsaken < 0) {
        debug_log("bungie: add BUNGIE_API_KEY to .env to verify Forsaken");
    }
    debug_log("steam: d2=%d forsaken=%d (red war is base game)", g_owns_d2, g_owns_forsaken);
}

static int
download_avatar(const char *url, const char *file_path)
{
    char https[1024];
    if (!url || !url[0]) {
        return 0;
    }
    if (strncmp(url, "http://", 7) == 0) {
        snprintf(https, sizeof(https), "https://%s", url + 7);
        url = https;
    }
    if (strncmp(url, "https://", 8) != 0) {
        return 0;
    }
    const char *host_start = url + 8;
    const char *slash = strchr(host_start, '/');
    if (!slash) {
        return 0;
    }
    char host[160];
    int host_len = (int)(slash - host_start);
    if (host_len <= 0 || host_len >= (int)sizeof(host)) {
        return 0;
    }
    memcpy(host, host_start, (size_t)host_len);
    host[host_len] = '\0';
    wchar_t whost[160];
    wchar_t wpath[1024];
    MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, 160);
    MultiByteToWideChar(CP_UTF8, 0, slash, -1, wpath, 1024);
    DWORD status = 0;
    int ok = http_exchange(
        whost,
        INTERNET_DEFAULT_HTTPS_PORT,
        L"GET",
        wpath,
        NULL,
        0,
        NULL,
        NULL,
        0,
        file_path,
        &status
    );
    debug_log("steam: avatar http %lu ok=%d", status, ok);
    return ok;
}

static int
start_listen(unsigned *port_out)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return 0;
    }
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(sock, (sockaddr *)&addr, sizeof(addr)) != 0) {
        closesocket(sock);
        return 0;
    }
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    if (getsockname(sock, (sockaddr *)&addr, &len) != 0 || listen(sock, 1) != 0) {
        closesocket(sock);
        return 0;
    }
    g_listen = sock;
    *port_out = ntohs(addr.sin_port);
    return 1;
}

static void
close_listen(void)
{
    SOCKET sock = g_listen;
    g_listen = INVALID_SOCKET;
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
    }
}

static void
send_html(SOCKET client, const char *body)
{
    char page[1024];
    int n = snprintf(
        page,
        sizeof(page),
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: %u\r\n\r\n%s",
        (unsigned)strlen(body),
        body
    );
    if (n > 0) {
        send(client, page, n, 0);
    }
}

static int
http_content_length(const char *request)
{
    const char *p = strstr(request, "\r\nContent-Length:");
    if (!p) {
        p = strstr(request, "\r\ncontent-length:");
    }
    if (!p) {
        return 0;
    }
    p += 17;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return atoi(p);
}

static int
extract_callback_query(const char *request, int got, char *query_out, int query_max)
{
    if (!request || !query_out || query_max < 2) {
        return 0;
    }
    query_out[0] = '\0';
    const char *line_end = strstr(request, "\r\n");
    if (!line_end) {
        return 0;
    }
    char first[8192];
    int flen = (int)(line_end - request);
    if (flen >= (int)sizeof(first)) {
        flen = (int)sizeof(first) - 1;
    }
    memcpy(first, request, (size_t)flen);
    first[flen] = '\0';

    char *q = strchr(first, '?');
    if (q) {
        q++;
        char *http = strstr(q, " HTTP/");
        if (http) {
            *http = '\0';
        }
        snprintf(query_out, query_max, "%s", q);
        return query_out[0] != '\0';
    }

    const char *body = strstr(request, "\r\n\r\n");
    if (!body) {
        return 0;
    }
    body += 4;
    int have = got - (int)(body - request);
    if (have <= 0) {
        return 0;
    }
    int need = http_content_length(request);
    int take = have;
    if (need > 0 && need < take) {
        take = need;
    }
    if (take >= query_max) {
        take = query_max - 1;
    }
    memcpy(query_out, body, (size_t)take);
    query_out[take] = '\0';
    return query_out[0] != '\0';
}

#ifdef _WIN32
static DWORD WINAPI
auth_thread(LPVOID)
#else
static void *
auth_thread(void *)
#endif
{
    char request[16384];
    int got = 0;
    SOCKET client = INVALID_SOCKET;
    while (!InterlockedCompareExchange(&g_cancel, 0, 0)) {
        fd_set set;
        FD_ZERO(&set);
        if (g_listen == INVALID_SOCKET) {
            break;
        }
        FD_SET(g_listen, &set);
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 250000;
#ifdef _WIN32
        int ready = select(0, &set, NULL, NULL, &tv);
#else
        int ready = select((int)g_listen + 1, &set, NULL, NULL, &tv);
#endif
        if (ready <= 0) {
            continue;
        }
        client = accept(g_listen, NULL, NULL);
        if (client == INVALID_SOCKET) {
            continue;
        }
        got = 0;
        request[0] = '\0';
        int header_end = 0;
        while (got < (int)sizeof(request) - 1) {
            int n = recv(client, request + got, (int)sizeof(request) - 1 - got, 0);
            if (n <= 0) {
                break;
            }
            got += n;
            request[got] = '\0';
            const char *headers = strstr(request, "\r\n\r\n");
            if (!headers) {
                continue;
            }
            header_end = (int)(headers - request + 4);
            int need = http_content_length(request);
            if (need <= 0 || got - header_end >= need) {
                break;
            }
        }
        if (strncmp(request, "GET /favicon", 12) == 0) {
            send_html(client, "");
            closesocket(client);
            client = INVALID_SOCKET;
            continue;
        }
        break;
    }
    close_listen();
    if (client == INVALID_SOCKET) {
        if (g_phase == STEAM_WAITING) {
            g_phase = STEAM_FAILED;
            set_status("Sign in cancelled");
        }
        return 0;
    }

    send_html(
        client,
        "<!doctype html><html><body style='background:#1b2838;color:#c7d5e0;font-family:Segoe UI,sans-serif;padding:32px'>"
        "Returning to Dawn. You can close this tab.</body></html>"
    );
    closesocket(client);

    char query[8192];
    if (!extract_callback_query(request, got, query, (int)sizeof(query))) {
        g_phase = STEAM_FAILED;
        set_status("Steam did not return a login");
        return 0;
    }
    debug_log("steam: callback query %d bytes", (int)strlen(query));

    QueryPair pairs[64];
    int count = parse_query(query, pairs, 64);
    const char *mode = query_get(pairs, count, "openid.mode");
    if (mode && strcmp(mode, "cancel") == 0) {
        g_phase = STEAM_FAILED;
        set_status("Steam sign in cancelled");
        return 0;
    }
    const char *claimed = query_get(pairs, count, "openid.claimed_id");
    if (!claimed) {
        claimed = query_get(pairs, count, "openid.identity");
    }
    char steamid[STEAM_ID_MAX];
    steamid[0] = '\0';
    if (!claimed || !extract_steamid(claimed, steamid, (int)sizeof(steamid))) {
        g_phase = STEAM_FAILED;
        set_status("Steam account id missing");
        debug_log("steam: claimed_id missing pairs=%d", count);
        return 0;
    }
    g_phase = STEAM_WORKING;
    set_status("Confirming Steam login");
    if (!verify_openid(query)) {
        g_phase = STEAM_FAILED;
        set_status("Steam login was not valid");
        return 0;
    }

    char name[STEAM_NAME_MAX];
    char avatar_url[512];
    name[0] = '\0';
    avatar_url[0] = '\0';
    set_status("Loading Steam profile");
    if (!fetch_profile(steamid, name, (int)sizeof(name), avatar_url, (int)sizeof(avatar_url))) {
        g_phase = STEAM_FAILED;
        if (!g_status[0] || strcmp(g_status, "Loading Steam profile") == 0) {
            set_status("Steam profile request failed");
        }
        return 0;
    }

    char avatar_path[MAX_PATH];
    session_paths(NULL, avatar_path, MAX_PATH);
    DeleteFileA(avatar_path);
    if (avatar_url[0]) {
        set_status("Loading Steam avatar");
        download_avatar(avatar_url, avatar_path);
    }

    set_status("Checking Destiny 2 licenses");
    fetch_owned(steamid);

    EnterCriticalSection(&g_lock);
    snprintf(g_id, sizeof(g_id), "%s", steamid);
    snprintf(g_name, sizeof(g_name), "%s", name);
    if (GetFileAttributesA(avatar_path) != INVALID_FILE_ATTRIBUTES) {
        snprintf(g_avatar, sizeof(g_avatar), "%s", avatar_path);
    } else {
        g_avatar[0] = '\0';
    }
    g_phase = STEAM_READY;
    LeaveCriticalSection(&g_lock);
    save_session();
    resolve_steam_account_name();
    if (g_avatar[0]) {
        set_status("Signed in");
    } else {
        set_status("Signed in (avatar unavailable)");
    }
    debug_log(
        "steam: ready persona ok avatar=%d d2=%d forsaken=%d",
        g_avatar[0] != '\0',
        g_owns_d2,
        g_owns_forsaken
    );
    return 0;
}

static void
join_thread(void)
{
    if (g_thread) {
#ifdef _WIN32
        WaitForSingleObject(g_thread, 4000);
        CloseHandle(g_thread);
        g_thread = NULL;
#else
        pthread_join(g_thread, NULL);
        g_thread = 0;
#endif
    }
}

void
steam_auth_init(const char *project_root)
{
    InitializeCriticalSection(&g_lock);
    g_ready = 1;
    snprintf(g_root, sizeof(g_root), "%s", project_root ? project_root : ".");
    g_owns_d2 = -1;
    g_owns_forsaken = -1;
    g_owns_known = 0;
    load_dd_user_file();
    load_key(g_root);
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) == 0) {
        g_wsa = 1;
    }
#else
#ifdef APP_HAVE_CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
    g_wsa = 1;
#endif
    load_session();
    if (g_key[0]) {
        debug_log("steam: api key loaded");
    } else {
        debug_log("steam: STEAM_API_KEY missing from .env");
    }
    if (g_bungie_key[0]) {
        debug_log("bungie: api key loaded");
    } else {
        debug_log("bungie: BUNGIE_API_KEY missing from .env");
    }
}

void
steam_auth_shutdown(void)
{
    steam_auth_cancel();
    if (g_wsa) {
#ifdef _WIN32
        WSACleanup();
#else
#ifdef APP_HAVE_CURL
        curl_global_cleanup();
#endif
#endif
        g_wsa = 0;
    }
    if (g_ready) {
        DeleteCriticalSection(&g_lock);
        g_ready = 0;
    }
}

int
steam_auth_begin(void)
{
    if (!g_key[0]) {
        set_status("Add STEAM_API_KEY to .env");
        g_phase = STEAM_FAILED;
        return 0;
    }
    if (g_phase == STEAM_WAITING || g_phase == STEAM_WORKING) {
        return 1;
    }
    steam_auth_cancel();
    InterlockedExchange(&g_cancel, 0);
    unsigned port = 0;
    if (!g_wsa || !start_listen(&port)) {
        set_status("Could not start Steam login");
        g_phase = STEAM_FAILED;
        return 0;
    }

    char return_to[128];
    char realm[128];
    snprintf(return_to, sizeof(return_to), "http://127.0.0.1:%u/steam/return", port);
    snprintf(realm, sizeof(realm), "http://127.0.0.1:%u/", port);
    char enc_return[200];
    char enc_realm[200];
    url_encode(return_to, enc_return, (int)sizeof(enc_return));
    url_encode(realm, enc_realm, (int)sizeof(enc_realm));

    char url[STEAM_URL_MAX];
    snprintf(
        url,
        sizeof(url),
        "https://steamcommunity.com/openid/login"
        "?openid.ns=http%%3A%%2F%%2Fspecs.openid.net%%2Fauth%%2F2.0"
        "&openid.mode=checkid_setup"
        "&openid.return_to=%s"
        "&openid.realm=%s"
        "&openid.identity=http%%3A%%2F%%2Fspecs.openid.net%%2Fauth%%2F2.0%%2Fidentifier_select"
        "&openid.claimed_id=http%%3A%%2F%%2Fspecs.openid.net%%2Fauth%%2F2.0%%2Fidentifier_select",
        enc_return,
        enc_realm
    );

    g_phase = STEAM_WAITING;
    set_status("Finish signing in with Steam in your browser");
#ifdef _WIN32
    g_thread = CreateThread(NULL, 0, auth_thread, NULL, 0, NULL);
    if (!g_thread) {
#else
    if (pthread_create(&g_thread, NULL, auth_thread, NULL) != 0) {
        g_thread = 0;
#endif
        close_listen();
        g_phase = STEAM_FAILED;
        set_status("Could not start Steam login");
        return 0;
    }
    os_open_url(url);
    return 1;
}

void
steam_auth_cancel(void)
{
    InterlockedExchange(&g_cancel, 1);
    close_listen();
    join_thread();
    InterlockedExchange(&g_cancel, 0);
    if (g_phase == STEAM_WAITING || g_phase == STEAM_WORKING) {
        g_phase = STEAM_IDLE;
        set_status("");
    }
}

void
steam_auth_sign_out(void)
{
    steam_auth_cancel();
    EnterCriticalSection(&g_lock);
    g_id[0] = '\0';
    g_name[0] = '\0';
    g_avatar[0] = '\0';
    g_owns_known = 0;
    g_owns_d2 = -1;
    g_owns_forsaken = -1;
    g_phase = STEAM_IDLE;
    LeaveCriticalSection(&g_lock);
    clear_session_file();
    set_status("Signed out");
}

int
steam_auth_signed_in(void)
{
    return g_phase == STEAM_READY && g_id[0] != '\0' && g_name[0] != '\0';
}

int
steam_auth_busy(void)
{
    return g_phase == STEAM_WAITING || g_phase == STEAM_WORKING;
}

const char *
steam_auth_persona(void)
{
    return g_name;
}

const char *
steam_auth_id(void)
{
    return g_id;
}

const char *
steam_auth_avatar_path(void)
{
    return g_avatar;
}

const char *
steam_auth_status(void)
{
    return g_status;
}

const char *
steam_auth_dd_user(void)
{
    if (!g_dd_user[0] && g_id[0]) {
        resolve_steam_account_name();
    }
    return g_dd_user;
}

void
steam_auth_set_dd_user(const char *username)
{
    EnterCriticalSection(&g_lock);
    snprintf(g_dd_user, sizeof(g_dd_user), "%s", username ? username : "");
    LeaveCriticalSection(&g_lock);
    save_dd_user_file();
    if (g_id[0]) {
        save_session();
    }
}

int
steam_auth_owns_d2(void)
{
    if (!g_owns_known) {
        return -1;
    }
    return g_owns_d2 > 0 ? 1 : 0;
}

int
steam_auth_owns_forsaken(void)
{
    if (!g_owns_known || g_owns_forsaken < 0) {
        return -1;
    }
    return g_owns_forsaken > 0 ? 1 : 0;
}

int
steam_auth_owns_red_war(void)
{
    return steam_auth_owns_d2();
}
