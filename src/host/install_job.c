#include "install_job.h"
#include "steam_auth.h"
#include "shared/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
typedef unsigned int DWORD;
#ifndef SecureZeroMemory
#define SecureZeroMemory(p, n) os_zero_memory((p), (n))
#endif
#ifndef _strnicmp
#define _strnicmp os_strnicmp
#endif
#endif

#define INSTALL_APP "1085660"
#define INSTALL_DEPOT_CONTENT "1085661"
#define INSTALL_MANIFEST_CONTENT "7180122903232116872"
#define INSTALL_DEPOT_LANG "1085662"
#define INSTALL_MANIFEST_LANG "2210332166360342287"
#define INSTALL_MARKER ".dawn-ready"
#define GAME_EXE_VERSION "86657.20.08.23"
#define GAME_EXE_MAJOR 21122
#define GAME_EXE_SIZE 122984224ull
#define DAWN_RELEASES_API "https://api.github.com/repos/isinternets/Dawn/releases/latest"
#define DAWN_FALLBACK_ZIP "https://github.com/isinternets/Dawn/releases/download/0.1.3/Dawn-0.1.3.zip"
#define DAWN_EXE_VERSION "86657.20.08.23.1800.d2_rc"

typedef enum InstallStep {
    STEP_DEPOT_CONTENT = 0,
    STEP_DAWN_RELEASE = 1
} InstallStep;

typedef enum InstallPhase {
    INSTALL_IDLE = 0,
    INSTALL_RUNNING,
    INSTALL_OK,
    INSTALL_FAILED
} InstallPhase;

static char g_root[MAX_PATH];
static char g_dir[MAX_PATH];
static char g_user[128];
static char g_tool[MAX_PATH];
static char g_secret[256];
static char g_status[256];
static char g_log[4096];
static size_t g_log_len;
static InstallPhase g_phase;
static InstallNeed g_need;
static int g_step;
static int g_verify;
static uint32_t g_scan_check;
static char g_dawn_zip_url[1024];
static char g_dawn_tag[64];
static int g_busy;
static float g_depot_progress;
static int g_installed;
static uint32_t g_installed_check;
static int g_dir_pinned;
static int g_session_job;
static int g_session_ready;
static int g_session_tried;
#ifdef _WIN32
static HANDLE g_process;
static HANDLE g_stdout_read;
static HANDLE g_stdin_write;
#else
static pid_t g_process;
static int g_stdout_read;
static int g_stdin_write;
#endif

static void
set_status(const char *text)
{
    snprintf(g_status, sizeof(g_status), "%s", text ? text : "");
}

static void
clear_secret(void)
{
    SecureZeroMemory(g_secret, sizeof(g_secret));
}

static void
append_log(const char *data, DWORD len)
{
    if (!data || len == 0) {
        return;
    }
    if (g_log_len + len >= sizeof(g_log)) {
        size_t drop = (g_log_len + len + 1) - sizeof(g_log);
        if (drop < g_log_len) {
            memmove(g_log, g_log + drop, g_log_len - drop);
            g_log_len -= drop;
        } else {
            g_log_len = 0;
        }
    }
    if (g_log_len + len >= sizeof(g_log)) {
        len = (DWORD)(sizeof(g_log) - g_log_len - 1);
    }
    memcpy(g_log + g_log_len, data, len);
    g_log_len += len;
    g_log[g_log_len] = '\0';
}

static int
contains_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (n == 0) {
        return 1;
    }
    for (const char *p = hay; *p; p++) {
        if (_strnicmp(p, needle, n) == 0) {
            return 1;
        }
    }
    return 0;
}

static float
parse_percent(const char *text)
{
    float best = -1.0f;
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') {
            continue;
        }
        char *end = NULL;
        float value = strtof(p, &end);
        if (end > p && *end == '%' && value >= 0.0f && value <= 100.0f) {
            best = value / 100.0f;
        }
        p = end > p ? end - 1 : p;
    }
    return best;
}

static void
close_child(void)
{
#ifdef _WIN32
    if (g_stdin_write) {
        CloseHandle(g_stdin_write);
        g_stdin_write = NULL;
    }
    if (g_stdout_read) {
        CloseHandle(g_stdout_read);
        g_stdout_read = NULL;
    }
    if (g_process) {
        CloseHandle(g_process);
        g_process = NULL;
    }
#else
    if (g_stdin_write >= 0) {
        close(g_stdin_write);
        g_stdin_write = -1;
    }
    if (g_stdout_read >= 0) {
        close(g_stdout_read);
        g_stdout_read = -1;
    }
    g_process = 0;
#endif
}

static void
fail_job(const char *text)
{
    close_child();
    g_busy = 0;
    g_verify = 0;
    g_need = INSTALL_NEED_NONE;
    g_phase = INSTALL_FAILED;
    set_status(text);
    clear_secret();
}

static int
file_exists(const char *path)
{
    return os_file_exists(path);
}

static void
persist_dir(void)
{
    char dawn[MAX_PATH];
    char path[MAX_PATH];
    FILE *file;

    os_data_dir(dawn, sizeof(dawn));
    if (!os_join(path, sizeof(path), dawn, "install_dir.txt")) {
        return;
    }
    file = fopen(path, "wb");
    if (!file) {
        return;
    }
    fputs(g_dir, file);
    if (g_dir_pinned) {
        fputs("\npinned\n", file);
    }
    fclose(file);
}

#define WALK_BUDGET 800
#define WALK_CHILD_MAX 64
#define WALK_NAME_MAX 160
#define WALK_PATH_MAX 520

typedef struct NameMatch {
    const char *dir;
    const char *want;
    char *out;
    size_t max;
    int want_dir;
    int hit;
} NameMatch;

typedef struct WalkKids {
    char parent[WALK_PATH_MAX];
    char path[WALK_CHILD_MAX][WALK_PATH_MAX];
    char name[WALK_CHILD_MAX][WALK_NAME_MAX];
    int is_dir[WALK_CHILD_MAX];
    int count;
} WalkKids;

typedef struct WalkState {
    char found[MAX_PATH];
    int budget;
    int hit;
} WalkState;

