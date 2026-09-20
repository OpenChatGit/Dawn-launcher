#include "self_update.h"
#include "config.h"
#include "shared/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define RELEASES_API \
    "https://api.github.com/repos/OpenChatGit/Dawn-launcher/releases?per_page=30"
#ifdef _WIN32
#define ASSET_NAME "Dawn-windows-x64.zip"
#define LAUNCHER_NAME "Dawn.exe"
#else
#define ASSET_NAME "Dawn-linux-x64.tar.gz"
#define LAUNCHER_NAME "Dawn"
#endif

#define CHECK_MS 1800000u
#define FIRST_CHECK_MS 1500u

typedef enum UpdatePhase {
    UPD_IDLE = 0,
    UPD_CHECKING,
    UPD_READY,
    UPD_DOWNLOADING,
    UPD_APPLY,
    UPD_FAILED
} UpdatePhase;

static UpdatePhase g_phase;
static char g_version[32];
static char g_url[1024];
static char g_status[160];
static char g_work[MAX_PATH];
static char g_json[MAX_PATH];
static char g_pkg[MAX_PATH];
static char g_unpack[MAX_PATH];
static uint32_t g_next_check;
static int g_quit;
#ifdef _WIN32
static HANDLE g_process;
#else
static pid_t g_process;
#endif

static void
set_status(const char *text)
{
    snprintf(g_status, sizeof(g_status), "%s", text ? text : "");
}

static void
close_child(void)
{
#ifdef _WIN32
    if (g_process) {
        CloseHandle(g_process);
        g_process = NULL;
    }
#else
    g_process = 0;
#endif
}

static int
child_running(void)
{
#ifdef _WIN32
    DWORD code;
    if (!g_process) {
        return 0;
    }
    if (WaitForSingleObject(g_process, 0) == WAIT_TIMEOUT) {
        return 1;
    }
    GetExitCodeProcess(g_process, &code);
    close_child();
    return code == 0 ? -1 : -2;
#else
    int status = 0;
    pid_t done;
    if (g_process <= 0) {
        return 0;
    }
    done = waitpid(g_process, &status, WNOHANG);
    if (done == 0) {
        return 1;
    }
    g_process = 0;
    if (done > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return -1;
    }
    return -2;
#endif
}

static void
find_curl(char *out, size_t max)
{
#ifdef _WIN32
    char sys[MAX_PATH];
    UINT n = GetSystemDirectoryA(sys, (UINT)sizeof(sys));
    if (n > 0 && n < sizeof(sys) && os_join(out, max, sys, "curl.exe") && os_file_exists(out)) {
        return;
    }
#endif
    snprintf(out, max, "curl");
}

static int
start_cmd(const char *command)
{
    close_child();
#ifdef _WIN32
    {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        char runnable[4096];

        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        snprintf(runnable, sizeof(runnable), "%s", command);
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        if (!CreateProcessA(NULL, runnable, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            return 0;
        }
        CloseHandle(pi.hThread);
        g_process = pi.hProcess;
        return 1;
    }
#else
    {
        pid_t pid = fork();
        if (pid < 0) {
            return 0;
        }
        if (pid == 0) {
            execl("/bin/sh", "sh", "-c", command, (char *)NULL);
            _exit(127);
        }
        g_process = pid;
        return 1;
    }
#endif
}

static int
run_cmd(const char *command)
{
#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char runnable[4096];
    DWORD code = 1;

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    snprintf(runnable, sizeof(runnable), "%s", command);
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessA(NULL, runnable, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return 0;
    }
    WaitForSingleObject(pi.hProcess, 180000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
#else
    return system(command) == 0;
#endif
}

static int
parse_version(const char *text, int *maj, int *min, int *pat)
{
    const char *p = text;
    char extra[32];
    int n = 0;

    if (!text || !maj || !min || !pat) {
        return 0;
    }
    if (*p == 'v' || *p == 'V') {
        p += 1;
    }
    if (sscanf(p, "%d.%d.%d%n", maj, min, pat, &n) != 3 || n <= 0) {
        return 0;
    }
    snprintf(extra, sizeof(extra), "%s", p + n);
    if (extra[0] == '\0') {
        return 1;
    }
    return 0;
}

static int
is_dev_build(void)
{
    char path[MAX_PATH];
    const char *base;

    if (APP_VERSION && strstr(APP_VERSION, "dev")) {
        return 1;
    }
#ifdef _WIN32
    if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
        int i;
        base = path;
        for (i = 0; path[i]; i++) {
            if (path[i] == '\\' || path[i] == '/') {
                base = path + i + 1;
            }
        }
        if (os_stricmp(base, "host.exe") == 0) {
            return 1;
        }
    }
#else
    {
        ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (n > 0) {
            path[n] = '\0';
            base = strrchr(path, '/');
            base = base ? base + 1 : path;
            if (os_stricmp(base, "host") == 0) {
                return 1;
            }
        }
    }
#endif
    return 0;
}

static int
version_newer(const char *have, const char *want)
{
    int a0 = 0;
    int a1 = 0;
    int a2 = 0;
    int b0 = 0;
    int b1 = 0;
    int b2 = 0;
    char clean[32];
    const char *p = have;
    size_t n = 0;

    if (!want || !parse_version(want, &b0, &b1, &b2)) {
        return 0;
    }
    if (!p) {
        p = "0.0.0";
    }
    while (p[n] && p[n] != '-' && n + 1 < sizeof(clean)) {
        clean[n] = p[n];
        n += 1;
    }
    clean[n] = '\0';
    if (!parse_version(clean, &a0, &a1, &a2)) {
        a0 = a1 = a2 = 0;
    }
    if (b0 != a0) {
        return b0 > a0;
    }
    if (b1 != a1) {
        return b1 > a1;
    }
    return b2 > a2;
}

static int
official_tag(const char *tag)
{
    int a;
    int b;
    int c;
    return parse_version(tag, &a, &b, &c);
}

static const char *
find_key(const char *json, const char *key)
{
    char needle[80];
    const char *p;

    if (!json || !key) {
        return NULL;
    }
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (!p) {
        return NULL;
    }
    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':') {
        p += 1;
    }
    return p;
}

static int
read_string(const char *p, char *out, size_t max)
{
    size_t n = 0;

    if (!p || *p != '"' || !out || max < 2) {
        return 0;
    }
    p += 1;
    while (*p && *p != '"' && n + 1 < max) {
        if (*p == '\\' && p[1]) {
            p += 1;
        }
        out[n++] = *p++;
    }
    out[n] = '\0';
    return n > 0 && *p == '"';
}

static int
read_bool(const char *p)
{
    return p && strncmp(p, "true", 4) == 0;
}

static int
slurp_file(const char *path, char **out)
{
    FILE *file;
    long size;
    char *buf;

    *out = NULL;
    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0 || size > 8 * 1024 * 1024) {
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
    *out = buf;
    return 1;
}

static int
find_asset_url(const char *block, const char *asset, char *out, size_t max)
{
    const char *p = block;

    if (!block || !asset || !out || max < 8) {
        return 0;
    }
    out[0] = '\0';
    while ((p = strstr(p, "browser_download_url")) != NULL) {
        const char *val = find_key(p, "browser_download_url");
        char url[1024];
        size_t n;
        size_t a;

        if (read_string(val, url, sizeof(url))) {
            n = strlen(url);
            a = strlen(asset);
            if (n >= a && strcmp(url + (n - a), asset) == 0 && strncmp(url, "https://", 8) == 0) {
                snprintf(out, max, "%s", url);
                return 1;
            }
        }
        p += 20;
    }
    return 0;
}

static int
pick_release(const char *json)
{
    const char *cursor = json;
    char best_ver[32];
    char best_url[1024];

    best_ver[0] = '\0';
    best_url[0] = '\0';
    g_version[0] = '\0';
    g_url[0] = '\0';
    if (!json) {
        return 0;
    }

    while (cursor && *cursor) {
        const char *tag_key = find_key(cursor, "tag_name");
        const char *pre;
        const char *draft;
        const char *next_tag;
        const char *end;
        char tag[64];

        if (!tag_key) {
            break;
        }
        if (!read_string(tag_key, tag, sizeof(tag))) {
            cursor = tag_key + 1;
            continue;
        }
        next_tag = strstr(tag_key + 10, "\"tag_name\"");
        end = next_tag ? next_tag : tag_key + strlen(tag_key);
        {
            size_t span = (size_t)(end - cursor);
            char *block = (char *)malloc(span + 1);
            const char *asset;
            char url[1024];

            if (!block) {
                cursor = tag_key + 1;
                continue;
            }
            memcpy(block, cursor, span);
            block[span] = '\0';
            pre = find_key(block, "prerelease");
            draft = find_key(block, "draft");
            if (read_bool(pre) || read_bool(draft) || !official_tag(tag)) {
                free(block);
                cursor = end;
                continue;
            }
            url[0] = '\0';
            asset = strstr(block, ASSET_NAME);
            if (asset) {
                find_asset_url(block, ASSET_NAME, url, sizeof(url));
            }
            if (strncmp(url, "https://", 8) == 0 &&
                (is_dev_build() || version_newer(APP_VERSION, tag)) &&
                version_newer(best_ver[0] ? best_ver : "0.0.0", tag)) {
                snprintf(best_ver, sizeof(best_ver), "%s", tag[0] == 'v' || tag[0] == 'V' ? tag + 1 : tag);
                snprintf(best_url, sizeof(best_url), "%s", url);
            }
            free(block);
        }
        cursor = end;
    }

    if (!best_ver[0] || !best_url[0]) {
        return 0;
    }
    snprintf(g_version, sizeof(g_version), "%s", best_ver);
    snprintf(g_url, sizeof(g_url), "%s", best_url);
    return 1;
}