static const char *
path_base(const char *path)
{
    const char *base = path ? path : "";
    const char *p;

    for (p = base; *p; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    return base;
}

static int
copy_env(const char *key, char *out, size_t max)
{
    if (!key || !out || max < 2) {
        return 0;
    }
#ifdef _WIN32
    {
        DWORD n = GetEnvironmentVariableA(key, out, (DWORD)max);
        return n > 0 && n < (DWORD)max;
    }
#else
    {
        const char *value = getenv(key);
        if (!value || !value[0]) {
            return 0;
        }
        snprintf(out, max, "%s", value);
        return 1;
    }
#endif
}

static int
path_parent(char *out, size_t max, const char *path)
{
    size_t n;

    if (!out || !path || !path[0] || max < 2) {
        return 0;
    }
    snprintf(out, max, "%s", path);
    n = strlen(out);
    while (n > 0 && (out[n - 1] == '/' || out[n - 1] == '\\')) {
        out[--n] = '\0';
    }
    while (n > 0 && out[n - 1] != '/' && out[n - 1] != '\\') {
        out[--n] = '\0';
    }
    while (n > 0 && (out[n - 1] == '/' || out[n - 1] == '\\')) {
        out[--n] = '\0';
    }
    return n > 0 && out[0] != '\0';
}

static int
list_any_cb(const char *name, int is_dir, void *user)
{
    int *found = (int *)user;
    (void)name;
    (void)is_dir;
    *found = 1;
    return 0;
}

static int
name_match_cb(const char *name, int is_dir, void *user)
{
    NameMatch *match = (NameMatch *)user;
    char path[MAX_PATH];

    if (!name || match->hit) {
        return 0;
    }
    if (match->want_dir) {
        if (!is_dir || os_stricmp(name, match->want) != 0) {
            return 1;
        }
    } else if (is_dir || os_stricmp(name, match->want) != 0) {
        return 1;
    }
    if (!os_join(path, sizeof(path), match->dir, name)) {
        return 1;
    }
    if (!match->want_dir && os_file_size(path) <= 1024ull * 1024ull) {
        return 1;
    }
    if (match->out && match->max > 0) {
        snprintf(match->out, match->max, "%s", path);
    }
    match->hit = 1;
    return 0;
}

static int
find_named(const char *dir, const char *want, int want_dir, char *out, size_t max)
{
    NameMatch match;

    if (!dir || !dir[0] || !want || !os_dir_exists(dir)) {
        return 0;
    }
    memset(&match, 0, sizeof(match));
    match.dir = dir;
    match.want = want;
    match.out = out;
    match.max = max;
    match.want_dir = want_dir;
    os_list_dir(dir, name_match_cb, &match);
    return match.hit;
}

static int
find_game_exe_in(const char *dir, char *out, size_t max)
{
    char nested[MAX_PATH];

    if (find_named(dir, "Destiny2.exe", 0, out, max)) {
        return 1;
    }
    if (find_named(dir, "Destiny2", 1, nested, sizeof(nested)) &&
        find_named(nested, "Destiny2.exe", 0, out, max)) {
        return 1;
    }
    return 0;
}

static int
find_game_exe(char *out, size_t max)
{
    return find_game_exe_in(g_dir, out, max);
}

static int
packages_ready(const char *dir)
{
    char path[MAX_PATH];
    int found = 0;

    if (!find_named(dir, "packages", 1, path, sizeof(path))) {
        return 0;
    }
    os_list_dir(path, list_any_cb, &found);
    return found;
}

static int
file_contains(const char *path, const char *needle)
{
    FILE *file;
    char buf[512];

    if (!file_exists(path) || !needle) {
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    while (fgets(buf, (int)sizeof(buf), file)) {
        if (strstr(buf, needle)) {
            fclose(file);
            return 1;
        }
    }
    fclose(file);
    return 0;
}

static int
marker_file_ok(const char *dir, const char *name)
{
    char path[MAX_PATH];

    if (!os_join(path, sizeof(path), dir, name)) {
        return 0;
    }
    return file_contains(path, INSTALL_MANIFEST_CONTENT) &&
        file_contains(path, INSTALL_MANIFEST_LANG);
}

static int
marker_matches(const char *dir)
{
    return marker_file_ok(dir, INSTALL_MARKER);
}

static int
join4(char *out, size_t max, const char *a, const char *b, const char *c, const char *d)
{
    char mid[MAX_PATH];
    char mid2[MAX_PATH];

    return os_join(mid, sizeof(mid), a, b) &&
        os_join(mid2, sizeof(mid2), mid, c) &&
        os_join(out, max, mid2, d);
}

static int
steam_dll_path(const char *dir, char *out, size_t max)
{
    return join4(out, max, dir, "bin", "x64", "steam_api64.dll");
}

static int
steam_dll_present(const char *dir)
{
    char path[MAX_PATH];

    return steam_dll_path(dir, path, sizeof(path)) && file_exists(path);
}

static int
dawn_settings_in(const char *dir, char *out, size_t max)
{
    char dawn[MAX_PATH];
    char nested[MAX_PATH];

    if (os_join(dawn, sizeof(dawn), dir, "Dawn") &&
        os_join(out, max, dawn, "settings.json") &&
        file_exists(out)) {
        return 1;
    }
    if (join4(nested, sizeof(nested), dir, "bin", "x64", "Dawn") &&
        os_join(out, max, nested, "settings.json") &&
        file_exists(out)) {
        return 1;
    }
    return 0;
}

static int
dll_product_is(const char *path, const char *want)
{
#ifdef _WIN32
    DWORD handle = 0;
    DWORD size;
    void *data;
    UINT len = 0;
    struct {
        WORD lang;
        WORD code;
    } *trans = NULL;
    char name[128];

    if (!path || !want || !file_exists(path)) {
        return 0;
    }
    size = GetFileVersionInfoSizeA(path, &handle);
    if (size == 0 || size > (1u << 20)) {
        return 0;
    }
    data = malloc(size);
    if (!data) {
        return 0;
    }
    name[0] = '\0';
    if (GetFileVersionInfoA(path, 0, size, data) &&
        VerQueryValueA(data, "\\VarFileInfo\\Translation", (LPVOID *)&trans, &len) &&
        trans &&
        len >= sizeof(*trans)) {
        char key[80];
        LPVOID val = NULL;
        UINT vlen = 0;
        snprintf(
            key,
            sizeof(key),
            "\\StringFileInfo\\%04x%04x\\ProductName",
            trans[0].lang,
            trans[0].code
        );
        if (VerQueryValueA(data, key, &val, &vlen) && val) {
            snprintf(name, sizeof(name), "%s", (const char *)val);
        }
    }
    free(data);
    return name[0] && os_stricmp(name, want) == 0;
#else
    (void)path;
    (void)want;
    return 0;
#endif
}

static int
dawn_dll_path(const char *dir, char *out, size_t max)
{
    char nested[MAX_PATH];

    if (steam_dll_path(dir, nested, sizeof(nested)) && file_exists(nested)) {
        if (out && max > 0) {
            snprintf(out, max, "%s", nested);
        }
        return 1;
    }
    if (os_join(nested, sizeof(nested), dir, "steam_api64.dll") && file_exists(nested)) {
        if (out && max > 0) {
            snprintf(out, max, "%s", nested);
        }
        return 1;
    }
    return 0;
}

static int
dawn_dll_ready(const char *dir)
{
    char path[MAX_PATH];
    uint64_t size;

    if (!dawn_dll_path(dir, path, sizeof(path))) {
        return 0;
    }
#ifdef _WIN32
    (void)size;
    return dll_product_is(path, "Dawn");
#else
    size = os_file_size(path);
    return size > 8ull * 1024ull * 1024ull && size < 50ull * 1024ull * 1024ull;
#endif
}

static int
dawn_ready(const char *dir)
{
    char settings[MAX_PATH];

    return dir && dir[0] && dawn_settings_in(dir, settings, sizeof(settings)) && dawn_dll_ready(dir);
}

static int
destiny2_running(void)
{
#ifdef _WIN32
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe;
    int found = 0;

    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            if (os_stricmp(pe.szExeFile, "destiny2.exe") == 0) {
                found = 1;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return found;
#else
    return system("pidof -q destiny2.exe") == 0 || system("pidof -q destiny2") == 0;
#endif
}

typedef struct DepotNameScan {
    int content;
    int lang;
} DepotNameScan;

static int
depot_name_cb(const char *name, int is_dir, void *user)
{
    DepotNameScan *scan = (DepotNameScan *)user;
    (void)is_dir;
    if (!name) {
        return 1;
    }
    if (strstr(name, INSTALL_DEPOT_CONTENT) || strstr(name, INSTALL_MANIFEST_CONTENT)) {
        scan->content = 1;
    }
    if (strstr(name, INSTALL_DEPOT_LANG) || strstr(name, INSTALL_MANIFEST_LANG)) {
        scan->lang = 1;
    }
    return 1;
}

static int
dawn_depots_present(const char *dir)
{
    char trace[MAX_PATH];
    DepotNameScan scan;

    if (marker_matches(dir)) {
        return 1;
    }
    memset(&scan, 0, sizeof(scan));
    if (os_join(trace, sizeof(trace), dir, ".DepotDownloader") && os_dir_exists(trace)) {
        os_list_dir(trace, depot_name_cb, &scan);
    }
    os_list_dir(dir, depot_name_cb, &scan);
    return scan.content && scan.lang;
}

typedef struct ForeignDepotScan {
    int foreign;
} ForeignDepotScan;

static int
parse_depot_trace_name(const char *name, char *depot, size_t depot_max, char *manifest, size_t man_max)
{
    const char *us;
    const char *dot;
    size_t depot_len;
    size_t man_len;

    if (!name || strstr(name, ".sha")) {
        return 0;
    }
    if (strncmp(name, "manifest_", 9) == 0) {
        name += 9;
    }
    us = strchr(name, '_');
    if (!us || us == name) {
        return 0;
    }
    dot = strrchr(us + 1, '.');
    if (!dot || dot <= us + 1) {
        return 0;
    }
    depot_len = (size_t)(us - name);
    man_len = (size_t)(dot - (us + 1));
    if (depot_len >= depot_max || man_len >= man_max || man_len < 6) {
        return 0;
    }
    memcpy(depot, name, depot_len);
    depot[depot_len] = '\0';
    memcpy(manifest, us + 1, man_len);
    manifest[man_len] = '\0';
    return 1;
}

static int
foreign_depot_cb(const char *name, int is_dir, void *user)
{
    ForeignDepotScan *scan = (ForeignDepotScan *)user;
    char depot[32];
    char manifest[64];

    (void)is_dir;
    if (!name || scan->foreign) {
        return scan->foreign ? 0 : 1;
    }
    if (!parse_depot_trace_name(name, depot, sizeof(depot), manifest, sizeof(manifest))) {
        return 1;
    }
    if (strcmp(depot, INSTALL_DEPOT_CONTENT) == 0 && strcmp(manifest, INSTALL_MANIFEST_CONTENT) != 0) {
        scan->foreign = 1;
        return 0;
    }
    if (strcmp(depot, INSTALL_DEPOT_LANG) == 0 && strcmp(manifest, INSTALL_MANIFEST_LANG) != 0) {
        scan->foreign = 1;
        return 0;
    }
    return 1;
}

static int
scan_foreign_depot_dir(const char *dir)
{
    ForeignDepotScan scan;

    if (!dir || !os_dir_exists(dir)) {
        return 0;
    }
    memset(&scan, 0, sizeof(scan));
    os_list_dir(dir, foreign_depot_cb, &scan);
    return scan.foreign;
}

static int
json_lists_foreign_manifest(const char *path)
{
    if (!file_exists(path)) {
        return 0;
    }
    if (file_contains(path, INSTALL_MANIFEST_CONTENT) && file_contains(path, INSTALL_MANIFEST_LANG)) {
        return 0;
    }
    return file_contains(path, INSTALL_DEPOT_CONTENT) || file_contains(path, INSTALL_DEPOT_LANG);
}

static int
acf_lists_foreign_manifest(const char *path)
{
    if (!file_exists(path)) {
        return 0;
    }
    if (!file_contains(path, INSTALL_DEPOT_CONTENT) && !file_contains(path, "\"1085660\"")) {
        return 0;
    }
    if (file_contains(path, INSTALL_MANIFEST_CONTENT)) {
        return 0;
    }
    return file_contains(path, "\"manifest\"");
}

static int looks_like_steam_common(const char *dir);

static int
nearby_steam_acf_is_latest(const char *dir)
{
    char cur[MAX_PATH];
    char next[MAX_PATH];
    char acf[MAX_PATH];
    int hops;

    if (!looks_like_steam_common(dir)) {
        return 0;
    }
    snprintf(cur, sizeof(cur), "%s", dir);
    for (hops = 0; hops < 5; hops++) {
        if (os_stricmp(path_base(cur), "steamapps") == 0 &&
            os_join(acf, sizeof(acf), cur, "appmanifest_1085660.acf") &&
            acf_lists_foreign_manifest(acf)) {
            return 1;
        }
        if (!path_parent(next, sizeof(next), cur)) {
            break;
        }
        snprintf(cur, sizeof(cur), "%s", next);
    }
    return 0;
}

static int
has_live_runtime_files(const char *dir)
{
    char path[MAX_PATH];

    if (!dir || !dir[0]) {
        return 0;
    }
    if (os_join(path, sizeof(path), dir, "EasyAntiCheat") && os_dir_exists(path)) {
        return 1;
    }
    if (os_join(path, sizeof(path), dir, "EasyAntiCheat_EOS") && os_dir_exists(path)) {
        return 1;
    }
    if (os_join(path, sizeof(path), dir, "start_protected_game.exe") && file_exists(path)) {
        return 1;
    }
    if (os_join(path, sizeof(path), dir, "eac_launcher.exe") && file_exists(path)) {
        return 1;
    }
    if (os_join(path, sizeof(path), dir, "EasyAntiCheat_EOS_Setup.exe") && file_exists(path)) {
        return 1;
    }
    return 0;
}

static int
exe_version_is_live(const char *dir)
{
#ifdef _WIN32
    char exe[MAX_PATH];
    DWORD handle = 0;
    DWORD size;
    void *data;
    UINT len = 0;
    VS_FIXEDFILEINFO *ffi = NULL;
    struct {
        WORD lang;
        WORD code;
    } *trans = NULL;
    char filever[128];
    char prodver[128];
    unsigned major = 0;

    if (!find_game_exe_in(dir, exe, sizeof(exe))) {
        return 0;
    }
    size = GetFileVersionInfoSizeA(exe, &handle);
    if (size == 0 || size > (1u << 20)) {
        return 0;
    }
    data = malloc(size);
    if (!data) {
        return 0;
    }
    filever[0] = '\0';
    prodver[0] = '\0';
    if (!GetFileVersionInfoA(exe, 0, size, data)) {
        free(data);
        return 0;
    }
    if (VerQueryValueA(data, "\\", (LPVOID *)&ffi, &len) && ffi && len >= sizeof(*ffi)) {
        major = (unsigned)HIWORD(ffi->dwFileVersionMS);
    }
    if (VerQueryValueA(data, "\\VarFileInfo\\Translation", (LPVOID *)&trans, &len) &&
        trans &&
        len >= sizeof(*trans)) {
        char key[80];
        LPVOID val = NULL;
        UINT vlen = 0;
        snprintf(
            key,
            sizeof(key),
            "\\StringFileInfo\\%04x%04x\\FileVersion",
            trans[0].lang,
            trans[0].code
        );
        if (VerQueryValueA(data, key, &val, &vlen) && val) {
            snprintf(filever, sizeof(filever), "%s", (const char *)val);
        }
        snprintf(
            key,
            sizeof(key),
            "\\StringFileInfo\\%04x%04x\\ProductVersion",
            trans[0].lang,
            trans[0].code
        );
        val = NULL;
        vlen = 0;
        if (VerQueryValueA(data, key, &val, &vlen) && val) {
            snprintf(prodver, sizeof(prodver), "%s", (const char *)val);
        }
    }
    free(data);
    if (filever[0] == '\0' && prodver[0] == '\0' && major == 0) {
        return 0;
    }
    if (strstr(filever, GAME_EXE_VERSION) ||
        strstr(filever, DAWN_EXE_VERSION) ||
        strstr(prodver, GAME_EXE_VERSION) ||
        strstr(prodver, DAWN_EXE_VERSION) ||
        major == (unsigned)GAME_EXE_MAJOR) {
        return 0;
    }
    return 1;
#else
    (void)dir;
    return 0;
#endif
}

static int
is_live_latest_d2(const char *dir)
{
    char path[MAX_PATH];

    if (!dir || !dir[0] || !os_dir_exists(dir)) {
        return 0;
    }
    if (find_game_exe_in(dir, path, sizeof(path)) && os_file_size(path) == GAME_EXE_SIZE) {
        return 0;
    }
    if (os_join(path, sizeof(path), dir, ".DepotDownloader") && scan_foreign_depot_dir(path)) {
        return 1;
    }
    if (scan_foreign_depot_dir(dir)) {
        return 1;
    }
    if (os_join(path, sizeof(path), dir, "install-state.json") && json_lists_foreign_manifest(path)) {
        return 1;
    }
    if (nearby_steam_acf_is_latest(dir)) {
        return 1;
    }
    if (has_live_runtime_files(dir)) {
        return 1;
    }
    if (exe_version_is_live(dir)) {
        return 1;
    }
    return 0;
}

static int
looks_like_steam_common(const char *dir)
{
    return dir && contains_ci(dir, "steamapps") && contains_ci(dir, "common");
}

static int
game_root_ok(const char *dir)
{
    if (!find_game_exe_in(dir, NULL, 0) || !packages_ready(dir)) {
        return 0;
    }
    if (is_live_latest_d2(dir)) {
        return 0;
    }
    if (looks_like_steam_common(dir) && !marker_matches(dir) && !dawn_depots_present(dir)) {
        return 0;
    }
    return 1;
}

static int
game_root_in(const char *dir, char *out, size_t max)
{
    char nested[MAX_PATH];

    if (!dir || !dir[0] || !os_dir_exists(dir)) {
        return 0;
    }
    if (game_root_ok(dir)) {
        if (out && max > 0) {
            snprintf(out, max, "%s", dir);
        }
        return 1;
    }
    if (find_named(dir, "Destiny2", 1, nested, sizeof(nested)) && game_root_ok(nested)) {
        if (out && max > 0) {
            snprintf(out, max, "%s", nested);
        }
        return 1;
    }
    return 0;
}

static int
adopt_dir(const char *dir)
{
    if (!dir || !dir[0]) {
        return 0;
    }
    snprintf(g_dir, sizeof(g_dir), "%s", dir);
    persist_dir();
    return 1;
}

static int
skip_walk_name(const char *name)
{
    static const char *skip[] = {
        ".git", ".svn", ".hg", ".cache", ".npm", ".cargo", ".rustup", ".nuget",
        ".gradle", ".Trash", ".trash",
        "node_modules", "__pycache__",
        "Temp", "tmp", "Cache", "Caches",
        "Windows", "System32", "SysWOW64", "WinSxS",
        "$Recycle.Bin", "System Volume Information",
        "AppData", "Library", "Pictures", "Music", "Videos",
        "proc", "sys", "dev", "compatdata",
        NULL
    };
    int i;

    if (!name || !name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 1;
    }
    for (i = 0; skip[i]; i++) {
        if (os_stricmp(name, skip[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int
is_game_folder_name(const char *name)
{
    return name &&
        (contains_ci(name, "dawn") ||
         contains_ci(name, "destiny"));
}

static int
is_pass_through_name(const char *name)
{
    return name &&
        (os_stricmp(name, ".local") == 0 ||
         os_stricmp(name, "share") == 0 ||
         os_stricmp(name, ".steam") == 0 ||
         os_stricmp(name, "steam") == 0 ||
         os_stricmp(name, ".wine") == 0 ||
         os_stricmp(name, "drive_c") == 0 ||
         os_stricmp(name, "drive_d") == 0 ||
         os_stricmp(name, "users") == 0 ||
         os_stricmp(name, "steamapps") == 0 ||
         os_stricmp(name, "common") == 0 ||
         os_stricmp(name, "pfx") == 0);
}

static int
is_bridge_name(const char *name)
{
    if (!name) {
        return 0;
    }
    if (is_game_folder_name(name) || is_pass_through_name(name) || contains_ci(name, "game")) {
        return 1;
    }
    return os_stricmp(name, "Documents") == 0 ||
        os_stricmp(name, "Downloads") == 0 ||
        os_stricmp(name, "Desktop") == 0 ||
        os_stricmp(name, "Program Files") == 0 ||
        os_stricmp(name, "Program Files (x86)") == 0;
}

static int
allow_hidden_name(const char *name)
{
    return name &&
        (os_stricmp(name, ".local") == 0 ||
         os_stricmp(name, ".steam") == 0 ||
         os_stricmp(name, ".wine") == 0 ||
         os_stricmp(name, ".var") == 0 ||
         os_stricmp(name, ".dawn") == 0 ||
         os_stricmp(name, INSTALL_MARKER) == 0);
}

static int
collect_walk_cb(const char *name, int is_dir, void *user)
{
    WalkKids *kids = (WalkKids *)user;

    if (!name || kids->count >= WALK_CHILD_MAX) {
        return 0;
    }
    if (!is_dir || skip_walk_name(name) || (name[0] == '.' && !allow_hidden_name(name))) {
        return 1;
    }
    if (!os_join(kids->path[kids->count], WALK_PATH_MAX, kids->parent, name)) {
        return 1;
    }
    snprintf(kids->name[kids->count], WALK_NAME_MAX, "%s", name);
    kids->is_dir[kids->count] = is_dir;
    kids->count += 1;
    return 1;
}

static int
walk_for_install(WalkState *st, const char *dir, int depth, int selective)
{
    WalkKids kids;
    int i;
    int current_game;
    int current_pass;

    if (!st || st->hit || !dir || !dir[0] || st->budget <= 0) {
        return st ? st->hit : 0;
    }
    st->budget -= 1;
    if (game_root_in(dir, st->found, sizeof(st->found))) {
        st->hit = 1;
        return 1;
    }
    if (depth <= 0) {
        return 0;
    }

    kids.count = 0;
    snprintf(kids.parent, sizeof(kids.parent), "%s", dir);
    os_list_dir(dir, collect_walk_cb, &kids);

    current_game = is_game_folder_name(path_base(dir));
    current_pass = is_pass_through_name(path_base(dir));

    for (i = 0; i < kids.count && !st->hit && st->budget > 0; i++) {
        int next_sel;
        if (!kids.is_dir[i] || skip_walk_name(kids.name[i])) {
            continue;
        }
        if (kids.name[i][0] == '.' && !allow_hidden_name(kids.name[i])) {
            continue;
        }
        if (selective && !current_game && !current_pass && !is_bridge_name(kids.name[i])) {
            continue;
        }
        next_sel = !is_game_folder_name(kids.name[i]);
        walk_for_install(st, kids.path[i], depth - 1, next_sel);
    }
    return st->hit;
}

static void
try_root(WalkState *st, const char *dir, int depth, int selective)
{
    if (!st || st->hit || !dir || !dir[0] || !os_dir_exists(dir)) {
        return;
    }
    walk_for_install(st, dir, depth, selective);
}

static int
refresh_installed(void)
{
    WalkState st;
    char dawn[MAX_PATH];
    char home[MAX_PATH];
    char path[MAX_PATH];
    char parent[MAX_PATH];
    char root[MAX_PATH];

    if (g_dir[0] && game_root_in(g_dir, root, sizeof(root))) {
        if (os_stricmp(g_dir, root) != 0) {
            adopt_dir(root);
        }
        g_installed = 1;
        return 1;
    }

    memset(&st, 0, sizeof(st));
    st.budget = WALK_BUDGET;

    os_data_dir(dawn, sizeof(dawn));
    try_root(&st, dawn, 3, 0);
    if (!st.hit && path_parent(parent, sizeof(parent), dawn)) {
        try_root(&st, parent, 2, 1);
    }
    if (copy_env("XDG_DATA_HOME", path, sizeof(path))) {
        try_root(&st, path, 3, 1);
    }
    if (copy_env("XDG_DOCUMENTS_DIR", path, sizeof(path))) {
        try_root(&st, path, 3, 1);
    }
    if (copy_env("USERPROFILE", home, sizeof(home)) || copy_env("HOME", home, sizeof(home))) {
        try_root(&st, home, 3, 1);
        if (os_join(path, sizeof(path), home, "Documents")) {
            try_root(&st, path, 3, 1);
        }
        if (os_join(path, sizeof(path), home, "Downloads")) {
            try_root(&st, path, 2, 1);
        }
        if (os_join(path, sizeof(path), home, "Desktop")) {
            try_root(&st, path, 2, 1);
        }
        if (os_join(path, sizeof(path), home, "Games")) {
            try_root(&st, path, 3, 0);
        }
        if (os_join(path, sizeof(path), home, ".local")) {
            char share[MAX_PATH];
            if (os_join(share, sizeof(share), path, "share")) {
                try_root(&st, share, 3, 1);
            }
        }
        if (os_join(path, sizeof(path), home, ".wine")) {
            try_root(&st, path, 5, 1);
        }
        if (os_join(path, sizeof(path), home, ".steam")) {
            try_root(&st, path, 5, 1);
        }
        if (os_join(path, sizeof(path), home, ".var")) {
            try_root(&st, path, 4, 1);
        }
    }
#ifdef _WIN32
    if (copy_env("LOCALAPPDATA", path, sizeof(path))) {
        try_root(&st, path, 3, 1);
    }
    if (copy_env("ProgramFiles", path, sizeof(path))) {
        try_root(&st, path, 2, 1);
    }
    if (copy_env("ProgramFiles(x86)", path, sizeof(path))) {
        try_root(&st, path, 2, 1);
    }
    {
        char sys[16];
        DWORD bits = GetLogicalDrives();
        int i;
        sys[0] = '\0';
        copy_env("SystemDrive", sys, sizeof(sys));
        for (i = 0; i < 26 && !st.hit; i++) {
            char drive[8];
            UINT type;
            if ((bits & (1u << i)) == 0) {
                continue;
            }
            snprintf(drive, sizeof(drive), "%c:\\", (char)('A' + i));
            if (sys[0] && (drive[0] == sys[0] || drive[0] == (char)(sys[0] + 32) || drive[0] == (char)(sys[0] - 32))) {
                continue;
            }
            type = GetDriveTypeA(drive);
            if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) {
                continue;
            }
            try_root(&st, drive, 3, 1);
        }
    }
#else
    try_root(&st, "/opt", 2, 1);
    try_root(&st, "/usr/local/games", 2, 1);
    try_root(&st, "/usr/local/share", 2, 1);
    try_root(&st, "/mnt", 2, 1);
    try_root(&st, "/media", 2, 1);
#endif

    if (st.hit) {
        adopt_dir(st.found);
        g_installed = 1;
        return 1;
    }
    g_installed = 0;
    return 0;
}

static void
write_marker(const char *dir)
{
    char path[MAX_PATH];
    FILE *file;

    if (!os_join(path, sizeof(path), dir, INSTALL_MARKER)) {
        return;
    }
    file = fopen(path, "wb");
    if (!file) {
        return;
    }
    fprintf(
        file,
        "app %s\ncontent %s %s\nlanguage %s %s\n",
        INSTALL_APP,
        INSTALL_DEPOT_CONTENT,
        INSTALL_MANIFEST_CONTENT,
        INSTALL_DEPOT_LANG,
        INSTALL_MANIFEST_LANG
    );
    fclose(file);
}

static void
mark_installed(void)
{
    persist_dir();
    write_marker(g_dir);
    g_installed = game_root_ok(g_dir);
}

static void
load_install_dir(void)
{
    char dawn[MAX_PATH];
    char path[MAX_PATH];
    FILE *file;

    os_data_dir(dawn, sizeof(dawn));
    if (os_join(path, sizeof(path), dawn, "install_dir.txt")) {
        file = fopen(path, "rb");
        if (file) {
            char extra[32];
            if (fgets(g_dir, (int)sizeof(g_dir), file)) {
                size_t n = strlen(g_dir);
                while (n > 0 && (g_dir[n - 1] == '\n' || g_dir[n - 1] == '\r')) {
                    g_dir[--n] = '\0';
                }
            }
            while (fgets(extra, (int)sizeof(extra), file)) {
                size_t n = strlen(extra);
                while (n > 0 && (extra[n - 1] == '\n' || extra[n - 1] == '\r')) {
                    extra[--n] = '\0';
                }
                if (strcmp(extra, "pinned") == 0) {
                    g_dir_pinned = 1;
                }
            }
            fclose(file);
        }
    }
    if (g_dir[0] == '\0') {
        os_join(g_dir, sizeof(g_dir), dawn, "Destiny2");
    }
    os_mkdirs(g_dir);
    persist_dir();
    refresh_installed();
}

static int
find_tool(void)
{
    const char *names[] = {
        "/tools/DepotDownloader" OS_EXE_EXT,
        "/tools/DepotDownloader/DepotDownloader" OS_EXE_EXT,
        NULL
    };
    int i;
    for (i = 0; names[i]; i++) {
        snprintf(g_tool, sizeof(g_tool), "%s%s", g_root, names[i]);
        if (file_exists(g_tool)) {
            return 1;
        }
    }

#ifdef _WIN32
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (n > 0 && n < sizeof(exe)) {
        char *slash = strrchr(exe, '\\');
        if (slash) {
            char nested[MAX_PATH];
            *slash = '\0';
            if (os_join(g_tool, sizeof(g_tool), exe, "DepotDownloader.exe") && file_exists(g_tool)) {
                return 1;
            }
            if (os_join(nested, sizeof(nested), exe, "tools") &&
                os_join(g_tool, sizeof(g_tool), nested, "DepotDownloader.exe") &&
                file_exists(g_tool)) {
                return 1;
            }
        }
    }
#endif

    char local[MAX_PATH];
    os_data_dir(local, sizeof(local));
    os_join(g_tool, sizeof(g_tool), local, "DepotDownloader");
    {
        char exe[MAX_PATH];
        os_join(exe, sizeof(exe), g_tool, "DepotDownloader" OS_EXE_EXT);
        if (file_exists(exe)) {
            snprintf(g_tool, sizeof(g_tool), "%s", exe);
            return 1;
        }
    }

    g_tool[0] = '\0';
    return 0;
}

static int
send_secret(void)
{
#ifdef _WIN32
    if (!g_stdin_write || g_secret[0] == '\0') {
        return 0;
    }
#else
    if (g_stdin_write < 0 || g_secret[0] == '\0') {
        return 0;
    }
#endif

    size_t len = strlen(g_secret);
#ifdef _WIN32
    DWORD written = 0;
    if (!WriteFile(g_stdin_write, g_secret, (DWORD)len, &written, NULL)) {
        return 0;
    }
    WriteFile(g_stdin_write, "\r\n", 2, &written, NULL);
    FlushFileBuffers(g_stdin_write);
#else
    if (g_stdin_write < 0) {
        return 0;
    }
    if (write(g_stdin_write, g_secret, len) < 0) {
        return 0;
    }
    if (write(g_stdin_write, "\n", 1) < 0) {
        return 0;
    }
#endif
    clear_secret();
    g_need = INSTALL_NEED_NONE;
    set_status("Signing in to Steam...");
    return 1;
}

static void
capture_dd_user(const char *text)
{
    const char *p = strstr(text, "Logging '");
    if (!p) {
        return;
    }
    p += 9;
    const char *end = strchr(p, '\'');
    if (!end || end <= p || (int)(end - p) >= (int)sizeof(g_user)) {
        return;
    }
    char user[128];
    memcpy(user, p, (size_t)(end - p));
    user[end - p] = '\0';
    snprintf(g_user, sizeof(g_user), "%s", user);
    steam_auth_set_dd_user(user);
}

static void
depot_work_dir(char *out, int max)
{
    char dawn[MAX_PATH];
    os_data_dir(dawn, sizeof(dawn));
    os_join(out, max, dawn, "depot");
    os_mkdirs(out);
}

static int
extract_leaf(const char *text, char *out, int max)
{
    const char *best = NULL;
    size_t best_len = 0;
    const char *p;

    if (!text || !out || max < 8) {
        return 0;
    }
    for (p = text; *p; p++) {
        if (*p == '/' || *p == '\\') {
            const char *s = p + 1;
            const char *e = s;
            while (*e && *e != ' ' && *e != '"' && *e != '\'' && *e != '\r' && *e != '\n' && *e != '\t') {
                e++;
            }
            if (e > s && memchr(s, '.', (size_t)(e - s))) {
                best = s;
                best_len = (size_t)(e - s);
            }
        }
    }
    if (!best) {
        const char *tok = text;
        while (*tok) {
            while (*tok == ' ' || *tok == '\t') {
                tok++;
            }
            const char *e = tok;
            while (*e && *e != ' ' && *e != '\r' && *e != '\n') {
                e++;
            }
            if (e > tok && memchr(tok, '.', (size_t)(e - tok))) {
                const char *dot = (const char *)memchr(tok, '.', (size_t)(e - tok));
                if (dot &&
                    (strstr(dot, ".pkg") ||
                     strstr(dot, ".exe") ||
                     strstr(dot, ".dll") ||
                     strstr(dot, ".lua") ||
                     strstr(dot, ".bin") ||
                     strstr(dot, ".manifest"))) {
                    best = tok;
                    best_len = (size_t)(e - tok);
                }
            }
            tok = *e ? e + 1 : e;
        }
    }
    if (!best || best_len < 3 || best_len >= (size_t)max) {
        return 0;
    }
    if (os_strnicmp(best, "http", 4) == 0) {
        return 0;
    }
    memcpy(out, best, best_len);
    out[best_len] = '\0';
    return 1;
}

static void
status_from_work(const char *verb, const char *text)
{
    char leaf[96];

    if (extract_leaf(text, leaf, (int)sizeof(leaf))) {
        snprintf(g_status, sizeof(g_status), "%s %s", verb, leaf);
        return;
    }
    snprintf(g_status, sizeof(g_status), "%s", verb);
}

static void
scan_output(const char *text)
{
    float pct = parse_percent(text);
    if (pct >= 0.0f) {
        g_depot_progress = pct;
        g_need = INSTALL_NEED_NONE;
    }
    capture_dd_user(text);

    if (contains_ci(text, "Enter account password")) {
        g_need = INSTALL_NEED_PASSWORD;
        set_status("Enter password in the app");
        if (g_secret[0]) {
            send_secret();
        }
        return;
    }
    if (contains_ci(text, "STEAM GUARD!")) {
        if (contains_ci(text, "Mobile App")) {
            g_need = INSTALL_NEED_NONE;
            set_status("Confirm Steam Guard on your phone");
            return;
        }
        g_need = INSTALL_NEED_GUARD;
        set_status("Enter Steam Guard code in the app");
        if (g_secret[0]) {
            send_secret();
        }
        return;
    }
    if (contains_ci(text, "Logging ") && contains_ci(text, "into Steam")) {
        set_status("Opening Steam session...");
        return;
    }
    if (contains_ci(text, "validat")) {
        status_from_work("Checking", text);
        return;
    }
    if (contains_ci(text, "pre-alloc")) {
        set_status("Preparing files...");
        return;
    }
    if (contains_ci(text, "processing depot") || contains_ci(text, "depot complete")) {
        set_status(g_verify ? "Finishing file check..." : "Finishing download...");
        return;
    }
    if (contains_ci(text, "receiving objects")) {
        set_status("Downloading mission scripts...");
        return;
    }
    if (contains_ci(text, "resolving deltas")) {
        set_status("Updating mission scripts...");
        return;
    }
    if (contains_ci(text, "checking out files") || contains_ci(text, "updating files")) {
        set_status("Installing mission scripts...");
        return;
    }
    {
        char leaf[96];
        int has_leaf = extract_leaf(text, leaf, (int)sizeof(leaf));
        if (contains_ci(text, "updat") && has_leaf) {
            status_from_work("Updating", text);
            return;
        }
        if (contains_ci(text, "download") &&
            !contains_ci(text, "depotdownloader") &&
            (contains_ci(text, "file") || has_leaf || contains_ci(text, ".pkg"))) {
            status_from_work("Downloading", text);
            return;
        }
    }
    if (contains_ci(text, "retry") || contains_ci(text, "reconnect")) {
        set_status("Reconnecting...");
    }
}

static int
start_child(const char *command, const char *work, int show_window, const char *fail_text)
{
    close_child();
    g_depot_progress = 0.0f;
    g_need = INSTALL_NEED_NONE;
    g_log[0] = '\0';
    g_log_len = 0;

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdout_write = NULL;
    HANDLE stdin_read = NULL;
    if (!CreatePipe(&g_stdout_read, &stdout_write, &sa, 0)) {
        fail_job("Pipe failed");
        return 0;
    }
    if (!CreatePipe(&stdin_read, &g_stdin_write, &sa, 0)) {
        CloseHandle(stdout_write);
        fail_job("Pipe failed");
        return 0;
    }
    SetHandleInformation(g_stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(g_stdin_write, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = show_window ? SW_SHOWNORMAL : SW_HIDE;
    si.hStdOutput = stdout_write;
    si.hStdError = stdout_write;
    si.hStdInput = stdin_read;

    DWORD flags = CREATE_NEW_PROCESS_GROUP;
    if (!show_window) {
        flags |= CREATE_NO_WINDOW;
    }

    char runnable[2048];
    snprintf(runnable, sizeof(runnable), "%s", command);
    BOOL ok = CreateProcessA(
        NULL,
        runnable,
        NULL,
        NULL,
        TRUE,
        flags,
        NULL,
        work,
        &si,
        &pi
    );

    CloseHandle(stdout_write);
    CloseHandle(stdin_read);

    if (!ok) {
        close_child();
        fail_job(fail_text);
        return 0;
    }

    CloseHandle(pi.hThread);
    g_process = pi.hProcess;
#else
    int out_pipe[2];
    int in_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(in_pipe) != 0) {
        fail_job("Pipe failed");
        return 0;
    }
    g_stdout_read = out_pipe[0];
    g_stdin_write = in_pipe[1];
    fcntl(g_stdout_read, F_SETFL, O_NONBLOCK);

    pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(in_pipe[0]);
        close(in_pipe[1]);
        fail_job(fail_text);
        return 0;
    }
    if (pid == 0) {
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        dup2(in_pipe[0], STDIN_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(in_pipe[0]);
        close(in_pipe[1]);
        if (work && work[0] && chdir(work) != 0) {
            _exit(1);
        }
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    close(out_pipe[1]);
    close(in_pipe[0]);
    g_process = pid;
    (void)show_window;
#endif
    g_busy = 1;
    g_phase = INSTALL_RUNNING;
    return 1;
}

static int start_dawn(void);
static int collect_wipe_cb(const char *name, int is_dir, void *user);

static int
complete_install(void)
{
    close_child();
    g_busy = 0;
    g_need = INSTALL_NEED_NONE;
    g_phase = INSTALL_OK;
    g_depot_progress = 1.0f;
    mark_installed();
    if (game_root_ok(g_dir) && dawn_ready(g_dir)) {
        g_installed = 1;
        set_status(g_verify ? "Game files verified" : "Dawn is ready");
    } else if (game_root_ok(g_dir)) {
        g_installed = 0;
        set_status("Game files are in, Dawn is still missing");
    } else {
        g_installed = 0;
        set_status("Install is incomplete");
    }
    g_verify = 0;
    clear_secret();
    return 1;
}

static void
bind_steam_identity(void)
{
    const char *user = steam_auth_dd_user();

    if (!steam_auth_signed_in()) {
        g_session_ready = 0;
        g_session_tried = 0;
        return;
    }
    if (user && user[0] && os_stricmp(g_user, user) != 0) {
        snprintf(g_user, sizeof(g_user), "%s", user);
    }
}

static int
start_steam_session(void)
{
    char command[2048];
    char work[MAX_PATH];
    char dest[MAX_PATH];
    char list[MAX_PATH];
    FILE *file;

    if (!g_user[0] || !find_tool()) {
        return 0;
    }
    depot_work_dir(work, MAX_PATH);
    if (!os_join(dest, sizeof(dest), work, "session") ||
        !os_join(list, sizeof(list), work, "session-files.txt")) {
        return 0;
    }
    os_mkdirs(dest);
    file = fopen(list, "wb");
    if (file) {
        fclose(file);
    }
    snprintf(
        command,
        sizeof(command),
        "\"%s\" -app %s -depot %s -manifest %s -dir \"%s\" -filelist \"%s\" -username \"%s\" "
        "-remember-password -os windows -osarch 64",
        g_tool,
        INSTALL_APP,
        INSTALL_DEPOT_CONTENT,
        INSTALL_MANIFEST_CONTENT,
        dest,
        list,
        g_user
    );
    g_session_job = 1;
    g_session_tried = 1;
    if (!start_child(command, work, 0, "Could not start Steam downloader login")) {
        g_session_job = 0;
        return 0;
    }
    set_status("Connecting Steam for downloads...");
    return 1;
}

static void
maybe_prepare_session(void)
{
    if (g_busy || g_session_ready || g_session_tried) {
        return;
    }
    if (!steam_auth_signed_in()) {
        return;
    }
    bind_steam_identity();
    if (!g_user[0]) {
        return;
    }
    start_steam_session();
}

static int
start_depot(void)
{
    char command[2048];
    char work[MAX_PATH];

    g_session_job = 0;
    g_step = STEP_DEPOT_CONTENT;
    bind_steam_identity();
    if (!g_user[0]) {
        fail_job("Steam account name is missing");
        return 0;
    }

    snprintf(
        command,
        sizeof(command),
        "\"%s\" -app %s -depot %s %s -manifest %s %s -dir \"%s\" -username \"%s\" "
        "-remember-password -os windows -osarch 64 -validate -max-servers 20 -max-downloads 16",
        g_tool,
        INSTALL_APP,
        INSTALL_DEPOT_CONTENT,
        INSTALL_DEPOT_LANG,
        INSTALL_MANIFEST_CONTENT,
        INSTALL_MANIFEST_LANG,
        g_dir,
        g_user
    );

    depot_work_dir(work, MAX_PATH);
    if (!start_child(command, work, 0, "DepotDownloader failed to start")) {
        return 0;
    }
    set_status(g_verify ? "Checking Destiny 2 files..." : "Downloading Destiny 2 depots...");
    return 1;
}

static int
find_curl(char *out, size_t max)
{
#ifdef _WIN32
    char sys[MAX_PATH];
    UINT n = GetSystemDirectoryA(sys, (UINT)sizeof(sys));
    if (n > 0 && n < sizeof(sys) && os_join(out, max, sys, "curl.exe") && file_exists(out)) {
        return 1;
    }
#endif
    snprintf(out, max, "curl");
    return 1;
}

static int
run_hidden(const char *command, const char *work, DWORD timeout_ms)
{
#ifdef _WIN32
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    DWORD code = 1;
    char runnable[2048];

    snprintf(runnable, sizeof(runnable), "%s", command);
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessA(NULL, runnable, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, work, &si, &pi)) {
        return 0;
    }
    if (WaitForSingleObject(pi.hProcess, timeout_ms ? timeout_ms : 60000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 0;
    }
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
#else
    (void)timeout_ms;
    if (work && work[0]) {
        char full[4096];
        snprintf(full, sizeof(full), "cd \"%s\" && %s", work, command);
        return system(full) == 0;
    }
    return system(command) == 0;
#endif
}

static int
copy_tree(const char *from, const char *to)
{
    WalkKids kids;
    int i;
    int ok = 1;

    if (!from || !to || !os_dir_exists(from)) {
        return 0;
    }
    os_mkdirs(to);
    kids.count = 0;
    snprintf(kids.parent, sizeof(kids.parent), "%s", from);
    os_list_dir(from, collect_wipe_cb, &kids);
    for (i = 0; i < kids.count; i++) {
        char dest[WALK_PATH_MAX];
        if (!os_join(dest, sizeof(dest), to, kids.name[i])) {
            ok = 0;
            continue;
        }
        if (kids.is_dir[i]) {
            if (!copy_tree(kids.path[i], dest)) {
                ok = 0;
            }
        } else if (!os_copy_file(kids.path[i], dest)) {
            ok = 0;
        }
    }
    return ok;
}

static int
payload_has_file(const char *root, const char *a, const char *b)
{
    char mid[MAX_PATH];
    char path[MAX_PATH];

    if (!root || !a) {
        return 0;
    }
    if (!b) {
        return os_join(path, sizeof(path), root, a) && file_exists(path);
    }
    return os_join(mid, sizeof(mid), root, a) && os_join(path, sizeof(path), mid, b) && file_exists(path);
}

static int
payload_has_dir(const char *root, const char *a, const char *b)
{
    char mid[MAX_PATH];
    char path[MAX_PATH];

    if (!root || !a) {
        return 0;
    }
    if (!b) {
        return os_join(path, sizeof(path), root, a) && os_dir_exists(path);
    }
    return os_join(mid, sizeof(mid), root, a) && os_join(path, sizeof(path), mid, b) && os_dir_exists(path);
}

static int
payload_looks_good(const char *dir)
{
    char dll[MAX_PATH];

    if (!dir ||
        !payload_has_file(dir, "steam_api64.dll", NULL) ||
        !payload_has_file(dir, "Dawn", "settings.json") ||
        !payload_has_file(dir, "Dawn", "hud.json") ||
        !payload_has_file(dir, "Dawn", "movement.json") ||
        !payload_has_file(dir, "Dawn", "player.json") ||
        !payload_has_file(dir, "Dawn", "vendor_catalog.txt") ||
        !payload_has_file(dir, "Dawn", "vendor_bounty_roll.txt") ||
        !payload_has_file(dir, "Dawn", "vendor_exchange.txt") ||
        !payload_has_file(dir, "Dawn", "vendor_item_substitute.txt") ||
        !payload_has_dir(dir, "Dawn", "scripts") ||
        !payload_has_dir(dir, "Dawn", "event_presets")) {
        return 0;
    }
#ifdef _WIN32
    return os_join(dll, sizeof(dll), dir, "steam_api64.dll") && dll_product_is(dll, "Dawn");
#else
    (void)dll;
    return 1;
#endif
}

static int
find_dawn_payload(const char *root, char *out, size_t max)
{
    WalkKids kids;
    char nested[MAX_PATH];
    int i;

    if (payload_looks_good(root)) {
        snprintf(out, max, "%s", root);
        return 1;
    }
    if (os_join(nested, sizeof(nested), root, "payload") && payload_looks_good(nested)) {
        snprintf(out, max, "%s", nested);
        return 1;
    }
    kids.count = 0;
    snprintf(kids.parent, sizeof(kids.parent), "%s", root);
    os_list_dir(root, collect_wipe_cb, &kids);
    for (i = 0; i < kids.count; i++) {
        if (!kids.is_dir[i]) {
            continue;
        }
        if (payload_looks_good(kids.path[i])) {
            snprintf(out, max, "%s", kids.path[i]);
            return 1;
        }
        if (os_join(nested, sizeof(nested), kids.path[i], "payload") && payload_looks_good(nested)) {
            snprintf(out, max, "%s", nested);
            return 1;
        }
    }
    return 0;
}

static int
find_bundled_dawn(char *out, size_t max)
{
    char parent[MAX_PATH];
    char path[MAX_PATH];
    char mid[MAX_PATH];
    char cache[MAX_PATH];

    if (path_parent(parent, sizeof(parent), g_root) &&
        os_join(mid, sizeof(mid), parent, "Dawn-installer") &&
        os_join(path, sizeof(path), mid, "bundle") &&
        os_join(mid, sizeof(mid), path, "dawn-release") &&
        find_dawn_payload(mid, out, max)) {
        return 1;
    }
    if (os_join(path, sizeof(path), g_root, "tools") &&
        os_join(mid, sizeof(mid), path, "dawn-release") &&
        find_dawn_payload(mid, out, max)) {
        return 1;
    }
#ifdef _WIN32
    if (copy_env("LOCALAPPDATA", cache, sizeof(cache)) &&
        os_join(mid, sizeof(mid), cache, "DawnInstaller") &&
        os_join(path, sizeof(path), mid, "releases") &&
        os_dir_exists(path)) {
        WalkKids kids;
        int i;
        kids.count = 0;
        snprintf(kids.parent, sizeof(kids.parent), "%s", path);
        os_list_dir(path, collect_wipe_cb, &kids);
        for (i = kids.count - 1; i >= 0; i--) {
            if (kids.is_dir[i] && find_dawn_payload(kids.path[i], out, max)) {
                return 1;
            }
        }
    }
#else
    (void)cache;
#endif
    return 0;
}

static int
json_find_dawn_zip(const char *path, char *url, size_t url_max, char *tag, size_t tag_max)
{
    FILE *file;
    char *buf;
    long size;
    const char *p;
    const char *best = NULL;

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
    p = strstr(buf, "\"tag_name\"");
    if (p && tag && tag_max > 1) {
        const char *q = strchr(p, ':');
        if (q) {
            q = strchr(q, '"');
            if (q) {
                const char *end = strchr(q + 1, '"');
                size_t n = end ? (size_t)(end - (q + 1)) : 0;
                if (n > 0 && n < tag_max) {
                    memcpy(tag, q + 1, n);
                    tag[n] = '\0';
                }
            }
        }
    }
    for (p = buf; (p = strstr(p, "browser_download_url")) != NULL; p += 20) {
        const char *q = strchr(p, '"');
        q = q ? strchr(q + 1, '"') : NULL;
        q = q ? strchr(q + 1, '"') : NULL;
        if (!q) {
            continue;
        }
        {
            const char *end = strchr(q + 1, '"');
            size_t n = end ? (size_t)(end - (q + 1)) : 0;
            char found[1024];
            if (n < 12 || n >= sizeof(found)) {
                continue;
            }
            memcpy(found, q + 1, n);
            found[n] = '\0';
            if (!contains_ci(found, ".zip") || contains_ci(found, "sha256") || contains_ci(found, "source")) {
                continue;
            }
            if (contains_ci(found, "Dawn-") || contains_ci(found, "/Dawn")) {
                best = p;
                snprintf(url, url_max, "%s", found);
                break;
            }
            if (!best) {
                snprintf(url, url_max, "%s", found);
                best = p;
            }
        }
    }
    free(buf);
    return url && url[0] != '\0';
}

static int
fetch_dawn_zip_url(void)
{
    char work[MAX_PATH];
    char meta[MAX_PATH];
    char curl[MAX_PATH];
    char command[2048];

    g_dawn_zip_url[0] = '\0';
    g_dawn_tag[0] = '\0';
    depot_work_dir(work, MAX_PATH);
    if (!os_join(meta, sizeof(meta), work, "dawn-latest.json")) {
        return 0;
    }
    find_curl(curl, sizeof(curl));
    snprintf(
        command,
        sizeof(command),
        "\"%s\" -fsSL --retry 3 -A DawnLauncher/1.0 -o \"%s\" \"%s\"",
        curl,
        meta,
        DAWN_RELEASES_API
    );
    if (run_hidden(command, work, 30000) && json_find_dawn_zip(meta, g_dawn_zip_url, sizeof(g_dawn_zip_url), g_dawn_tag, sizeof(g_dawn_tag))) {
        return 1;
    }
    snprintf(g_dawn_zip_url, sizeof(g_dawn_zip_url), "%s", DAWN_FALLBACK_ZIP);
    snprintf(g_dawn_tag, sizeof(g_dawn_tag), "0.1.3");
    return 1;
}

static void
write_launch_scripts(const char *dir)
{
    char path[MAX_PATH];
    FILE *file;

#ifdef _WIN32
    if (os_join(path, sizeof(path), dir, "launch-destiny.cmd") && !file_exists(path)) {
        file = fopen(path, "wb");
        if (file) {
            fputs("@echo off\r\ncd /d \"%~dp0\"\r\nset DAWN_FOREST_BASELINE=1\r\nstart \"\" \"%~dp0destiny2.exe\" %*\r\n", file);
            fclose(file);
        }
    }
#else
    if (os_join(path, sizeof(path), dir, "launch-destiny.sh") && !file_exists(path)) {
        file = fopen(path, "wb");
        if (file) {
            fputs(
                "#!/usr/bin/env sh\n"
                "set -e\n"
                "GAME_DIR=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n"
                "cd \"$GAME_DIR\"\n"
                "export DAWN_FOREST_BASELINE=1\n"
                "if command -v steam-run >/dev/null 2>&1; then RUNNER=steam-run; else RUNNER=; fi\n"
                "for cand in \\\n"
                "  \"$HOME/.local/share/Steam/steamapps/common/Proton - Experimental/proton\" \\\n"
                "  \"$HOME/.local/share/Steam/steamapps/common/Proton 9.0/proton\" \\\n"
                "  \"$HOME/.steam/steam/steamapps/common/Proton - Experimental/proton\"; do\n"
                "  if [ -f \"$cand\" ]; then\n"
                "    STEAM_ROOT=\"$(dirname \"$(dirname \"$(dirname \"$(dirname \"$cand\")\")\")\")\"\n"
                "    export STEAM_COMPAT_CLIENT_INSTALL_PATH=\"$STEAM_ROOT\"\n"
                "    export STEAM_COMPAT_DATA_PATH=\"$STEAM_ROOT/steamapps/compatdata/1085660\"\n"
                "    mkdir -p \"$STEAM_COMPAT_DATA_PATH\"\n"
                "    if [ -n \"$RUNNER\" ]; then exec $RUNNER \"$cand\" run \"$GAME_DIR/destiny2.exe\" \"$@\"; fi\n"
                "    exec \"$cand\" run \"$GAME_DIR/destiny2.exe\" \"$@\"\n"
                "  fi\n"
                "done\n"
                "if command -v wine >/dev/null 2>&1; then\n"
                "  if [ -n \"$RUNNER\" ]; then exec $RUNNER wine \"$GAME_DIR/destiny2.exe\" \"$@\"; fi\n"
                "  exec wine \"$GAME_DIR/destiny2.exe\" \"$@\"\n"
                "fi\n"
                "echo \"[ERROR] Neither Proton nor Wine was found.\"\n"
                "exit 1\n",
                file
            );
            fclose(file);
        }
    }
#endif
}

static int
backup_stock_steam_dll(const char *dir)
{
    char backup_dir[MAX_PATH];
    char backup[MAX_PATH];
    char src[MAX_PATH];
    char mid[MAX_PATH];

    if (!os_join(mid, sizeof(mid), dir, ".dawn") || !os_join(backup_dir, sizeof(backup_dir), mid, "backup")) {
        return 0;
    }
    os_mkdirs(backup_dir);
    if (!os_join(backup, sizeof(backup), backup_dir, "steam_api64.dll")) {
        return 0;
    }
    if (file_exists(backup)) {
        return 1;
    }
    if (steam_dll_path(dir, src, sizeof(src)) && file_exists(src)) {
        return os_copy_file(src, backup);
    }
    if (os_join(src, sizeof(src), dir, "steam_api64.dll") && file_exists(src)) {
        return os_copy_file(src, backup);
    }
    return 1;
}

static void
patch_language_file(const char *settings, const char *lang)
{
    FILE *file;
    char *buf;
    long size;
    char *hit;

    if (!settings || !lang) {
        return;
    }
    file = fopen(settings, "rb");
    if (!file) {
        return;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0 || size > 2 * 1024 * 1024) {
        fclose(file);
        return;
    }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(file);
        return;
    }
    if (fread(buf, 1, (size_t)size, file) != (size_t)size) {
        free(buf);
        fclose(file);
        return;
    }
    fclose(file);
    buf[size] = '\0';
    hit = strstr(buf, "\"language\"");
    if (hit) {
        char *q = strchr(hit, ':');
        q = q ? strchr(q, '"') : NULL;
        if (q) {
            char *end = strchr(q + 1, '"');
            if (end && (size_t)(end - (q + 1)) == strlen(lang)) {
                memcpy(q + 1, lang, strlen(lang));
                file = fopen(settings, "wb");
                if (file) {
                    fwrite(buf, 1, (size_t)size, file);
                    fclose(file);
                }
            }
        }
    }
    free(buf);
}

static void
set_dawn_language(const char *dir, const char *lang)
{
    char dawn[MAX_PATH];
    char settings[MAX_PATH];
    char bin_dawn[MAX_PATH];

    if (!dir || !lang || !lang[0]) {
        return;
    }
    if (os_join(dawn, sizeof(dawn), dir, "Dawn") &&
        os_join(settings, sizeof(settings), dawn, "settings.json")) {
        patch_language_file(settings, lang);
    }
    if (join4(bin_dawn, sizeof(bin_dawn), dir, "bin", "x64", "Dawn") &&
        os_join(settings, sizeof(settings), bin_dawn, "settings.json")) {
        patch_language_file(settings, lang);
    }
}

#ifdef _WIN32
static void
apply_windowed_fullscreen(void)
{
    char appdata[MAX_PATH];
    char mid[MAX_PATH];
    char prefs[MAX_PATH];
    char path[MAX_PATH];
    char *buf;
    FILE *file;
    long size;
    char *mode;
    const char *seed =
        "<?xml version=\"1.0\"?><body><namespace name=\"graphics\"><cvar name=\"window_mode\" value=\"2\" /></namespace></body>\r\n";

    if (!copy_env("APPDATA", appdata, sizeof(appdata)) ||
        !os_join(mid, sizeof(mid), appdata, "Bungie") ||
        !os_join(prefs, sizeof(prefs), mid, "DestinyPC") ||
        !os_join(mid, sizeof(mid), prefs, "prefs") ||
        !os_join(path, sizeof(path), mid, "cvars.xml")) {
        return;
    }
    os_mkdirs(mid);
    if (!file_exists(path)) {
        file = fopen(path, "wb");
        if (file) {
            fputs(seed, file);
            fclose(file);
        }
        return;
    }
    file = fopen(path, "rb");
    if (!file) {
        return;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0 || size > 1024 * 1024) {
        fclose(file);
        return;
    }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(file);
        return;
    }
    if (fread(buf, 1, (size_t)size, file) != (size_t)size) {
        free(buf);
        fclose(file);
        return;
    }
    fclose(file);
    buf[size] = '\0';
    mode = strstr(buf, "name=\"window_mode\"");
    if (!mode) {
        mode = strstr(buf, "name='window_mode'");
    }
    if (mode) {
        char *val = strstr(mode, "value=\"");
        if (!val || val > mode + 80) {
            val = strstr(mode, "value='");
        }
        if (val && val < mode + 80) {
            char *q = val + 7;
            if (*q && *q != '"' && *q != '\'') {
                *q = '2';
            }
            file = fopen(path, "wb");
            if (file) {
                fwrite(buf, 1, (size_t)size, file);
                fclose(file);
            }
        }
    }
    free(buf);
}
#endif

static int
write_dawn_receipt(const char *dir, const char *payload)
{
    char dawn_meta[MAX_PATH];
    char path[MAX_PATH];
    char parent[MAX_PATH];
    char src[MAX_PATH];
    FILE *file;

    if (!os_join(dawn_meta, sizeof(dawn_meta), dir, ".dawn")) {
        return 0;
    }
    os_mkdirs(dawn_meta);
    if (path_parent(parent, sizeof(parent), payload) &&
        os_join(src, sizeof(src), parent, "release.json") &&
        file_exists(src)) {
        if (os_join(path, sizeof(path), dawn_meta, "release.json")) {
            os_copy_file(src, path);
        }
        if (os_join(path, sizeof(path), dir, "release.json")) {
            os_copy_file(src, path);
        }
    }
    if (os_join(path, sizeof(path), dawn_meta, "release.json") && !file_exists(path)) {
        file = fopen(path, "wb");
        if (file) {
            fprintf(
                file,
                "{\n  \"schema\": 1,\n  \"release\": \"%s\",\n  \"gameBuild\": 86657,\n  \"runtimeDirectory\": \"Dawn\",\n  \"profileMode\": \"fresh\"\n}\n",
                g_dawn_tag[0] ? g_dawn_tag : "latest"
            );
            fclose(file);
        }
        if (os_join(path, sizeof(path), dir, "release.json") && !file_exists(path)) {
            file = fopen(path, "wb");
            if (file) {
                fprintf(
                    file,
                    "{\n  \"schema\": 1,\n  \"release\": \"%s\",\n  \"gameBuild\": 86657,\n  \"runtimeDirectory\": \"Dawn\",\n  \"profileMode\": \"fresh\"\n}\n",
                    g_dawn_tag[0] ? g_dawn_tag : "latest"
                );
                fclose(file);
            }
        }
    }
    return 1;
}

static int
deploy_dawn(const char *payload)
{
    char dest_dawn[MAX_PATH];
    char dest_bin_dawn[MAX_PATH];
    char src_dll[MAX_PATH];
    char dest_dll[MAX_PATH];
    char dest_root_dll[MAX_PATH];
    char src_dawn[MAX_PATH];

    int fresh;

    if (!payload_looks_good(payload)) {
        return 0;
    }
    if (destiny2_running()) {
        fail_job("Close Destiny 2 before installing Dawn");
        return 0;
    }
    if (!game_root_ok(g_dir)) {
        fail_job("Game folder is not Destiny 2 build 86657");
        return 0;
    }
    fresh = !dawn_ready(g_dir);
    backup_stock_steam_dll(g_dir);
    if (!os_join(src_dawn, sizeof(src_dawn), payload, "Dawn") ||
        !os_join(dest_dawn, sizeof(dest_dawn), g_dir, "Dawn") ||
        !join4(dest_bin_dawn, sizeof(dest_bin_dawn), g_dir, "bin", "x64", "Dawn") ||
        !os_join(src_dll, sizeof(src_dll), payload, "steam_api64.dll") ||
        !steam_dll_path(g_dir, dest_dll, sizeof(dest_dll)) ||
        !os_join(dest_root_dll, sizeof(dest_root_dll), g_dir, "steam_api64.dll")) {
        return 0;
    }
    os_mkdirs(dest_dawn);
    os_mkdirs(dest_bin_dawn);
    if (!copy_tree(payload, g_dir) || !copy_tree(src_dawn, dest_bin_dawn)) {
        return 0;
    }
    if (!os_copy_file(src_dll, dest_dll) || !os_copy_file(src_dll, dest_root_dll)) {
        return 0;
    }
    write_dawn_receipt(g_dir, payload);
    write_launch_scripts(g_dir);
    set_dawn_language(g_dir, "english");
#ifdef _WIN32
    if (fresh) {
        apply_windowed_fullscreen();
    }
#else
    (void)fresh;
#endif
    return dawn_ready(g_dir);
}

static int
unpack_dawn_zip(void)
{
    char work[MAX_PATH];
    char zip[MAX_PATH];
    char unpack[MAX_PATH];
    char payload[MAX_PATH];
    char command[2048];

    depot_work_dir(work, MAX_PATH);
    if (!os_join(zip, sizeof(zip), work, "dawn-release.zip") || !file_exists(zip)) {
        return 0;
    }
    if (!os_join(unpack, sizeof(unpack), work, "dawn-unpack")) {
        return 0;
    }
    os_mkdirs(unpack);
    snprintf(command, sizeof(command), "tar -xf \"%s\" -C \"%s\"", zip, unpack);
    if (!run_hidden(command, work, 120000)) {
        return 0;
    }
    return find_dawn_payload(unpack, payload, sizeof(payload)) && deploy_dawn(payload);
}

static int
start_dawn_zip(void)
{
    char work[MAX_PATH];
    char zip[MAX_PATH];
    char curl[MAX_PATH];
    char command[2048];

    g_step = STEP_DAWN_RELEASE;
    if (!g_dawn_zip_url[0]) {
        snprintf(g_dawn_zip_url, sizeof(g_dawn_zip_url), "%s", DAWN_FALLBACK_ZIP);
    }
    depot_work_dir(work, MAX_PATH);
    if (!os_join(zip, sizeof(zip), work, "dawn-release.zip")) {
        fail_job("Dawn folder is invalid");
        return 0;
    }
    find_curl(curl, sizeof(curl));
    snprintf(
        command,
        sizeof(command),
        "\"%s\" -fL --retry 3 -A DawnLauncher/1.0 -o \"%s\" \"%s\"",
        curl,
        zip,
        g_dawn_zip_url
    );
    if (!start_child(command, work, 0, "Could not start Dawn download")) {
        return 0;
    }
    set_status(g_dawn_tag[0] ? "Downloading Dawn..." : "Downloading Dawn release...");
    if (g_dawn_tag[0]) {
        snprintf(g_status, sizeof(g_status), "Downloading Dawn %s...", g_dawn_tag);
    }
    return 1;
}

static int
start_dawn(void)
{
    char payload[MAX_PATH];

    g_step = STEP_DAWN_RELEASE;
    if (dawn_ready(g_dir)) {
        return complete_install();
    }
    set_status("Installing Dawn...");
    if (find_bundled_dawn(payload, sizeof(payload)) && deploy_dawn(payload)) {
        return complete_install();
    }
    if (!fetch_dawn_zip_url()) {
        fail_job("Dawn release was not found");
        return 0;
    }
    return start_dawn_zip();
}

static void
finish_child(DWORD exit_code)
{
    close_child();
    if (g_session_job) {
        g_session_job = 0;
        g_busy = 0;
        g_need = INSTALL_NEED_NONE;
        if (exit_code == 0) {
            g_session_ready = 1;
            set_status("Steam is ready to download");
        } else {
            set_status("Steam downloader will sign in when you download");
        }
        return;
    }
    if (exit_code != 0) {
        if (g_step == STEP_DEPOT_CONTENT &&
            (contains_ci(g_log, "AccessDenied") || contains_ci(g_log, "401"))) {
            fail_job("Steam denied depot access");
            return;
        }
        if (g_step == STEP_DAWN_RELEASE) {
            char payload[MAX_PATH];
            if (find_bundled_dawn(payload, sizeof(payload)) && deploy_dawn(payload)) {
                complete_install();
                return;
            }
            fail_job("Dawn download failed");
            return;
        }
        fail_job("Download failed");
        return;
    }

    g_depot_progress = 1.0f;
    if (g_step == STEP_DEPOT_CONTENT) {
        start_dawn();
        return;
    }
    if (g_step == STEP_DAWN_RELEASE) {
        if (!unpack_dawn_zip() && !dawn_ready(g_dir)) {
            char payload[MAX_PATH];
            if (!find_bundled_dawn(payload, sizeof(payload)) || !deploy_dawn(payload)) {
                fail_job("Could not install Dawn onto the game folder");
                return;
            }
        }
        complete_install();
        return;
    }
    start_dawn();
}

void
install_job_init(const char *project_root)
{
    memset(g_root, 0, sizeof(g_root));
    memset(g_dir, 0, sizeof(g_dir));
    memset(g_user, 0, sizeof(g_user));
    memset(g_tool, 0, sizeof(g_tool));
    clear_secret();
    g_status[0] = '\0';
    g_log[0] = '\0';
    g_log_len = 0;
    g_phase = INSTALL_IDLE;
    g_need = INSTALL_NEED_NONE;
    g_step = 0;
    g_verify = 0;
    g_scan_check = 0;
    g_session_job = 0;
    g_session_ready = 0;
    g_session_tried = 0;
    g_busy = 0;
    g_depot_progress = 0.0f;
    g_installed = 0;
#ifdef _WIN32
    g_process = NULL;
    g_stdout_read = NULL;
    g_stdin_write = NULL;
#else
    g_process = 0;
    g_stdout_read = -1;
    g_stdin_write = -1;
#endif
    if (project_root && project_root[0]) {
        snprintf(g_root, sizeof(g_root), "%s", project_root);
    }
    g_dawn_zip_url[0] = '\0';
    g_dawn_tag[0] = '\0';
    load_install_dir();
    if (dawn_ready(g_dir)) {
        set_status("Dawn is ready");
    } else if (g_installed) {
        set_status("Ready to install Dawn");
    } else {
        set_status("Ready");
    }
}

void
install_job_shutdown(void)
{
    install_job_cancel();
    clear_secret();
}

void
install_job_set_dir(const char *dir)
{
    char root[MAX_PATH];
    size_t n;

    if (g_busy) {
        set_status("Folder is locked while downloading");
        return;
    }
    if (!dir || !dir[0]) {
        return;
    }
    snprintf(g_dir, sizeof(g_dir), "%s", dir);
    n = strlen(g_dir);
    while (n > 3 && (g_dir[n - 1] == '/' || g_dir[n - 1] == '\\')) {
        g_dir[--n] = '\0';
    }
    g_dir_pinned = 1;
    os_mkdirs(g_dir);
    persist_dir();
    g_installed_check = 0;
    g_scan_check = 0;
    if (is_live_latest_d2(g_dir)) {
        g_installed = 0;
        set_status("This folder is a live Destiny 2 install, not the 86657 files Dawn needs");
        return;
    }
    if (game_root_in(g_dir, root, sizeof(root))) {
        if (os_stricmp(g_dir, root) != 0) {
            snprintf(g_dir, sizeof(g_dir), "%s", root);
            persist_dir();
        }
        g_installed = dawn_ready(g_dir);
        set_status(g_installed ? "Dawn is ready" : "Game files found, Dawn is not installed yet");
    } else {
        g_installed = 0;
        set_status("Download folder updated");
    }
}

const char *
install_job_dir(void)
{
    return g_dir;
}

void
install_job_set_user(const char *username)
{
    if (!username) {
        g_user[0] = '\0';
        return;
    }
    snprintf(g_user, sizeof(g_user), "%s", username);
}

void
install_job_submit_secret(const char *text)
{
    if (!text) {
        return;
    }
    snprintf(g_secret, sizeof(g_secret), "%s", text);
    if (g_busy && g_need != INSTALL_NEED_NONE) {
        send_secret();
    }
}

int
install_job_start(void)
{
    if (g_busy) {
        return 0;
    }
    bind_steam_identity();
    if (g_dir[0] && is_live_latest_d2(g_dir)) {
        set_status("This folder is a live Destiny 2 install, not the 86657 files Dawn needs");
        g_phase = INSTALL_FAILED;
        g_installed = 0;
        return 0;
    }
    if (g_dir[0] && game_root_ok(g_dir) && dawn_ready(g_dir)) {
        g_phase = INSTALL_OK;
        g_installed = 1;
        set_status("Dawn is already installed");
        return 1;
    }
    if (g_dir[0] && game_root_in(g_dir, NULL, 0)) {
        return start_dawn();
    }
    if (!steam_auth_signed_in()) {
        set_status("Sign in with Steam first");
        g_phase = INSTALL_FAILED;
        return 0;
    }
    if (steam_auth_owns_d2() == 0) {
        set_status("Destiny 2 is not on this Steam account");
        g_phase = INSTALL_FAILED;
        return 0;
    }
    bind_steam_identity();
    if (g_user[0] == '\0') {
        set_status("Steam account name is missing");
        g_phase = INSTALL_FAILED;
        return 0;
    }
    if (g_dir[0] == '\0') {
        set_status("Install folder is missing");
        g_phase = INSTALL_FAILED;
        return 0;
    }
    if (!find_tool()) {
        set_status("DepotDownloader is missing in tools/");
        g_phase = INSTALL_FAILED;
        return 0;
    }

    g_phase = INSTALL_RUNNING;
    return start_depot();
}

void
install_job_cancel(void)
{
#ifdef _WIN32
    if (g_process) {
        TerminateProcess(g_process, 1);
    }
#else
    if (g_process > 0) {
        kill(g_process, SIGTERM);
        waitpid(g_process, NULL, 0);
    }
#endif
    close_child();
    g_busy = 0;
    g_session_job = 0;
    g_verify = 0;
    g_need = INSTALL_NEED_NONE;
    if (g_phase == INSTALL_RUNNING) {
        g_phase = INSTALL_IDLE;
        set_status("Cancelled");
    }
    clear_secret();
}

void
install_job_poll(void)
{
    bind_steam_identity();
#ifdef _WIN32
    if (!g_busy || !g_process) {
        maybe_prepare_session();
        return;
    }
#else
    if (!g_busy || g_process <= 0) {
        maybe_prepare_session();
        return;
    }
#endif

    char chunk[1024];
#ifdef _WIN32
    DWORD available = 0;
    if (g_stdout_read && PeekNamedPipe(g_stdout_read, NULL, 0, NULL, &available, NULL) && available > 0) {
        DWORD read_bytes = 0;
        if (available > sizeof(chunk) - 1) {
            available = sizeof(chunk) - 1;
        }
        if (ReadFile(g_stdout_read, chunk, available, &read_bytes, NULL) && read_bytes > 0) {
            chunk[read_bytes] = '\0';
            append_log(chunk, read_bytes);
            scan_output(chunk);
        }
    }

    if (WaitForSingleObject(g_process, 0) == WAIT_TIMEOUT) {
        return;
    }

    if (g_stdout_read && PeekNamedPipe(g_stdout_read, NULL, 0, NULL, &available, NULL) && available > 0) {
        DWORD read_bytes = 0;
        if (available > sizeof(chunk) - 1) {
            available = sizeof(chunk) - 1;
        }
        if (ReadFile(g_stdout_read, chunk, available, &read_bytes, NULL) && read_bytes > 0) {
            chunk[read_bytes] = '\0';
            append_log(chunk, read_bytes);
            scan_output(chunk);
        }
    }

    DWORD exit_code = 1;
    GetExitCodeProcess(g_process, &exit_code);
    finish_child(exit_code);
#else
    ssize_t n = read(g_stdout_read, chunk, sizeof(chunk) - 1);
    if (n > 0) {
        chunk[n] = '\0';
        append_log(chunk, (DWORD)n);
        scan_output(chunk);
    }
    int status = 0;
    pid_t done = waitpid(g_process, &status, WNOHANG);
    if (done == 0) {
        return;
    }
    DWORD exit_code = 1;
    if (done > 0 && WIFEXITED(status)) {
        exit_code = (DWORD)WEXITSTATUS(status);
    }
    finish_child(exit_code);
#endif
}

int
install_job_busy(void)
{
    return g_busy;
}

int
install_job_need(void)
{
    return (int)g_need;
}

float
install_job_progress(void)
{
    float local = g_depot_progress;
    if (local < 0.0f) {
        local = 0.0f;
    }
    if (local > 1.0f) {
        local = 1.0f;
    }
    return ((float)g_step + local) / 2.0f;
}

const char *
install_job_status(void)
{
    return g_status;
}

int
install_job_ready(void)
{
    char root[MAX_PATH];
    uint32_t now;

    if (g_busy) {
        return 0;
    }
    now = os_tick_ms();
    if (g_installed_check != 0 && (now - g_installed_check) < 2500u) {
        return g_installed;
    }
    g_installed_check = now;
    if (g_dir[0] && game_root_in(g_dir, root, sizeof(root))) {
        if (os_stricmp(g_dir, root) != 0) {
            snprintf(g_dir, sizeof(g_dir), "%s", root);
            persist_dir();
        }
        g_installed = dawn_ready(g_dir);
        return g_installed;
    }
    if (g_scan_check != 0 && (now - g_scan_check) < 15000u) {
        g_installed = 0;
        return 0;
    }
    g_scan_check = now;
    if (refresh_installed() && game_root_in(g_dir, NULL, 0) && dawn_ready(g_dir)) {
        g_installed = 1;
        return 1;
    }
    g_installed = 0;
    return 0;
}

int
install_job_launch(void)
{
    char exe[MAX_PATH];
    char cmd[MAX_PATH];

#ifdef _WIN32
    if (os_join(cmd, sizeof(cmd), g_dir, "launch-destiny.cmd") && file_exists(cmd)) {
        if (!os_launch(cmd)) {
            set_status("Could not start Dawn");
            return 0;
        }
        set_status("Launching Dawn");
        return 1;
    }
#else
    if (os_join(cmd, sizeof(cmd), g_dir, "launch-destiny.sh") && file_exists(cmd)) {
        if (!os_launch(cmd)) {
            set_status("Could not start Dawn");
            return 0;
        }
        set_status("Launching Dawn");
        return 1;
    }
#endif
    if (!find_game_exe(exe, sizeof(exe))) {
        if (!g_installed) {
            set_status("Dawn is not installed yet");
            return 0;
        }
        snprintf(exe, sizeof(exe), "%s", g_dir);
    }
    if (!os_launch(exe)) {
        set_status("Could not start Dawn");
        return 0;
    }
    set_status("Launching Dawn");
    return 1;
}

static int
collect_wipe_cb(const char *name, int is_dir, void *user)
{
    WalkKids *kids = (WalkKids *)user;

    if (!name || kids->count >= WALK_CHILD_MAX) {
        return 0;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 1;
    }
    if (!os_join(kids->path[kids->count], WALK_PATH_MAX, kids->parent, name)) {
        return 1;
    }
    snprintf(kids->name[kids->count], WALK_NAME_MAX, "%s", name);
    kids->is_dir[kids->count] = is_dir;
    kids->count += 1;
    return 1;
}

static int
wipe_tree(const char *dir)
{
    WalkKids kids;
    int i;
    int ok = 1;

    if (!dir || !dir[0] || !os_dir_exists(dir)) {
        return 0;
    }
    for (;;) {
        kids.count = 0;
        snprintf(kids.parent, sizeof(kids.parent), "%s", dir);
        os_list_dir(dir, collect_wipe_cb, &kids);
        if (kids.count == 0) {
            break;
        }
        for (i = 0; i < kids.count; i++) {
            if (kids.is_dir[i]) {
                if (!wipe_tree(kids.path[i])) {
                    ok = 0;
                }
            } else if (!os_delete_file(kids.path[i])) {
                ok = 0;
            }
        }
        if (kids.count < WALK_CHILD_MAX) {
            break;
        }
    }
#ifdef _WIN32
    if (!RemoveDirectoryA(dir)) {
        ok = 0;
    }
#else
    if (rmdir(dir) != 0) {
        ok = 0;
    }
#endif
    return ok;
}

int
install_job_uninstall(void)
{
    size_t n;

    if (g_busy) {
        set_status("Cannot uninstall while busy");
        return 0;
    }
    if (!g_dir[0] || !os_dir_exists(g_dir)) {
        set_status("Nothing to uninstall");
        return 0;
    }
    n = strlen(g_dir);
    if (n < 6) {
        set_status("Install folder looks unsafe");
        return 0;
    }
    {
        const char *base = path_base(g_dir);
        if (os_stricmp(base, "Documents") == 0 ||
            os_stricmp(base, "Desktop") == 0 ||
            os_stricmp(base, "Downloads") == 0 ||
            os_stricmp(base, "Users") == 0 ||
            os_stricmp(base, "home") == 0 ||
            os_stricmp(base, "AppData") == 0) {
            set_status("Install folder looks unsafe");
            return 0;
        }
    }
    if (looks_like_steam_common(g_dir) && !marker_matches(g_dir) && !dawn_depots_present(g_dir)) {
        set_status("Refusing to delete a live Steam install");
        return 0;
    }
    if (!game_root_in(g_dir, NULL, 0) &&
        !dawn_ready(g_dir) &&
        !steam_dll_present(g_dir)) {
        set_status("No game files in this folder");
        return 0;
    }
    wipe_tree(g_dir);
    os_mkdirs(g_dir);
    g_installed = 0;
    g_installed_check = 0;
    g_scan_check = 0;
    persist_dir();
    set_status("Install folder was removed");
    return 1;
}

int
install_job_verify(void)
{
    if (g_busy) {
        return 0;
    }
    if (g_dir[0] == '\0') {
        set_status("Install folder is missing");
        return 0;
    }
    if (is_live_latest_d2(g_dir)) {
        set_status("This folder is a live Destiny 2 install, not the 86657 files Dawn needs");
        return 0;
    }
    if (!find_tool()) {
        set_status("DepotDownloader is missing in tools/");
        return 0;
    }
    bind_steam_identity();
    if (!steam_auth_signed_in()) {
        set_status("Sign in with Steam first");
        return 0;
    }
    if (g_user[0] == '\0') {
        set_status("Steam account name is missing");
        return 0;
    }
    g_verify = 1;
    g_phase = INSTALL_RUNNING;
    return start_depot();
}