static int
start_curl(const char *url, const char *dest, int github_json)
{
    char curl[MAX_PATH];
    char command[4096];
    const char *accept;

    find_curl(curl, sizeof(curl));
    accept = github_json
        ? " -H \"Accept: application/vnd.github+json\""
        : " -H \"Accept: application/octet-stream\"";
    snprintf(
        command,
        sizeof(command),
        "\"%s\" -fsSL --retry 2 -A \"DawnLauncher/%s\"%s -o \"%s\" \"%s\"",
        curl,
        APP_VERSION,
        accept,
        dest,
        url
    );
    return start_cmd(command);
}

static int
find_launcher(const char *dir, char *out, size_t max)
{
    char path[MAX_PATH];

#ifdef _WIN32
    if (os_join(path, sizeof(path), dir, "Dawn.exe") && os_file_exists(path)) {
        snprintf(out, max, "Dawn.exe");
        return 1;
    }
    if (os_join(path, sizeof(path), dir, "host.exe") && os_file_exists(path)) {
        snprintf(out, max, "host.exe");
        return 1;
    }
#else
    if (os_join(path, sizeof(path), dir, "Dawn") && os_file_exists(path)) {
        snprintf(out, max, "Dawn");
        return 1;
    }
    if (os_join(path, sizeof(path), dir, "host") && os_file_exists(path)) {
        snprintf(out, max, "host");
        return 1;
    }
#endif
    return 0;
}

static int
apply_update(void)
{
    char dest[MAX_PATH];
    char exe[64];
    char script[MAX_PATH];
    char command[4096];
    FILE *file;

    if (!os_exe_dir(dest, sizeof(dest)) || !dest[0]) {
        set_status("Update folder missing");
        return 0;
    }
    if (!find_launcher(g_unpack, exe, sizeof(exe))) {
        set_status("Update package incomplete");
        return 0;
    }
#ifdef _WIN32
    if (!os_join(script, sizeof(script), g_work, "apply.cmd")) {
        return 0;
    }
    file = fopen(script, "wb");
    if (!file) {
        set_status("Could not write updater");
        return 0;
    }
    fputs(
        "@echo off\r\n"
        "setlocal\r\n"
        "set PID=%1\r\n"
        "set SRC=%~2\r\n"
        "set DST=%~3\r\n"
        "set EXE=%~4\r\n"
        ":wait\r\n"
        "ping -n 2 127.0.0.1 >nul\r\n"
        "tasklist /fi \"PID eq %PID%\" 2>nul | findstr /i \"%PID%\" >nul && goto wait\r\n"
        "set TRY=0\r\n"
        ":copy\r\n"
        "xcopy /e /y /q \"%SRC%\\*\" \"%DST%\\\" >nul\r\n"
        "if errorlevel 1 (\r\n"
        "  set /a TRY+=1\r\n"
        "  if %TRY% lss 20 (\r\n"
        "    ping -n 2 127.0.0.1 >nul\r\n"
        "    goto copy\r\n"
        "  )\r\n"
        ")\r\n"
        "start \"\" \"%DST%\\%EXE%\"\r\n"
        "rmdir /s /q \"%SRC%\" >nul 2>nul\r\n"
        "del \"%~f0\" >nul 2>nul\r\n",
        file
    );
    fclose(file);
    snprintf(
        command,
        sizeof(command),
        "cmd.exe /c \"\"%s\" %lu \"%s\" \"%s\" \"%s\"\"",
        script,
        (unsigned long)GetCurrentProcessId(),
        g_unpack,
        dest,
        exe
    );
    {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        char runnable[4096];

        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        snprintf(runnable, sizeof(runnable), "%s", command);
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        if (!CreateProcessA(
                NULL,
                runnable,
                NULL,
                NULL,
                FALSE,
                CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB,
                NULL,
                NULL,
                &si,
                &pi
            )) {
            set_status("Could not start updater");
            return 0;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
#else
    if (!os_join(script, sizeof(script), g_work, "apply.sh")) {
        return 0;
    }
    file = fopen(script, "wb");
    if (!file) {
        set_status("Could not write updater");
        return 0;
    }
    fputs(
        "#!/bin/sh\n"
        "pid=\"$1\"\n"
        "src=\"$2\"\n"
        "dst=\"$3\"\n"
        "exe=\"$4\"\n"
        "while kill -0 \"$pid\" 2>/dev/null; do sleep 0.3; done\n"
        "i=0\n"
        "while [ \"$i\" -lt 20 ]; do\n"
        "  cp -a \"$src\"/. \"$dst\"/ && break\n"
        "  i=$((i + 1))\n"
        "  sleep 0.3\n"
        "done\n"
        "chmod +x \"$dst/$exe\" 2>/dev/null || true\n"
        "\"$dst/$exe\" >/dev/null 2>&1 &\n"
        "rm -rf \"$src\"\n"
        "rm -f \"$0\"\n",
        file
    );
    fclose(file);
    chmod(script, 0755);
    snprintf(
        command,
        sizeof(command),
        "nohup /bin/sh \"%s\" %d \"%s\" \"%s\" \"%s\" >/dev/null 2>&1 &",
        script,
        (int)getpid(),
        g_unpack,
        dest,
        exe
    );
    if (!run_cmd(command)) {
        set_status("Could not start updater");
        return 0;
    }
#endif
    g_quit = 1;
    set_status("Restarting");
    return 1;
}

static void
finish_check(int ok)
{
    char *json = NULL;

    close_child();
    if (!ok || !slurp_file(g_json, &json) || !pick_release(json)) {
        free(json);
        g_phase = UPD_IDLE;
        g_next_check = os_tick_ms() + CHECK_MS;
        g_version[0] = '\0';
        g_url[0] = '\0';
        set_status("");
        return;
    }
    free(json);
    g_phase = UPD_READY;
    {
        char line[160];
        snprintf(line, sizeof(line), "Dawn %s is available", g_version);
        set_status(line);
    }
}

static int
package_looks_valid(void)
{
    FILE *file;
    unsigned char mag[4];

    file = fopen(g_pkg, "rb");
    if (!file) {
        return 0;
    }
    if (fread(mag, 1, 4, file) != 4) {
        fclose(file);
        return 0;
    }
    fclose(file);
#ifdef _WIN32
    return mag[0] == 'P' && mag[1] == 'K';
#else
    return (mag[0] == 0x1f && mag[1] == 0x8b) || (mag[0] == 'P' && mag[1] == 'K');
#endif
}

static int
extract_package(void)
{
    char command[4096];

#ifdef _WIN32
    {
        char sys[MAX_PATH];
        char tar[MAX_PATH];
        UINT n;

        snprintf(command, sizeof(command), "cmd.exe /c if exist \"%s\" rmdir /s /q \"%s\"", g_unpack, g_unpack);
        run_cmd(command);
        os_mkdirs(g_unpack);
        n = GetSystemDirectoryA(sys, (UINT)sizeof(sys));
        if (n > 0 && n < sizeof(sys) && os_join(tar, sizeof(tar), sys, "tar.exe") && os_file_exists(tar)) {
            snprintf(command, sizeof(command), "\"%s\" -xf \"%s\" -C \"%s\"", tar, g_pkg, g_unpack);
            if (run_cmd(command)) {
                return 1;
            }
        }
        snprintf(
            command,
            sizeof(command),
            "powershell.exe -NoProfile -Command \"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\"",
            g_pkg,
            g_unpack
        );
        return run_cmd(command);
    }
#else
    snprintf(command, sizeof(command), "rm -rf \"%s\" && mkdir -p \"%s\"", g_unpack, g_unpack);
    run_cmd(command);
    snprintf(command, sizeof(command), "tar -xf \"%s\" -C \"%s\"", g_pkg, g_unpack);
    return run_cmd(command);
#endif
}

static void
finish_download(int ok)
{
    close_child();
    if (!ok || !os_file_exists(g_pkg) || os_file_size(g_pkg) < 1024) {
        g_phase = UPD_FAILED;
        set_status("Update download failed");
        return;
    }
    if (!package_looks_valid()) {
        g_phase = UPD_FAILED;
        set_status("Update download was not a package");
        return;
    }
    set_status("Installing update");
    if (!extract_package()) {
        g_phase = UPD_FAILED;
        set_status("Update extract failed");
        return;
    }
    g_phase = UPD_APPLY;
    if (!apply_update()) {
        g_phase = UPD_FAILED;
    }
}

static void
begin_check(void)
{
    os_mkdirs(g_work);
    if (!start_curl(RELEASES_API, g_json, 1)) {
        g_phase = UPD_IDLE;
        g_next_check = os_tick_ms() + CHECK_MS;
        return;
    }
    g_phase = UPD_CHECKING;
    set_status("");
}

void
self_update_init(void)
{
    char dawn[MAX_PATH];

    memset(g_version, 0, sizeof(g_version));
    memset(g_url, 0, sizeof(g_url));
    memset(g_status, 0, sizeof(g_status));
    g_phase = UPD_IDLE;
    g_quit = 0;
    g_next_check = os_tick_ms() + FIRST_CHECK_MS;
    os_data_dir(dawn, sizeof(dawn));
    if (!os_join(g_work, sizeof(g_work), dawn, "update") ||
        !os_join(g_json, sizeof(g_json), g_work, "releases.json") ||
        !os_join(g_pkg, sizeof(g_pkg), g_work, "package.bin") ||
        !os_join(g_unpack, sizeof(g_unpack), g_work, "unpack")) {
        g_work[0] = '\0';
    }
    os_mkdirs(g_work);
    if (is_dev_build()) {
        set_status("Checking for update");
    }
}

void
self_update_shutdown(void)
{
    if (g_phase == UPD_CHECKING || g_phase == UPD_DOWNLOADING) {
#ifdef _WIN32
        if (g_process) {
            TerminateProcess(g_process, 1);
        }
#else
        if (g_process > 0) {
            kill(g_process, SIGTERM);
        }
#endif
    }
    close_child();
}

void
self_update_poll(void)
{
    int child;

    if (g_quit) {
        return;
    }
    if (g_phase == UPD_CHECKING || g_phase == UPD_DOWNLOADING) {
        child = child_running();
        if (child == 1) {
            return;
        }
        if (g_phase == UPD_CHECKING) {
            finish_check(child == -1);
        } else {
            finish_download(child == -1);
        }
        return;
    }
    if (g_phase == UPD_IDLE && g_work[0] && os_tick_ms() >= g_next_check) {
        begin_check();
    }
}

int
self_update_available(void)
{
    if (is_dev_build()) {
        return 1;
    }
    if (!g_version[0]) {
        return 0;
    }
    if (g_phase != UPD_READY && g_phase != UPD_DOWNLOADING && g_phase != UPD_APPLY &&
        g_phase != UPD_FAILED) {
        return 0;
    }
    return version_newer(APP_VERSION, g_version);
}

const char *
self_update_version(void)
{
    return g_version;
}

int
self_update_busy(void)
{
    return g_phase == UPD_DOWNLOADING || g_phase == UPD_APPLY;
}

const char *
self_update_status(void)
{
    return g_status;
}

int
self_update_begin(void)
{
    if (g_phase == UPD_CHECKING) {
        set_status("Checking for update");
        return 1;
    }
    if (g_phase == UPD_IDLE) {
        begin_check();
        set_status("Checking for update");
        return 1;
    }
    if (g_phase != UPD_READY && g_phase != UPD_FAILED) {
        return 0;
    }
    if (!g_url[0]) {
        return 0;
    }
    os_mkdirs(g_work);
    if (!start_curl(g_url, g_pkg, 0)) {
        g_phase = UPD_FAILED;
        set_status("Could not start download");
        return 0;
    }
    g_phase = UPD_DOWNLOADING;
    set_status("Downloading update");
    return 1;
}

void
self_update_cancel(void)
{
    if (g_phase == UPD_DOWNLOADING) {
#ifdef _WIN32
        if (g_process) {
            TerminateProcess(g_process, 1);
        }
#else
        if (g_process > 0) {
            kill(g_process, SIGTERM);
        }
#endif
        close_child();
        g_phase = UPD_READY;
        set_status("Update cancelled");
        return;
    }
    if (g_phase == UPD_FAILED) {
        g_phase = UPD_READY;
        {
            char line[160];
            snprintf(line, sizeof(line), "Dawn %s is available", g_version);
            set_status(line);
        }
    }
}

int
self_update_should_quit(void)
{
    return g_quit;
}
