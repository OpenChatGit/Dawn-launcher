#include "install_job.h"
#include "config.h"
#include "steam_auth.h"
#include "shared/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef APP_HAVE_ZLIB
#include <zlib.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
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

typedef struct LangSpec {
    const char *steam;
    const char *label;
    const char *depot;
    const char *manifest;
} LangSpec;

static const LangSpec k_langs[] = {
    { "english", "English", "1085662", "2210332166360342287" },
    { "french", "French", "1085663", "2934940253687559290" },
    { "german", "German", "1085664", "2207989571290186153" },
    { "italian", "Italian", "1085665", "6668232053215128229" },
    { "japanese", "Japanese", "1085666", "7430022397683116838" },
    { "brazilian", "Portuguese (Brazil)", "1085667", "9037238175838085860" },
    { "spanish", "Spanish", "1085668", "3424833900894552134" },
    { "russian", "Russian", "1085669", "4539277942371480381" },
    { "polish", "Polish", "1085670", "6407581507105256731" },
    { "schinese", "Chinese (Simplified)", "1085671", "4397663774546719308" },
    { "tchinese", "Chinese (Traditional)", "1085672", "3906738704604711877" },
    { "latam", "Spanish (Latam)", "1085673", "4773170998099699561" },
    { "koreana", "Korean", "1085674", "7148196199569436690" }
};

#define LANG_COUNT ((int)(sizeof(k_langs) / sizeof(k_langs[0])))
#define DAWN_WORK_NONE 0
#define DAWN_WORK_START 1
#define DAWN_WORK_FIND 2
#define DAWN_WORK_EXTRACT 3
#define DAWN_WORK_DEPLOY 4
#define DAWN_WORK_FINISH 5
#define REMOVE_NONE 0
#define REMOVE_DAWN 1
#define REMOVE_SUNRISE 2
#define REMOVE_FULL 3

static const LangSpec *g_lang;
#define INSTALL_MARKER ".dawn-ready"
#define GAME_EXE_VERSION "86657.20.08.23"
#define GAME_EXE_MAJOR 21122
#define GAME_EXE_SIZE 122984224ull
#define MIN_PACKAGE_FILES 1500
#define DAWN_RELEASES_API "https://api.github.com/repos/isinternets/Dawn/releases/latest"
#define DAWN_RELEASES_HTML "https://github.com/isinternets/Dawn/releases"
#define DAWN_RELEASES_LATEST_HTML "https://github.com/isinternets/Dawn/releases/latest"
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
static char g_exe[MAX_PATH];
static char g_user[128];
static char g_tool[MAX_PATH];
static char g_secret[256];
static char g_status[256];
static uint32_t g_status_ms;
static char g_log[4096];
static size_t g_log_len;
static InstallPhase g_phase;
static InstallNeed g_need;
static int g_step;
static int g_verify;
static uint32_t g_scan_check;
static char g_dawn_zip_url[1024];
static char g_dawn_tag[64];
static char g_dawn_installed[64];
static char g_dawn_latest[64];
static char g_dawn_latest_json[MAX_PATH];
static uint32_t g_dawn_latest_ms;
static int g_dawn_installed_dirty;
static int g_dawn_ver_pending;
#ifdef _WIN32
static HANDLE g_dawn_ver_proc;
#else
static pid_t g_dawn_ver_proc;
#endif
static int g_busy;
static float g_depot_progress;
static int g_installed;
static uint32_t g_installed_check;
static int g_dir_pinned;
static uint32_t g_ready_gen;
static char g_filelist[MAX_PATH];
static int g_session_job;
static int g_session_ready;
static int g_session_tried;
static int g_user_start;
static int g_setup_creds;
static int g_cancel;
static int g_paused;
static int g_sim;
static int g_lang_only;
static int g_launch_after;
static int g_dawn_next;
static int g_remove_next;
static char g_dawn_payload[MAX_PATH];
#ifdef _WIN32
static HANDLE g_process;
static HANDLE g_stdout_read;
static HANDLE g_stdin_write;
static HANDLE g_game_proc;
static DWORD g_game_pid;
#else
static pid_t g_process;
static int g_stdout_read;
static int g_stdin_write;
static pid_t g_game_pid;
#endif
static uint32_t g_game_launch_ms;
static int g_game_state;
static int g_d2_running;
static uint32_t g_d2_running_ms;
static int g_d2_window;
static uint32_t g_d2_window_ms;

static const LangSpec *game_language(void);

static const LangSpec *
lang_by_steam(const char *steam)
{
    int i;

    if (!steam || !steam[0]) {
        return &k_langs[0];
    }
    for (i = 0; i < LANG_COUNT; i++) {
        if (os_stricmp(k_langs[i].steam, steam) == 0) {
            return &k_langs[i];
        }
    }
    return &k_langs[0];
}

static void
persist_lang(void)
{
    char dawn[MAX_PATH];
    char path[MAX_PATH];
    FILE *file;

    os_data_dir(dawn, sizeof(dawn));
    if (!os_join(path, sizeof(path), dawn, "install_lang.txt")) {
        return;
    }
    file = fopen(path, "wb");
    if (!file) {
        return;
    }
    fputs(game_language()->steam, file);
    fclose(file);
}

static void
load_lang(void)
{
    char dawn[MAX_PATH];
    char path[MAX_PATH];
    char line[64];
    FILE *file;
    size_t n;

    os_data_dir(dawn, sizeof(dawn));
    if (!os_join(path, sizeof(path), dawn, "install_lang.txt")) {
        return;
    }
    file = fopen(path, "rb");
    if (!file) {
        return;
    }
    if (fgets(line, (int)sizeof(line), file)) {
        n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        if (line[0]) {
            g_lang = lang_by_steam(line);
        }
    }
    fclose(file);
}

static const LangSpec *
game_language(void)
{
    if (g_lang) {
        return g_lang;
    }
    load_lang();
    if (g_lang) {
        return g_lang;
    }
    g_lang = &k_langs[0];
    return g_lang;
}

static const char *
lang_depot(void)
{
    return game_language()->depot;
}

static const char *
lang_manifest(void)
{
    return game_language()->manifest;
}

static const char *
lang_steam(void)
{
    return game_language()->steam;
}

static int
status_holds(const char *text)
{
    return text &&
        text[0] &&
        (strcmp(text, "Folder removed") == 0 ||
         strcmp(text, "Uninstalled Dawn") == 0 ||
         strcmp(text, "Uninstalled Sunrise") == 0 ||
         strcmp(text, "Removing Dawn Mod") == 0 ||
         strcmp(text, "Removing Sunrise Mod") == 0 ||
         strcmp(text, "Ready to install Dawn") == 0 ||
         strcmp(text, "Could not remove Dawn") == 0 ||
         strcmp(text, "Could not remove Sunrise") == 0 ||
         strcmp(text, "Nothing to uninstall") == 0 ||
         strcmp(text, "Busy, can't uninstall") == 0 ||
         strcmp(text, "Close Destiny 2 first") == 0 ||
         strcmp(text, "Folder looks unsafe") == 0 ||
         strcmp(text, "Won't delete Steam install") == 0 ||
         strcmp(text, "No game files here") == 0 ||
         strcmp(text, "Cancelled") == 0 ||
         strcmp(text, "Paused") == 0 ||
         strcmp(text, "Stopped") == 0 ||
         strcmp(text, "Running") == 0 ||
         strcmp(text, "Files verified") == 0 ||
         strcmp(text, "Already installed") == 0 ||
         strcmp(text, "Folder set") == 0 ||
         strcmp(text, "Dawn failed to start") == 0);
}

static void
set_status(const char *text)
{
    snprintf(g_status, sizeof(g_status), "%s", text ? text : "");
    g_status_ms = os_tick_ms();
}

static void
expire_hold_status(void)
{
    uint32_t now;

    if (g_paused || g_busy || g_dawn_next != DAWN_WORK_NONE || g_remove_next || !status_holds(g_status)) {
        return;
    }
    now = os_tick_ms();
    if (g_status_ms == 0 || (now - g_status_ms) < 3500u) {
        return;
    }
    if (strcmp(g_status, "Files verified") == 0 ||
        strcmp(g_status, "Already installed") == 0) {
        snprintf(g_status, sizeof(g_status), "%s", g_installed ? "Dawn is ready" : "Ready");
    } else if (strcmp(g_status, "Uninstalled Dawn") == 0 ||
               strcmp(g_status, "Uninstalled Sunrise") == 0 ||
               strcmp(g_status, "Ready to install Dawn") == 0) {
        g_status_ms = 0;
        return;
    } else {
        g_status[0] = '\0';
    }
    g_status_ms = 0;
}

static void
clear_secret(void)
{
    SecureZeroMemory(g_secret, sizeof(g_secret));
}

static void forget_depot_password(void);

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

#ifdef _WIN32
static void
kill_pid_tree(DWORD pid)
{
    HANDLE snap;
    PROCESSENTRY32 pe;
    HANDLE proc;

    if (!pid || pid == GetCurrentProcessId()) {
        return;
    }
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        pe.dwSize = sizeof(pe);
        if (Process32First(snap, &pe)) {
            do {
                if (pe.th32ParentProcessID == pid) {
                    kill_pid_tree(pe.th32ProcessID);
                }
            } while (Process32Next(snap, &pe));
        }
        CloseHandle(snap);
    }
    proc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (proc) {
        TerminateProcess(proc, 1);
        CloseHandle(proc);
    }
}
#endif

static void
stop_child_tree(void)
{
#ifdef _WIN32
    if (g_process) {
        DWORD pid = GetProcessId(g_process);
        if (pid) {
            kill_pid_tree(pid);
        }
        TerminateProcess(g_process, 1);
        WaitForSingleObject(g_process, 1200);
    }
#else
    if (g_process > 0) {
        kill(g_process, SIGTERM);
        kill(-g_process, SIGTERM);
        usleep(80000);
        kill(g_process, SIGKILL);
        kill(-g_process, SIGKILL);
        waitpid(g_process, NULL, 0);
    }
#endif
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
    g_paused = 0;
    g_sim = 0;
    g_dawn_next = DAWN_WORK_NONE;
    g_verify = 0;
    g_need = INSTALL_NEED_NONE;
    g_user_start = 0;
    g_setup_creds = 0;
    g_phase = INSTALL_FAILED;
    set_status(text);
    forget_depot_password();
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

static void
persist_exe(void)
{
    char dawn[MAX_PATH];
    char path[MAX_PATH];
    FILE *file;

    os_data_dir(dawn, sizeof(dawn));
    if (!os_join(path, sizeof(path), dawn, "game_exe.txt")) {
        return;
    }
    if (!g_exe[0]) {
        os_delete_file(path);
        return;
    }
    file = fopen(path, "wb");
    if (!file) {
        return;
    }
    fputs(g_exe, file);
    fclose(file);
}

static void persist_lang(void);
static void depot_work_dir(char *out, int max);
static void normalize_install_dir(void);
static void write_marker(const char *dir);
static void forget_depot_password(void);
static int collect_wipe_cb(const char *name, int is_dir, void *user);

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
count_files_cb(const char *name, int is_dir, void *user)
{
    int *count = (int *)user;

    if (!is_dir && name && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
        *count += 1;
        if (*count >= MIN_PACKAGE_FILES) {
            return 0;
        }
    }
    return 1;
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
packages_count(const char *dir)
{
    char path[MAX_PATH];
    int count = 0;

    if (!find_named(dir, "packages", 1, path, sizeof(path))) {
        return 0;
    }
    os_list_dir(path, count_files_cb, &count);
    return count;
}

static int
packages_ready(const char *dir)
{
    return packages_count(dir) >= MIN_PACKAGE_FILES;
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
        file_contains(path, lang_manifest());
}

static int
marker_matches(const char *dir)
{
    return marker_file_ok(dir, INSTALL_MARKER);
}

/*
 * DepotDownloader records every depot it finished in
 * <dir>/.DepotDownloader/depot.config: a raw-deflate stream wrapping a
 * protobuf-net Dictionary<uint depot, ulong manifest> (repeated field 1,
 * each entry = { 1: depot varint, 2: manifest varint }). It is only written
 * after a depot completed, so it is the ground truth for "downloaded" and
 * is not fooled by pre-allocated zero-filled files.
 */
#define DEPOT_CFG_MAX 64

typedef struct DepotConfig {
    int count;
    uint32_t depot[DEPOT_CFG_MAX];
    uint64_t manifest[DEPOT_CFG_MAX];
} DepotConfig;

static int
pb_varint(const unsigned char **p, const unsigned char *end, uint64_t *out)
{
    uint64_t value = 0;
    int shift = 0;

    while (*p < end && shift < 64) {
        unsigned char b = *(*p)++;
        value |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) {
            *out = value;
            return 1;
        }
        shift += 7;
    }
    return 0;
}

static int
pb_skip(const unsigned char **p, const unsigned char *end, unsigned wire)
{
    uint64_t n;

    switch (wire) {
    case 0:
        return pb_varint(p, end, &n);
    case 1:
        if ((size_t)(end - *p) < 8) {
            return 0;
        }
        *p += 8;
        return 1;
    case 2:
        if (!pb_varint(p, end, &n) || n > (uint64_t)(end - *p)) {
            return 0;
        }
        *p += (size_t)n;
        return 1;
    case 5:
        if ((size_t)(end - *p) < 4) {
            return 0;
        }
        *p += 4;
        return 1;
    default:
        return 0;
    }
}

static int
parse_depot_config(const unsigned char *data, size_t len, DepotConfig *cfg)
{
    const unsigned char *p = data;
    const unsigned char *end = data + len;

    cfg->count = 0;
    while (p < end) {
        uint64_t tag;
        unsigned field;
        unsigned wire;

        if (!pb_varint(&p, end, &tag)) {
            return 0;
        }
        field = (unsigned)(tag >> 3);
        wire = (unsigned)(tag & 7u);
        if (field == 1 && wire == 2) {
            uint64_t n;
            const unsigned char *q;
            const unsigned char *qend;
            uint64_t key = 0;
            uint64_t val = 0;

            if (!pb_varint(&p, end, &n) || n > (uint64_t)(end - p)) {
                return 0;
            }
            q = p;
            qend = p + (size_t)n;
            while (q < qend) {
                uint64_t t2;
                if (!pb_varint(&q, qend, &t2)) {
                    return 0;
                }
                if ((t2 & 7u) == 0) {
                    uint64_t v;
                    if (!pb_varint(&q, qend, &v)) {
                        return 0;
                    }
                    if ((t2 >> 3) == 1) {
                        key = v;
                    } else if ((t2 >> 3) == 2) {
                        val = v;
                    }
                } else if (!pb_skip(&q, qend, (unsigned)(t2 & 7u))) {
                    return 0;
                }
            }
            if (cfg->count < DEPOT_CFG_MAX) {
                cfg->depot[cfg->count] = (uint32_t)key;
                cfg->manifest[cfg->count] = val;
                cfg->count += 1;
            }
            p = qend;
        } else if (!pb_skip(&p, end, wire)) {
            return 0;
        }
    }
    return 1;
}

static int
inflate_raw(const unsigned char *in, size_t in_len, unsigned char *out, size_t out_max, size_t *out_len)
{
#ifdef APP_HAVE_ZLIB
    z_stream s;
    int rc;

    memset(&s, 0, sizeof(s));
    if (inflateInit2(&s, -15) != Z_OK) {
        return 0;
    }
    s.next_in = (Bytef *)in;
    s.avail_in = (uInt)in_len;
    s.next_out = out;
    s.avail_out = (uInt)out_max;
    rc = inflate(&s, Z_FINISH);
    *out_len = out_max - s.avail_out;
    inflateEnd(&s);
    return rc == Z_STREAM_END;
#else
    (void)in;
    (void)in_len;
    (void)out;
    (void)out_max;
    *out_len = 0;
    return 0;
#endif
}

/* 1 = parsed, 0 = file missing/unreadable, -1 = present but undecodable */
static int
load_depot_config(const char *dir, DepotConfig *cfg)
{
    char trace[MAX_PATH];
    char path[MAX_PATH];
    unsigned char raw[8192];
    unsigned char plain[16384];
    size_t raw_len;
    size_t plain_len = 0;
    FILE *file;

    cfg->count = 0;
    if (!dir || !dir[0] ||
        !os_join(trace, sizeof(trace), dir, ".DepotDownloader") ||
        !os_join(path, sizeof(path), trace, "depot.config")) {
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    raw_len = fread(raw, 1, sizeof(raw), file);
    fclose(file);
    if (raw_len == 0 || raw_len >= sizeof(raw)) {
        return raw_len == 0 ? 0 : -1;
    }
    if (inflate_raw(raw, raw_len, plain, sizeof(plain), &plain_len) &&
        parse_depot_config(plain, plain_len, cfg)) {
        return 1;
    }
    /* tolerate an uncompressed store as well */
    if (parse_depot_config(raw, raw_len, cfg)) {
        return 1;
    }
    cfg->count = 0;
    return -1;
}

/* 1 = depot finished with that manifest, 0 = not finished, -1 = unknown (no depot.config) */
static int
depot_config_has(const char *dir, const char *depot, const char *manifest)
{
    DepotConfig cfg;
    uint32_t want_depot;
    uint64_t want_manifest;
    int rc;
    int i;

    if (!depot || !manifest) {
        return -1;
    }
    rc = load_depot_config(dir, &cfg);
    if (rc <= 0) {
        return rc == 0 ? -1 : 0;
    }
    want_depot = (uint32_t)strtoul(depot, NULL, 10);
    want_manifest = strtoull(manifest, NULL, 10);
    for (i = 0; i < cfg.count; i++) {
        if (cfg.depot[i] == want_depot) {
            return cfg.manifest[i] == want_manifest;
        }
    }
    return 0;
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
    static char cached_dir[MAX_PATH];
    static uint32_t cached_ms;
    static uint32_t cached_gen;
    static int cached_ok;
    char path[MAX_PATH];
    uint64_t size;
    uint32_t now;

    if (!dir || !dir[0]) {
        return 0;
    }
    now = os_tick_ms();
    if (cached_ms &&
        cached_gen == g_ready_gen &&
        (now - cached_ms) < 4000u &&
        os_stricmp(cached_dir, dir) == 0) {
        return cached_ok;
    }
    cached_gen = g_ready_gen;
    snprintf(cached_dir, sizeof(cached_dir), "%s", dir);
    cached_ms = now;
    if (!dawn_dll_path(dir, path, sizeof(path))) {
        cached_ok = 0;
        return 0;
    }
#ifdef _WIN32
    (void)size;
    cached_ok = dll_product_is(path, "Dawn");
#else
    size = os_file_size(path);
    cached_ok = size > 8ull * 1024ull * 1024ull && size < 50ull * 1024ull * 1024ull;
#endif
    return cached_ok;
}

static int
dawn_ready(const char *dir)
{
    char settings[MAX_PATH];

    return dir && dir[0] && dawn_settings_in(dir, settings, sizeof(settings)) && dawn_dll_ready(dir);
}

static void
invalidate_game_scan(void)
{
    g_d2_running_ms = 0;
    g_d2_window_ms = 0;
}

static int
destiny2_running(void)
{
    uint32_t now = os_tick_ms();
    uint32_t ttl = g_game_launch_ms ? 120u : 400u;

    if (g_d2_running_ms && (now - g_d2_running_ms) < ttl) {
        return g_d2_running;
    }
#ifdef _WIN32
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe;
        int found = 0;

        if (snap == INVALID_HANDLE_VALUE) {
            return g_d2_running;
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
        g_d2_running = found;
    }
#else
    {
        DIR *proc = opendir("/proc");
        struct dirent *ent;
        int found = 0;

        if (proc) {
            while ((ent = readdir(proc)) != NULL) {
                char path[64];
                char buf[256];
                FILE *file;
                size_t n;
                size_t i;

                if (ent->d_name[0] < '1' || ent->d_name[0] > '9') {
                    continue;
                }
                snprintf(path, sizeof(path), "/proc/%s/comm", ent->d_name);
                file = fopen(path, "r");
                if (file) {
                    if (fgets(buf, sizeof(buf), file)) {
                        n = strlen(buf);
                        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
                            buf[--n] = '\0';
                        }
                        if (os_stricmp(buf, "destiny2.exe") == 0 || os_stricmp(buf, "destiny2") == 0) {
                            found = 1;
                        }
                    }
                    fclose(file);
                }
                if (found) {
                    break;
                }
                snprintf(path, sizeof(path), "/proc/%s/cmdline", ent->d_name);
                file = fopen(path, "r");
                if (!file) {
                    continue;
                }
                n = fread(buf, 1, sizeof(buf) - 1, file);
                fclose(file);
                buf[n] = '\0';
                for (i = 0; i < n; i++) {
                    if (buf[i] == '\0') {
                        buf[i] = ' ';
                    }
                }
                if (contains_ci(buf, "destiny2.exe")) {
                    found = 1;
                    break;
                }
            }
            closedir(proc);
        }
        g_d2_running = found;
    }
#endif
    g_d2_running_ms = now;
    return g_d2_running;
}

static void
clear_game_proc(void)
{
#ifdef _WIN32
    if (g_game_proc) {
        CloseHandle(g_game_proc);
        g_game_proc = NULL;
    }
    g_game_pid = 0;
#else
    g_game_pid = 0;
#endif
    g_game_launch_ms = 0;
}

static int
game_proc_alive(void)
{
#ifdef _WIN32
    DWORD code = 0;

    if (g_game_proc) {
        if (GetExitCodeProcess(g_game_proc, &code) && code == STILL_ACTIVE) {
            return 1;
        }
        CloseHandle(g_game_proc);
        g_game_proc = NULL;
        g_game_pid = 0;
    }
#else
    if (g_game_pid > 0) {
        if (kill(g_game_pid, 0) == 0) {
            return 1;
        }
        g_game_pid = 0;
    }
#endif
    return destiny2_running();
}

static void
kill_destiny2(void)
{
#ifdef _WIN32
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe;

    if (g_game_pid) {
        kill_pid_tree(g_game_pid);
    }
    if (snap == INVALID_HANDLE_VALUE) {
        return;
    }
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            if (os_stricmp(pe.szExeFile, "destiny2.exe") == 0) {
                kill_pid_tree(pe.th32ProcessID);
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
#else
    if (g_game_pid > 0) {
        kill(g_game_pid, SIGTERM);
        kill(-g_game_pid, SIGTERM);
    }
    system("pkill -f '[Dd]estiny2' >/dev/null 2>&1");
#endif
}

#ifdef _WIN32
static BOOL CALLBACK
destiny2_wnd_cb(HWND hwnd, LPARAM lp)
{
    DWORD pid = 0;
    char title[80];

    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    GetWindowThreadProcessId(hwnd, &pid);
    if (g_game_pid && pid == g_game_pid) {
        *(int *)lp = 1;
        return FALSE;
    }
    if (GetWindowTextA(hwnd, title, (int)sizeof(title)) > 0 &&
        (strstr(title, "Destiny 2") || strstr(title, "Destiny2"))) {
        *(int *)lp = 1;
        return FALSE;
    }
    return TRUE;
}
#endif

static int
destiny2_has_window(void)
{
#ifdef _WIN32
    uint32_t now = os_tick_ms();
    uint32_t ttl = g_game_launch_ms ? 150u : 400u;
    int found = 0;

    if (g_d2_window_ms && (now - g_d2_window_ms) < ttl) {
        return g_d2_window;
    }
    EnumWindows(destiny2_wnd_cb, (LPARAM)&found);
    g_d2_window = found;
    g_d2_window_ms = now;
    return found;
#else
    return destiny2_running();
#endif
}

static void
exe_dir(const char *exe, char *out, size_t max)
{
    char *slash;

    snprintf(out, max, "%s", exe);
    slash = strrchr(out, '\\');
    if (!slash) {
        slash = strrchr(out, '/');
    }
    if (slash) {
        *slash = '\0';
    }
}

static void
remove_steam_appid(const char *dir)
{
    char path[MAX_PATH];
    char nested[MAX_PATH];

    if (!dir || !dir[0]) {
        return;
    }
    if (os_join(path, sizeof(path), dir, "steam_appid.txt")) {
        os_delete_file(path);
    }
    if (os_join(path, sizeof(path), dir, "steam_app_id.txt")) {
        os_delete_file(path);
    }
    if (os_join(nested, sizeof(nested), dir, "bin") &&
        os_join(path, sizeof(path), nested, "x64") &&
        os_join(nested, sizeof(nested), path, "steam_appid.txt")) {
        os_delete_file(nested);
    }
}

static void write_launch_scripts(const char *dir);
static int require_licenses(void);

static int
launch_game_tracked(void)
{
    char root[MAX_PATH];
    char exe[MAX_PATH];

    if (!require_licenses()) {
        return 0;
    }
    if (!g_dir[0] && !(g_exe[0] && file_exists(g_exe))) {
        return 0;
    }
    exe[0] = '\0';
    if (g_exe[0] && file_exists(g_exe)) {
        snprintf(exe, sizeof(exe), "%s", g_exe);
        if (!path_parent(root, sizeof(root), exe)) {
            snprintf(root, sizeof(root), "%s", g_dir);
        }
    } else {
        snprintf(root, sizeof(root), "%s", g_dir);
        if (!(os_join(exe, sizeof(exe), root, "destiny2.exe") && file_exists(exe)) &&
            !find_game_exe_in(root, exe, sizeof(exe))) {
            return 0;
        }
    }
    remove_steam_appid(root);
#ifdef _WIN32
    {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        char cmd[MAX_PATH + 24];

        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        si.cb = sizeof(si);
        snprintf(cmd, sizeof(cmd), "\"%s\"", exe);
        SetEnvironmentVariableA("SteamAppId", NULL);
        SetEnvironmentVariableA("SteamGameId", NULL);
        SetEnvironmentVariableA("SteamOverlayGameId", NULL);
        SetEnvironmentVariableA("DAWN_FOREST_BASELINE", "1");
        if (!CreateProcessA(
                exe,
                cmd,
                NULL,
                NULL,
                FALSE,
                CREATE_NEW_PROCESS_GROUP,
                NULL,
                root,
                &si,
                &pi
            )) {
            return 0;
        }
        CloseHandle(pi.hThread);
        clear_game_proc();
        g_game_proc = pi.hProcess;
        g_game_pid = pi.dwProcessId;
    }
#else
    {
        char script[MAX_PATH];
        pid_t pid;

        write_launch_scripts(root);
        if (!os_join(script, sizeof(script), root, "launch-destiny.sh") || !file_exists(script)) {
            return 0;
        }
        chmod(script, 0755);
        pid = fork();
        if (pid < 0) {
            return 0;
        }
        if (pid == 0) {
            setpgid(0, 0);
            unsetenv("SteamAppId");
            unsetenv("SteamGameId");
            unsetenv("SteamOverlayGameId");
            setenv("DAWN_FOREST_BASELINE", "1", 1);
            if (exe[0]) {
                setenv("DAWN_GAME_EXE", exe, 1);
            }
            if (root[0] && chdir(root) != 0) {
                _exit(1);
            }
            execl("/bin/sh", "sh", script, (char *)NULL);
            execl(exe, exe, (char *)NULL);
            _exit(127);
        }
        setpgid(pid, pid);
        g_game_pid = pid;
    }
#endif
    g_game_launch_ms = os_tick_ms();
    invalidate_game_scan();
    set_status("Starting");
    return 1;
}

static void
poll_game(void)
{
    int running = destiny2_running();
    int alive = game_proc_alive();
    uint32_t now = os_tick_ms();
    uint32_t age = g_game_launch_ms ? now - g_game_launch_ms : 0;

    if (!running && !alive) {
        g_game_state = (g_game_launch_ms && age < 2500u) ? 1 : 0;
    } else if (running && age >= 12000u) {
        g_game_state = 2;
    } else if (running && destiny2_has_window() && age >= 4000u) {
        g_game_state = 2;
    } else {
        g_game_state = 1;
    }

    if (running) {
        if (strcmp(g_status, "Starting") == 0) {
            set_status("Running");
        }
        return;
    }
    if (alive && g_game_launch_ms && age < 12000u) {
        return;
    }
    if ((strcmp(g_status, "Starting") == 0 || strcmp(g_status, "Running") == 0) && !alive && !running) {
        clear_game_proc();
        set_status("Dawn is ready");
    }
}

/*
 * "Finished" state of the two depots we need. DepotDownloader's depot.config
 * is authoritative when present. Without it (foreign/hand-made folders) only
 * our own success marker counts, never bare file sizes: DepotDownloader
 * pre-allocates every file at full size before any bytes arrive, so a
 * cancelled run looks complete on disk.
 */
static int
content_depot_finished(const char *dir)
{
    int rc;

    if (!dir || !dir[0]) {
        return 0;
    }
    rc = depot_config_has(dir, INSTALL_DEPOT_CONTENT, INSTALL_MANIFEST_CONTENT);
    if (rc >= 0) {
        return rc;
    }
    return marker_matches(dir);
}

static int
language_depot_finished(const char *dir)
{
    int rc;

    if (!dir || !dir[0]) {
        return 0;
    }
    rc = depot_config_has(dir, lang_depot(), lang_manifest());
    if (rc >= 0) {
        return rc;
    }
    return marker_matches(dir);
}

static int
dawn_depots_present(const char *dir)
{
    return content_depot_finished(dir) && language_depot_finished(dir);
}

static int
language_depot_present(const char *dir)
{
    return language_depot_finished(dir);
}

static int
content_files_ready(const char *dir)
{
    char exe[MAX_PATH];

    if (!dir || !dir[0] || !os_dir_exists(dir)) {
        return 0;
    }
    if (!find_game_exe_in(dir, exe, sizeof(exe)) || os_file_size(exe) != GAME_EXE_SIZE) {
        return 0;
    }
    if (!packages_ready(dir)) {
        return 0;
    }
    return 1;
}

static int
content_depot_present(const char *dir)
{
    return content_depot_finished(dir) && content_files_ready(dir);
}

static int
depots_ready(const char *dir)
{
    return content_depot_present(dir) && language_depot_finished(dir);
}

static int
install_complete(const char *dir)
{
    if (!depots_ready(dir) || !dawn_ready(dir)) {
        return 0;
    }
    if (!marker_matches(dir)) {
        write_marker(dir);
    }
    return 1;
}

static void
invalidate_install_ready(void)
{
    g_ready_gen += 1;
    g_installed = 0;
    g_installed_check = 0;
    g_scan_check = 0;
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
    if (strcmp(depot, lang_depot()) == 0 && strcmp(manifest, lang_manifest()) != 0) {
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
    if (dawn_depots_present(dir)) {
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
depot_root_in(const char *dir, char *out, size_t max)
{
    char nested[MAX_PATH];

    if (!dir || !dir[0] || !os_dir_exists(dir)) {
        return 0;
    }
    if (dawn_depots_present(dir) || game_root_ok(dir) ||
        (find_game_exe_in(dir, NULL, 0) && packages_ready(dir) && !is_live_latest_d2(dir))) {
        if (out && max > 0) {
            snprintf(out, max, "%s", dir);
        }
        return 1;
    }
    if (find_named(dir, "Destiny2", 1, nested, sizeof(nested)) &&
        (dawn_depots_present(nested) || game_root_ok(nested) ||
         (find_game_exe_in(nested, NULL, 0) && packages_ready(nested) && !is_live_latest_d2(nested)))) {
        if (out && max > 0) {
            snprintf(out, max, "%s", nested);
        }
        return 1;
    }
    return 0;
}

static int
adopt_depot_root(void)
{
    char root[MAX_PATH];

    if (!depot_root_in(g_dir, root, sizeof(root))) {
        return 0;
    }
    if (os_stricmp(g_dir, root) != 0) {
        snprintf(g_dir, sizeof(g_dir), "%s", root);
        persist_dir();
    }
    return 1;
}

static int
same_volume(const char *a, const char *b)
{
#ifdef _WIN32
    char va[MAX_PATH];
    char vb[MAX_PATH];

    if (!a || !b || !GetVolumePathNameA(a, va, MAX_PATH) || !GetVolumePathNameA(b, vb, MAX_PATH)) {
        return 0;
    }
    return os_stricmp(va, vb) == 0;
#else
    struct stat sa;
    struct stat sb;
    return a && b && stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_dev == sb.st_dev;
#endif
}

static int
link_or_copy_file(const char *from, const char *to)
{
    if (!from || !to) {
        return 0;
    }
    /* DepotDownloader rewrites its trace files in place; never share those
     * inodes, and always refresh them so the cache tracks completed depots. */
    if (contains_ci(from, ".DepotDownloader")) {
        return os_copy_file(from, to);
    }
    if (file_exists(to)) {
        return 1;
    }
#ifdef _WIN32
    if (CreateHardLinkA(to, from, NULL)) {
        return 1;
    }
#else
    if (link(from, to) == 0) {
        return 1;
    }
#endif
    if (os_file_size(from) > 8ull * 1024ull * 1024ull) {
        return 0;
    }
    return os_copy_file(from, to);
}

static int stage_cached_game(const char *from, const char *to);

static int
stage_entry_cb(const char *name, int is_dir, void *user)
{
    const char **dirs = (const char **)user;
    char src[MAX_PATH];
    char dst[MAX_PATH];

    if (!name || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 1;
    }
    if (os_stricmp(name, "Dawn") == 0 ||
        os_stricmp(name, "Sunrise") == 0 ||
        os_stricmp(name, "Restoration") == 0 ||
        os_stricmp(name, ".dawn") == 0 ||
        os_stricmp(name, ".sunrise") == 0 ||
        os_stricmp(name, "launch-destiny.cmd") == 0 ||
        os_stricmp(name, "launch-destiny.sh") == 0 ||
        os_stricmp(name, "release.json") == 0) {
        return 1;
    }
    if (!os_join(src, sizeof(src), dirs[0], name) || !os_join(dst, sizeof(dst), dirs[1], name)) {
        return 1;
    }
    if (os_stricmp(name, "steam_api64.dll") == 0 && os_file_size(src) > (1ull << 20)) {
        return 1;
    }
    if (is_dir) {
        stage_cached_game(src, dst);
        return 1;
    }
    link_or_copy_file(src, dst);
    return 1;
}

static int
stage_cached_game(const char *from, const char *to)
{
    const char *dirs[3];

    if (!from || !to || !os_dir_exists(from)) {
        return 0;
    }
    if (os_stricmp(from, to) == 0) {
        return 1;
    }
    os_mkdirs(to);
    dirs[0] = from;
    dirs[1] = to;
    dirs[2] = "ok";
    os_list_dir(from, stage_entry_cb, dirs);
    return content_depot_present(to) || dirs[2][0] != '\0';
}

static void
depot_cache_dir(char *out, size_t max)
{
    char dawn[MAX_PATH];

    os_data_dir(dawn, sizeof(dawn));
    if (!os_join(out, max, dawn, "depot-cache")) {
        out[0] = '\0';
    }
}

static int
depot_cache_ready(const char *dir)
{
    return dir && dir[0] && content_depot_present(dir) && language_depot_present(dir);
}

static int
cache_root_ok(const char *dir, char *out, size_t max)
{
    char nested[MAX_PATH];

    if (!dir || !dir[0] || !os_dir_exists(dir)) {
        return 0;
    }
    if (content_depot_present(dir)) {
        snprintf(out, max, "%s", dir);
        return 1;
    }
    if (os_join(nested, sizeof(nested), dir, INSTALL_DEPOT_CONTENT) && content_depot_present(nested)) {
        snprintf(out, max, "%s", nested);
        return 1;
    }
    if (os_join(nested, sizeof(nested), dir, INSTALL_MANIFEST_CONTENT) && content_depot_present(nested)) {
        snprintf(out, max, "%s", nested);
        return 1;
    }
    return 0;
}

static int
find_content_cache(char *out, size_t max)
{
    char base[MAX_PATH];
    char path[MAX_PATH];
    char tool[MAX_PATH];

    if (content_depot_present(g_dir)) {
        snprintf(out, max, "%s", g_dir);
        return 1;
    }
    depot_cache_dir(path, sizeof(path));
    if (path[0] && os_stricmp(path, g_dir) != 0 && cache_root_ok(path, out, max)) {
        return 1;
    }
    tool[0] = '\0';
    if (g_tool[0]) {
        exe_dir(g_tool, tool, sizeof(tool));
    } else if (g_root[0] && os_join(path, sizeof(path), g_root, "tools") &&
               os_join(tool, sizeof(tool), path, "DepotDownloader") &&
               !os_dir_exists(tool)) {
        tool[0] = '\0';
    }
    if (tool[0] && os_join(path, sizeof(path), tool, "depots") &&
        os_join(base, sizeof(base), path, INSTALL_DEPOT_CONTENT) &&
        cache_root_ok(base, out, max)) {
        return 1;
    }
    depot_work_dir(base, (int)sizeof(base));
    if (cache_root_ok(base, out, max)) {
        return 1;
    }
    if (os_join(path, sizeof(path), base, "depots") &&
        os_join(base, sizeof(base), path, INSTALL_DEPOT_CONTENT) &&
        cache_root_ok(base, out, max)) {
        return 1;
    }
    return 0;
}

static void
preserve_depot_cache(void)
{
    char cache[MAX_PATH];

    if (!g_dir[0] || !content_depot_present(g_dir)) {
        return;
    }
    depot_cache_dir(cache, sizeof(cache));
    if (!cache[0] || os_stricmp(g_dir, cache) == 0) {
        return;
    }
    if (!same_volume(g_dir, cache)) {
        return;
    }
    if (depot_cache_ready(cache)) {
        return;
    }
    /* Volumes without hardlinks (FAT/exFAT) can never complete the cache;
     * don't re-walk thousands of files on every launch. */
    static int gave_up;
    if (gave_up) {
        return;
    }
    os_mkdirs(cache);
    stage_cached_game(g_dir, cache);
    if (!depot_cache_ready(cache)) {
        gave_up = 1;
    }
}

static int
apply_cached_install(void)
{
    char cache[MAX_PATH];

    if (!g_dir[0]) {
        return 0;
    }
    os_mkdirs(g_dir);
    if (content_depot_present(g_dir)) {
        return 1;
    }
    cache[0] = '\0';
    if (!find_content_cache(cache, sizeof(cache)) || !cache[0] || os_stricmp(cache, g_dir) == 0) {
        return 0;
    }
    if (!same_volume(cache, g_dir)) {
        return 0;
    }
    set_status("Using cached game files");
    g_phase = INSTALL_RUNNING;
    g_depot_progress = 0.15f;
    stage_cached_game(cache, g_dir);
    adopt_depot_root();
    g_depot_progress = content_depot_present(g_dir) ? 1.0f : 0.15f;
    return content_depot_present(g_dir);
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
        g_installed = install_complete(root);
        return g_installed;
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
        g_installed = install_complete(st.found);
        return g_installed;
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
        "app %s\ncontent %s %s\nlanguage %s %s %s\n",
        INSTALL_APP,
        INSTALL_DEPOT_CONTENT,
        INSTALL_MANIFEST_CONTENT,
        lang_steam(),
        lang_depot(),
        lang_manifest()
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
    if (os_join(path, sizeof(path), dawn, "game_exe.txt")) {
        file = fopen(path, "rb");
        if (file) {
            if (fgets(g_exe, (int)sizeof(g_exe), file)) {
                size_t n = strlen(g_exe);
                while (n > 0 && (g_exe[n - 1] == '\n' || g_exe[n - 1] == '\r')) {
                    g_exe[--n] = '\0';
                }
            }
            fclose(file);
        }
    }
    normalize_install_dir();
    os_mkdirs(g_dir);
    persist_dir();
    persist_exe();
    load_lang();
    refresh_installed();
}

static void exe_dir(const char *exe, char *out, size_t max);

static int
tool_at(const char *path)
{
    if (!path || !path[0] || !file_exists(path)) {
        return 0;
    }
    snprintf(g_tool, sizeof(g_tool), "%s", path);
    return 1;
}

static int
find_tool(void)
{
    char path[MAX_PATH];
    char mid[MAX_PATH];
    char exe[MAX_PATH];
    char local[MAX_PATH];
    const char *rel[] = {
        "tools/DepotDownloader" OS_EXE_EXT,
        "tools/DepotDownloader/DepotDownloader" OS_EXE_EXT,
#ifndef _WIN32
        "tools/DepotDownloader/DepotDownloader.exe",
        "tools/DepotDownloader.exe",
#endif
        NULL
    };
    int i;

    if (g_tool[0] && file_exists(g_tool)) {
        return 1;
    }
    for (i = 0; rel[i]; i++) {
        if (g_root[0] && os_join(path, sizeof(path), g_root, rel[i]) && tool_at(path)) {
            return 1;
        }
    }
#ifdef _WIN32
    {
        DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
        if (n > 0 && n < sizeof(exe)) {
            char *slash = strrchr(exe, '\\');
            if (slash) {
                *slash = '\0';
                if (os_join(path, sizeof(path), exe, "DepotDownloader.exe") && tool_at(path)) {
                    return 1;
                }
                if (os_join(mid, sizeof(mid), exe, "tools") &&
                    os_join(path, sizeof(path), mid, "DepotDownloader.exe") &&
                    tool_at(path)) {
                    return 1;
                }
            }
        }
    }
#else
    if (os_exe_dir(exe, sizeof(exe))) {
        if (os_join(path, sizeof(path), exe, "DepotDownloader") && tool_at(path)) {
            return 1;
        }
        if (os_join(path, sizeof(path), exe, "DepotDownloader.exe") && tool_at(path)) {
            return 1;
        }
        if (os_join(mid, sizeof(mid), exe, "tools") &&
            os_join(path, sizeof(path), mid, "DepotDownloader.exe") &&
            tool_at(path)) {
            return 1;
        }
        if (os_join(mid, sizeof(mid), exe, "tools") &&
            os_join(path, sizeof(path), mid, "DepotDownloader") &&
            os_join(mid, sizeof(mid), path, "DepotDownloader.exe") &&
            tool_at(mid)) {
            return 1;
        }
    }
#endif
    os_data_dir(local, sizeof(local));
    if (os_join(mid, sizeof(mid), local, "DepotDownloader")) {
        if (os_join(path, sizeof(path), mid, "DepotDownloader" OS_EXE_EXT) && tool_at(path)) {
            return 1;
        }
#ifndef _WIN32
        if (os_join(path, sizeof(path), mid, "DepotDownloader.exe") && tool_at(path)) {
            return 1;
        }
#endif
    }
    g_tool[0] = '\0';
    return 0;
}

#ifndef _WIN32
static int
cmd_on_path(const char *name)
{
    char line[256];

    if (!name || !name[0]) {
        return 0;
    }
    snprintf(line, sizeof(line), "command -v %s >/dev/null 2>&1", name);
    return system(line) == 0;
}
#endif

static int
format_tool_cmd(char *out, size_t max)
{
    if (!out || max < 8 || !g_tool[0]) {
        return 0;
    }
#ifdef _WIN32
    snprintf(out, max, "\"%s\"", g_tool);
    return 1;
#else
    size_t n = strlen(g_tool);
    if (n > 4 && os_stricmp(g_tool + n - 4, ".exe") == 0) {
        char dir[MAX_PATH];
        char dll[MAX_PATH];

        exe_dir(g_tool, dir, sizeof(dir));
        if (os_join(dll, sizeof(dll), dir, "DepotDownloader.dll") &&
            file_exists(dll) &&
            cmd_on_path("dotnet")) {
            snprintf(out, max, "dotnet \"%s\"", dll);
            return 1;
        }
        if (cmd_on_path("wine64")) {
            snprintf(out, max, "wine64 \"%s\"", g_tool);
            return 1;
        }
        snprintf(out, max, "wine \"%s\"", g_tool);
        return 1;
    }
    snprintf(out, max, "\"%s\"", g_tool);
    return 1;
#endif
}

#ifndef _WIN32
static void
sh_single_quote(const char *in, char *out, size_t max)
{
    size_t n = 0;

    if (!in || !out || max < 3) {
        if (out && max) {
            out[0] = '\0';
        }
        return;
    }
    out[n++] = '\'';
    for (; *in && n + 5 < max; in++) {
        if (*in == '\'') {
            out[n++] = '\'';
            out[n++] = '\\';
            out[n++] = '\'';
            out[n++] = '\'';
        } else {
            out[n++] = *in;
        }
    }
    out[n++] = '\'';
    out[n] = '\0';
}

static void
append_dd_password(char *cmd, size_t max)
{
    char quoted[512];
    size_t used;

    if (!cmd || max < 16 || !g_secret[0]) {
        return;
    }
    if (strstr(cmd, " -password ")) {
        return;
    }
    sh_single_quote(g_secret, quoted, sizeof(quoted));
    used = strlen(cmd);
    if (used + strlen(quoted) + 16 >= max) {
        return;
    }
    snprintf(cmd + used, max - used, " -password %s", quoted);
}
#endif

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
    g_need = INSTALL_NEED_NONE;
    set_status("Signing in");
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
    (void)text;
    set_status(verb);
}

static void
set_work_status(void)
{
    if (g_verify) {
        set_status("Checking files");
        return;
    }
    if (g_lang_only) {
        set_status("Downloading language");
        return;
    }
    set_status("Downloading game");
}

static int
status_is_prepare(void)
{
    return g_status[0] == '\0' ||
        strcmp(g_status, "Preparing files") == 0 ||
        strcmp(g_status, "Downloading files") == 0 ||
        strcmp(g_status, "Checking files") == 0;
}

static void
scan_output(const char *text)
{
    float pct = parse_percent(text);
    if (pct >= 0.0f) {
        g_depot_progress = pct;
        g_need = INSTALL_NEED_NONE;
        if (status_is_prepare()) {
            set_work_status();
        }
    }
    capture_dd_user(text);

    if (contains_ci(text, "Enter account password")) {
        g_need = INSTALL_NEED_PASSWORD;
        set_status("Enter password");
        if (g_secret[0]) {
            send_secret();
        }
        return;
    }
    if (contains_ci(text, "STEAM GUARD!")) {
        if (contains_ci(text, "Mobile App")) {
            g_need = INSTALL_NEED_NONE;
            set_status("Confirm Guard on phone");
            return;
        }
        g_need = INSTALL_NEED_GUARD;
        set_status("Enter Guard code");
        if (g_secret[0]) {
            send_secret();
        }
        return;
    }
    if (contains_ci(text, "Logging ") && contains_ci(text, "into Steam")) {
        set_status("Opening Steam");
        return;
    }
    if (contains_ci(text, "validat") && !contains_ci(text, "invalid")) {
        set_status(g_verify ? "Checking" : "Downloading");
        return;
    }
    if (contains_ci(text, "pre-alloc") || contains_ci(text, "prealloc")) {
        set_work_status();
        return;
    }
    if (contains_ci(text, "already exist") ||
        contains_ci(text, "already installed") ||
        contains_ci(text, "up to date") ||
        contains_ci(text, "no files to download") ||
        contains_ci(text, "from cache") ||
        contains_ci(text, "using local") ||
        contains_ci(text, "unchanged") ||
        contains_ci(text, "missing 0")) {
        if (g_verify) {
            set_status("Checking");
        } else if (g_lang_only) {
            set_status("Downloading language");
        } else {
            set_status("Using cached game files");
        }
        return;
    }
    if (contains_ci(text, "depot complete")) {
        set_status(g_verify ? "Finishing check" : "Finishing");
        return;
    }
    if (contains_ci(text, "processing depot") || contains_ci(text, "downloading depot")) {
        set_status(g_verify ? "Checking files" : "Downloading");
        return;
    }
    if (contains_ci(text, "receiving objects")) {
        set_status("Getting scripts");
        return;
    }
    if (contains_ci(text, "resolving deltas")) {
        set_status("Updating scripts");
        return;
    }
    if (contains_ci(text, "checking out files") || contains_ci(text, "updating files")) {
        set_status("Installing scripts");
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
        set_status("Reconnecting");
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
        setpgid(0, 0);
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
    setpgid(pid, pid);
    close(out_pipe[1]);
    close(in_pipe[0]);
    g_process = pid;
    (void)show_window;
#endif
    g_cancel = 0;
    g_busy = 1;
    g_phase = INSTALL_RUNNING;
    return 1;
}

static void queue_dawn(void);
static void advance_dawn(void);
static void apply_remove(void);
static int collect_wipe_cb(const char *name, int is_dir, void *user);
static void repair_vc_runtimes(const char *dir);
static int start_depot_repair(void);
static int purge_zeroed_sidecars(const char *dir, const char *list_path);
static int sku_config_ok(const char *dir);

static int
complete_install(void)
{
    close_child();
    g_busy = 0;
    g_need = INSTALL_NEED_NONE;
    g_phase = INSTALL_OK;
    g_depot_progress = 1.0f;
    mark_installed();
    repair_vc_runtimes(g_dir);
    remove_steam_appid(g_dir);
    preserve_depot_cache();
    purge_zeroed_sidecars(g_dir, NULL);
    if (!sku_config_ok(g_dir) && start_depot_repair()) {
        return 1;
    }
    if (game_root_ok(g_dir) && dawn_ready(g_dir)) {
        g_installed = 1;
        set_status(g_verify ? "Files verified" : "Dawn is ready");
    } else if (game_root_ok(g_dir)) {
        g_installed = 0;
        set_status("Need Dawn overlay");
    } else {
        g_installed = 0;
        set_status("Install incomplete");
    }
    g_verify = 0;
    g_user_start = 0;
    forget_depot_password();
    return 1;
}

static void
bind_steam_identity(void)
{
    const char *user = steam_auth_dd_user();

    if (!steam_auth_signed_in()) {
        if (g_secret[0] || g_session_ready) {
            forget_depot_password();
        }
        if (!g_busy) {
            g_setup_creds = 0;
            if (g_need == INSTALL_NEED_PASSWORD ||
                g_need == INSTALL_NEED_ACCOUNT ||
                g_need == INSTALL_NEED_GUARD) {
                g_need = INSTALL_NEED_NONE;
            }
        }
        g_session_ready = 0;
        g_session_tried = 0;
        return;
    }
    if (user && user[0] && os_stricmp(g_user, user) != 0) {
        snprintf(g_user, sizeof(g_user), "%s", user);
    }
}

#ifdef __GNUC__
__attribute__((unused))
#endif
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
        /* A non-empty list that matches nothing. An empty list makes DepotDownloader
         * download the whole depot after login. */
        fputs("dawn-steam-login-only\n", file);
        fclose(file);
    }
    {
        char tool[MAX_PATH + 64];
        if (!format_tool_cmd(tool, sizeof(tool))) {
            return 0;
        }
    snprintf(
        command,
        sizeof(command),
        "%s -app %s -depot %s -manifest %s -dir \"%s\" -filelist \"%s\" -username \"%s\" "
        "-remember-password -os windows -osarch 64",
        tool,
        INSTALL_APP,
        INSTALL_DEPOT_CONTENT,
        INSTALL_MANIFEST_CONTENT,
        dest,
        list,
        g_user
    );
    }
    g_session_job = 1;
    g_session_tried = 1;
    if (!start_child(command, work, 0, "Steam login failed")) {
        g_session_job = 0;
        return 0;
    }
    set_status("Connecting Steam");
    return 1;
}

static int
needs_depot_download(void)
{
    if (!g_dir[0]) {
        return 0;
    }
    if (install_complete(g_dir) || depots_ready(g_dir)) {
        return 0;
    }
    if (content_depot_present(g_dir) && language_depot_present(g_dir)) {
        return 0;
    }
    return 1;
}

static void
begin_depot_login(void)
{
    if (g_busy) {
        return;
    }
    if (!needs_depot_download()) {
        g_setup_creds = 0;
        g_need = INSTALL_NEED_NONE;
        return;
    }
    bind_steam_identity();
    g_setup_creds = 1;
    g_session_ready = 0;
    g_session_tried = 0;
    g_cancel = 0;
    if (!g_user[0]) {
        g_need = INSTALL_NEED_ACCOUNT;
        set_status("Enter Steam username");
        return;
    }
    g_need = INSTALL_NEED_PASSWORD;
    set_status("Enter password");
}

static void
maybe_prepare_session(void)
{
    /* Password stays in memory only until the depot job finishes. Do not
     * pre-login just to persist DepotDownloader tokens. */
    (void)0;
}

static int
require_licenses(void)
{
    const char *block = steam_auth_play_block();

    if (block) {
        set_status(block);
        return 0;
    }
    return 1;
}

static int
start_depot(void)
{
    char command[2048];
    char work[MAX_PATH];

    if (!require_licenses()) {
        g_phase = INSTALL_FAILED;
        g_busy = 0;
        g_launch_after = 0;
        return 0;
    }
    g_session_job = 0;
    g_step = STEP_DEPOT_CONTENT;
    bind_steam_identity();
    if (!g_user[0]) {
        fail_job("No Steam username");
        return 0;
    }
    {
        char tool[MAX_PATH + 64];
        if (!format_tool_cmd(tool, sizeof(tool))) {
            fail_job("DepotDownloader missing");
            return 0;
        }

    if (g_filelist[0] && file_exists(g_filelist)) {
        snprintf(
            command,
            sizeof(command),
            "%s -app %s -depot %s -manifest %s -dir \"%s\" -filelist \"%s\" "
            "-username \"%s\" -remember-password -os windows -osarch 64 "
            "-max-servers 32 -max-downloads 32",
            tool,
            INSTALL_APP,
            INSTALL_DEPOT_CONTENT,
            INSTALL_MANIFEST_CONTENT,
            g_dir,
            g_filelist,
            g_user
        );
    } else if (g_lang_only) {
        snprintf(
            command,
            sizeof(command),
            "%s -app %s -depot %s -manifest %s -dir \"%s\" -username \"%s\" "
            "-remember-password -os windows -osarch 64 "
            "%s-max-servers 32 -max-downloads 32",
            tool,
            INSTALL_APP,
            lang_depot(),
            lang_manifest(),
            g_dir,
            g_user,
            g_verify ? "-validate " : ""
        );
    } else {
        snprintf(
            command,
            sizeof(command),
            "%s -app %s -depot %s %s -manifest %s %s -dir \"%s\" -username \"%s\" "
            "-remember-password -os windows -osarch 64 "
            "%s-max-servers 32 -max-downloads 32",
            tool,
            INSTALL_APP,
            INSTALL_DEPOT_CONTENT,
            lang_depot(),
            INSTALL_MANIFEST_CONTENT,
            lang_manifest(),
            g_dir,
            g_user,
            g_verify ? "-validate " : ""
        );
    }
    }
#ifndef _WIN32
    append_dd_password(command, sizeof(command));
#endif

    depot_work_dir(work, MAX_PATH);
    set_work_status();
    if (!start_child(command, work, 0, "Downloader failed")) {
        return 0;
    }
    return 1;
}

static int
start_missing_language(int launch_after)
{
    if (language_depot_present(g_dir)) {
        return 0;
    }
    bind_steam_identity();
    if (!g_user[0] || !find_tool()) {
        set_status("Need Steam login for language");
        return 0;
    }
    g_lang_only = 1;
    g_launch_after = launch_after;
    g_user_start = 1;
    g_filelist[0] = '\0';
    g_verify = 0;
    g_phase = INSTALL_RUNNING;
    if (!start_depot()) {
        g_lang_only = 0;
        g_launch_after = 0;
        return 0;
    }
    set_status("Downloading language");
    return 1;
}

static int
start_depot_repair(void)
{
    char work[MAX_PATH];
    char list[MAX_PATH];

    g_filelist[0] = '\0';
    bind_steam_identity();
    if (!g_user[0] || !find_tool()) {
        return 0;
    }
    depot_work_dir(work, MAX_PATH);
    if (!os_join(list, sizeof(list), work, "repair-files.txt")) {
        return 0;
    }
    os_mkdirs(work);
    purge_zeroed_sidecars(g_dir, list);
    remove_steam_appid(g_dir);
    if (!file_exists(list) || os_file_size(list) == 0) {
        return sku_config_ok(g_dir);
    }
    snprintf(g_filelist, sizeof(g_filelist), "%s", list);
    g_verify = 0;
    g_user_start = 1;
    g_phase = INSTALL_RUNNING;
    if (!start_depot()) {
        g_filelist[0] = '\0';
        return 0;
    }
    set_status("Repairing game files");
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
    if (os_join(path, sizeof(path), g_root, "payload") &&
        os_join(mid, sizeof(mid), path, "dawn") &&
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
dawn_asset_ok(const char *url)
{
    if (!url || !contains_ci(url, ".zip")) {
        return 0;
    }
    if (contains_ci(url, "sha256") || contains_ci(url, "source")) {
        return 0;
    }
    return 1;
}

static void
tag_from_download_url(const char *url, char *tag, size_t tag_max)
{
    const char *p;
    const char *slash;
    size_t n;

    if (!url || !tag || tag_max < 2) {
        return;
    }
    p = strstr(url, "/download/");
    if (!p) {
        return;
    }
    p += 10;
    slash = strchr(p, '/');
    if (!slash || slash <= p) {
        return;
    }
    n = (size_t)(slash - p);
    if (n >= tag_max) {
        n = tag_max - 1;
    }
    memcpy(tag, p, n);
    tag[n] = '\0';
}

static int
curl_to_file(const char *url, const char *dest, const char *work)
{
    char curl[MAX_PATH];
    char command[2048];

    if (!url || !dest) {
        return 0;
    }
    find_curl(curl, sizeof(curl));
    snprintf(
        command,
        sizeof(command),
        "\"%s\" -fsSL --retry 2 -L -A DawnLauncher/1.0 -o \"%s\" \"%s\"",
        curl,
        dest,
        url
    );
    return run_hidden(command, work, 30000);
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
            if (!dawn_asset_ok(found)) {
                continue;
            }
            if (contains_ci(found, "Dawn-") ||
                contains_ci(found, "/Dawn/") ||
                contains_ci(found, "Hotfix") ||
                contains_ci(found, "0.1.")) {
                snprintf(url, url_max, "%s", found);
                if (tag && tag[0] == '\0') {
                    tag_from_download_url(found, tag, tag_max);
                }
                best = p;
                break;
            }
            if (!best) {
                snprintf(url, url_max, "%s", found);
                if (tag && tag[0] == '\0') {
                    tag_from_download_url(found, tag, tag_max);
                }
                best = p;
            }
        }
    }
    free(buf);
    return url && url[0] != '\0';
}

static void
plain_dawn_tag(char *text)
{
    if (text && (text[0] == 'v' || text[0] == 'V') && text[1]) {
        memmove(text, text + 1, strlen(text));
    }
}

static void
set_dawn_latest_tag(const char *tag)
{
    if (!tag || !tag[0]) {
        return;
    }
    snprintf(g_dawn_latest, sizeof(g_dawn_latest), "%s", tag);
    plain_dawn_tag(g_dawn_latest);
}

static int
file_json_string(const char *path, const char *key, char *out, int max)
{
    FILE *file;
    char buf[4096];
    char needle[80];
    size_t n;
    const char *p;

    if (!path || !key || !out || max < 2) {
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    n = fread(buf, 1, sizeof(buf) - 1, file);
    fclose(file);
    buf[n] = '\0';
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(buf, needle);
    if (!p) {
        return 0;
    }
    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    if (*p != ':') {
        return 0;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '"') {
        return 0;
    }
    p++;
    n = 0;
    while (*p && *p != '"' && n + 1 < (size_t)max) {
        out[n++] = *p++;
    }
    out[n] = '\0';
    return n > 0;
}

static void
mark_dawn_installed_dirty(void)
{
    g_dawn_installed_dirty = 1;
}

static void
refresh_dawn_installed(void)
{
    char path[MAX_PATH];
    char tag[64];

    g_dawn_installed[0] = '\0';
    tag[0] = '\0';
    if (g_dir[0] &&
        os_join(path, sizeof(path), g_dir, ".dawn") &&
        os_join(path, sizeof(path), path, "release.json")) {
        file_json_string(path, "release", tag, (int)sizeof(tag));
    }
    if (!tag[0] && g_dir[0] && os_join(path, sizeof(path), g_dir, "release.json")) {
        file_json_string(path, "release", tag, (int)sizeof(tag));
    }
    if (tag[0] && strcmp(tag, "latest") != 0) {
        snprintf(g_dawn_installed, sizeof(g_dawn_installed), "%s", tag);
        plain_dawn_tag(g_dawn_installed);
    }
    g_dawn_installed_dirty = 0;
}

static void
close_dawn_ver_proc(void)
{
#ifdef _WIN32
    if (g_dawn_ver_proc) {
        CloseHandle(g_dawn_ver_proc);
        g_dawn_ver_proc = NULL;
    }
#else
    g_dawn_ver_proc = 0;
#endif
}

static int
dawn_ver_proc_running(void)
{
#ifdef _WIN32
    if (!g_dawn_ver_proc) {
        return 0;
    }
    if (WaitForSingleObject(g_dawn_ver_proc, 0) == WAIT_TIMEOUT) {
        return 1;
    }
    close_dawn_ver_proc();
    return 0;
#else
    int st = 0;
    if (g_dawn_ver_proc <= 0) {
        return 0;
    }
    if (waitpid(g_dawn_ver_proc, &st, WNOHANG) == 0) {
        return 1;
    }
    g_dawn_ver_proc = 0;
    return 0;
#endif
}

static void
finish_dawn_latest_check(void)
{
    char tag[64];

    tag[0] = '\0';
    if (!g_dawn_latest_json[0]) {
        return;
    }
    if (file_json_string(g_dawn_latest_json, "tag_name", tag, (int)sizeof(tag))) {
        set_dawn_latest_tag(tag);
    }
}

static void
begin_dawn_latest_check(void)
{
    char curl[MAX_PATH];
    char command[2048];
#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char runnable[2048];
#endif

    if (!g_dawn_latest_json[0] || dawn_ver_proc_running()) {
        return;
    }
    find_curl(curl, sizeof(curl));
    snprintf(
        command,
        sizeof(command),
        "\"%s\" -fsSL --http1.1 --connect-timeout 15 --max-time 25 -A DawnLauncher -o \"%s\" \"%s\"",
        curl,
        g_dawn_latest_json,
        DAWN_RELEASES_API
    );
#ifdef _WIN32
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    snprintf(runnable, sizeof(runnable), "%s", command);
    if (!CreateProcessA(
            NULL,
            runnable,
            NULL,
            NULL,
            FALSE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &si,
            &pi
        )) {
        return;
    }
    CloseHandle(pi.hThread);
    g_dawn_ver_proc = pi.hProcess;
#else
    {
        pid_t pid = fork();
        if (pid < 0) {
            return;
        }
        if (pid == 0) {
            execl("/bin/sh", "sh", "-c", command, (char *)NULL);
            _exit(127);
        }
        g_dawn_ver_proc = pid;
    }
#endif
    g_dawn_ver_pending = 1;
}

static void
poll_dawn_release_version(void)
{
    uint32_t now = os_tick_ms();

    if (g_dawn_installed_dirty) {
        refresh_dawn_installed();
    }
    if (dawn_ver_proc_running()) {
        return;
    }
    if (g_dawn_ver_pending) {
        g_dawn_ver_pending = 0;
        finish_dawn_latest_check();
        return;
    }
    if (g_dawn_latest_ms && (now - g_dawn_latest_ms) < 1800000u) {
        return;
    }
    if (g_dawn_latest_ms == 0 && now < 1200u) {
        return;
    }
    g_dawn_latest_ms = now ? now : 1;
    begin_dawn_latest_check();
}

static int
html_find_dawn_zip(const char *path, char *url, size_t url_max, char *tag, size_t tag_max)
{
    FILE *file;
    char *buf;
    long size;
    const char *p;
    char found[1024];
    int ok = 0;

    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0 || size > 2 * 1024 * 1024) {
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
    for (p = buf; (p = strstr(p, "/releases/download/")) != NULL; p += 19) {
        const char *start = p;
        const char *end;
        size_t n;

        while (start > buf && start[-1] != '"' && start[-1] != '\'') {
            start--;
            if (p - start > 80) {
                start = p;
                break;
            }
        }
        end = strstr(p, ".zip");
        if (!end) {
            continue;
        }
        end += 4;
        n = (size_t)(end - start);
        if (n < 16 || n >= sizeof(found)) {
            continue;
        }
        memcpy(found, start, n);
        found[n] = '\0';
        if (!dawn_asset_ok(found)) {
            continue;
        }
        if (found[0] == '/') {
            snprintf(url, url_max, "https://github.com%s", found);
        } else {
            snprintf(url, url_max, "%s", found);
        }
        if (tag && tag_max > 1 && tag[0] == '\0') {
            tag_from_download_url(url, tag, tag_max);
        }
        ok = 1;
        break;
    }
    free(buf);
    return ok && url && url[0] != '\0';
}

static int
fetch_dawn_from_github(void)
{
    char work[MAX_PATH];
    char meta[MAX_PATH];

    g_dawn_zip_url[0] = '\0';
    g_dawn_tag[0] = '\0';
    depot_work_dir(work, MAX_PATH);
    if (os_join(meta, sizeof(meta), work, "dawn-latest.json") &&
        curl_to_file(DAWN_RELEASES_API, meta, work) &&
        json_find_dawn_zip(meta, g_dawn_zip_url, sizeof(g_dawn_zip_url), g_dawn_tag, sizeof(g_dawn_tag))) {
        set_dawn_latest_tag(g_dawn_tag);
        return 1;
    }
    if (os_join(meta, sizeof(meta), work, "dawn-latest.html") &&
        curl_to_file(DAWN_RELEASES_LATEST_HTML, meta, work) &&
        html_find_dawn_zip(meta, g_dawn_zip_url, sizeof(g_dawn_zip_url), g_dawn_tag, sizeof(g_dawn_tag))) {
        set_dawn_latest_tag(g_dawn_tag);
        return 1;
    }
    if (os_join(meta, sizeof(meta), work, "dawn-releases.html") &&
        curl_to_file(DAWN_RELEASES_HTML, meta, work) &&
        html_find_dawn_zip(meta, g_dawn_zip_url, sizeof(g_dawn_zip_url), g_dawn_tag, sizeof(g_dawn_tag))) {
        set_dawn_latest_tag(g_dawn_tag);
        return 1;
    }
    return 0;
}

static void
use_dawn_fallback_url(void)
{
    snprintf(g_dawn_zip_url, sizeof(g_dawn_zip_url), "%s", DAWN_FALLBACK_ZIP);
    if (g_dawn_tag[0] == '\0') {
        snprintf(g_dawn_tag, sizeof(g_dawn_tag), "0.1.3");
    }
}

static int
pe_is_amd64(const char *path)
{
    FILE *file;
    unsigned char buf[64];
    unsigned int pe;

    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    if (fread(buf, 1, sizeof(buf), file) != sizeof(buf)) {
        fclose(file);
        return 0;
    }
    fclose(file);
    if (buf[0] != 'M' || buf[1] != 'Z') {
        return 0;
    }
    pe = (unsigned int)buf[0x3c] | ((unsigned int)buf[0x3d] << 8) |
        ((unsigned int)buf[0x3e] << 16) | ((unsigned int)buf[0x3f] << 24);
    if (pe < 4 || pe > (1u << 20)) {
        return 0;
    }
    file = fopen(path, "rb");
    if (!file || fseek(file, (long)pe, SEEK_SET) != 0) {
        if (file) {
            fclose(file);
        }
        return 0;
    }
    if (fread(buf, 1, 6, file) != 6) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return buf[0] == 'P' && buf[1] == 'E' && buf[2] == 0 && buf[3] == 0 &&
        buf[4] == 0x64 && buf[5] == 0x86;
}

static void
repair_named_vc_dll(const char *dir, const char *name)
{
#ifdef _WIN32
    char local[MAX_PATH];
    char sysdir[MAX_PATH];
    char src[MAX_PATH];
    UINT n;

    if (!os_join(local, sizeof(local), dir, name) || !file_exists(local) || pe_is_amd64(local)) {
        return;
    }
    n = GetSystemDirectoryA(sysdir, (UINT)sizeof(sysdir));
    if (n == 0 || n >= sizeof(sysdir) || !os_join(src, sizeof(src), sysdir, name) || !pe_is_amd64(src)) {
        os_delete_file(local);
        return;
    }
    os_delete_file(local);
    os_copy_file(src, local);
#else
    (void)dir;
    (void)name;
#endif
}

static int
file_is_zero_head(const char *path)
{
    FILE *file;
    unsigned char buf[32];
    size_t n;
    size_t i;

    file = fopen(path, "rb");
    if (!file) {
        return 1;
    }
    n = fread(buf, 1, sizeof(buf), file);
    fclose(file);
    if (n == 0) {
        return 1;
    }
    for (i = 0; i < n; i++) {
        if (buf[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static int
sku_config_ok(const char *dir)
{
    char path[MAX_PATH];

    return os_join(path, sizeof(path), dir, "sku_config.txt") &&
        file_exists(path) &&
        os_file_size(path) > 0 &&
        !file_is_zero_head(path);
}

typedef struct ZeroPurge {
    const char *dir;
    FILE *list;
    int count;
} ZeroPurge;

static int
zero_purge_cb(const char *name, int is_dir, void *user)
{
    ZeroPurge *st = (ZeroPurge *)user;
    char path[MAX_PATH];

    if (!name || is_dir || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 1;
    }
    if (!os_join(path, sizeof(path), st->dir, name) || !file_is_zero_head(path)) {
        return 1;
    }
    if (st->list) {
        fprintf(st->list, "%s\n", name);
    }
    os_delete_file(path);
    st->count += 1;
    return 1;
}

static int
purge_zeroed_sidecars(const char *dir, const char *list_path)
{
    ZeroPurge st;
    FILE *list = NULL;

    if (!dir || !dir[0]) {
        return 0;
    }
    if (list_path && list_path[0]) {
        list = fopen(list_path, "wb");
    }
    st.dir = dir;
    st.list = list;
    st.count = 0;
    os_list_dir(dir, zero_purge_cb, &st);
    if (list) {
        if (!sku_config_ok(dir)) {
            fputs("sku_config.txt\n", list);
        }
        fclose(list);
    }
    return st.count;
}

static void
repair_vc_runtimes(const char *dir)
{
    static const char *names[] = {
        "vcruntime140.dll",
        "vcruntime140_1.dll",
        "msvcp140.dll",
        "msvcp140_1.dll",
        "msvcp140_2.dll",
        "concrt140.dll",
        "vccorlib140.dll",
        NULL
    };
    char bin[MAX_PATH];
    char nested[MAX_PATH];
    int i;

    if (!dir || !dir[0]) {
        return;
    }
    for (i = 0; names[i]; i++) {
        repair_named_vc_dll(dir, names[i]);
    }
    if (os_join(bin, sizeof(bin), dir, "bin") && os_join(nested, sizeof(nested), bin, "x64")) {
        for (i = 0; names[i]; i++) {
            repair_named_vc_dll(nested, names[i]);
        }
    }
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
    {
    char scripts[MAX_PATH];
    char src[MAX_PATH];
    if (os_join(path, sizeof(path), dir, "launch-destiny.sh")) {
        int copied = 0;

        if (g_root[0] &&
            os_join(scripts, sizeof(scripts), g_root, "scripts") &&
            os_join(src, sizeof(src), scripts, "launch-destiny.sh") &&
            file_exists(src) &&
            os_copy_file(src, path)) {
            copied = 1;
        }
        if (!copied) {
            file = fopen(path, "wb");
            if (file) {
                fputs(
                    "#!/usr/bin/env sh\n"
                    "set -e\n"
                    "GAME_DIR=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n"
                    "cd \"$GAME_DIR\"\n"
                    "export DAWN_FOREST_BASELINE=1\n"
                    "EXE=\"${DAWN_GAME_EXE:-$GAME_DIR/destiny2.exe}\"\n"
                    "if command -v steam-run >/dev/null 2>&1; then RUNNER=steam-run; else RUNNER=; fi\n"
                    "if command -v wine64 >/dev/null 2>&1; then\n"
                    "  if [ -n \"$RUNNER\" ]; then exec $RUNNER wine64 \"$EXE\" \"$@\"; fi\n"
                    "  exec wine64 \"$EXE\" \"$@\"\n"
                    "fi\n"
                    "if command -v wine >/dev/null 2>&1; then\n"
                    "  if [ -n \"$RUNNER\" ]; then exec $RUNNER wine \"$EXE\" \"$@\"; fi\n"
                    "  exec wine \"$EXE\" \"$@\"\n"
                    "fi\n"
                    "echo \"[ERROR] Wine was not found.\"\n"
                    "exit 1\n",
                    file
                );
                fclose(file);
            }
        }
        chmod(path, 0755);
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
            if (end) {
                size_t prefix = (size_t)(q + 1 - buf);
                size_t suffix = (size_t)((buf + size) - end);
                size_t lang_len = strlen(lang);
                size_t new_size = prefix + lang_len + suffix;
                char *out = (char *)malloc(new_size);
                if (out) {
                    memcpy(out, buf, prefix);
                    memcpy(out + prefix, lang, lang_len);
                    memcpy(out + prefix + lang_len, end, suffix);
                    file = fopen(settings, "wb");
                    if (file) {
                        fwrite(out, 1, new_size, file);
                        fclose(file);
                    }
                    free(out);
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
    mark_dawn_installed_dirty();
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

    if (!payload_looks_good(payload)) {
        return 0;
    }
    if (destiny2_running()) {
        fail_job("Close Destiny 2 first");
        return 0;
    }
    if (!game_root_ok(g_dir)) {
        fail_job("Not build 86657");
        return 0;
    }
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
    set_dawn_language(g_dir, lang_steam());
    repair_vc_runtimes(g_dir);
    remove_steam_appid(g_dir);
    purge_zeroed_sidecars(g_dir, NULL);
    return dawn_ready(g_dir);
}

static int
extract_dawn_zip(void)
{
    char work[MAX_PATH];
    char zip[MAX_PATH];
    char unpack[MAX_PATH];
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
    return find_dawn_payload(unpack, g_dawn_payload, sizeof(g_dawn_payload));
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
        fail_job("Dawn folder invalid");
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
    set_status("Downloading Dawn");
    if (!start_child(command, work, 0, "Dawn download failed")) {
        return 0;
    }
    return 1;
}

/*
 * DepotDownloader flags a depot as done in depot.config the moment its last
 * chunk lands, but the process itself can linger (final validation pass,
 * CDN teardown). Poll that file at most once a second instead of listing
 * thousands of package files every frame.
 */
static int
depots_finished_throttled(void)
{
    static uint32_t last_check;
    static int last_result;
    uint32_t now = os_tick_ms();

    if (last_check != 0 && (now - last_check) < 1000u) {
        return last_result;
    }
    last_check = now;
    last_result = depots_ready(g_dir);
    return last_result;
}

static void
finish_depot_from_files(void)
{
    int lang_only = g_lang_only;
    int launch = g_launch_after;

    stop_child_tree();
    close_child();
    g_busy = 0;
    g_session_job = 0;
    g_filelist[0] = '\0';
    g_depot_progress = 1.0f;
    g_lang_only = 0;
    g_launch_after = 0;
    write_marker(g_dir);
    set_dawn_language(g_dir, lang_steam());
    preserve_depot_cache();
    if (lang_only && dawn_ready(g_dir)) {
        g_phase = INSTALL_OK;
        g_installed = 1;
        g_user_start = 0;
        repair_vc_runtimes(g_dir);
        remove_steam_appid(g_dir);
        forget_depot_password();
        if (launch) {
            if (!launch_game_tracked()) {
                set_status("Dawn failed to start");
            }
        } else {
            set_status("Dawn is ready");
        }
        return;
    }
    if (dawn_ready(g_dir)) {
        complete_install();
        return;
    }
    if (!g_user_start) {
        g_busy = 0;
        g_phase = INSTALL_IDLE;
        set_status("Ready to install Dawn");
        return;
    }
    queue_dawn();
}

static void
queue_dawn(void)
{
    g_step = STEP_DAWN_RELEASE;
    g_busy = 1;
    g_phase = INSTALL_RUNNING;
    g_dawn_next = DAWN_WORK_START;
    set_status("Installing Dawn");
}

static void
advance_dawn(void)
{
    char payload[MAX_PATH];

    if (g_cancel) {
        g_dawn_next = DAWN_WORK_NONE;
        return;
    }
    switch (g_dawn_next) {
    case DAWN_WORK_START:
        set_status("Finding Dawn");
        g_dawn_next = DAWN_WORK_FIND;
        return;
    case DAWN_WORK_FIND:
        if (dawn_ready(g_dir)) {
            set_status("Installing Dawn");
            g_dawn_next = DAWN_WORK_FINISH;
            return;
        }
        if (fetch_dawn_from_github()) {
            g_dawn_next = DAWN_WORK_NONE;
            start_dawn_zip();
            return;
        }
        if (find_bundled_dawn(payload, sizeof(payload))) {
            snprintf(g_dawn_payload, sizeof(g_dawn_payload), "%s", payload);
            if (g_dawn_tag[0] == '\0') {
                snprintf(g_dawn_tag, sizeof(g_dawn_tag), "0.1.3");
            }
            set_status("Installing Dawn");
            g_dawn_next = DAWN_WORK_DEPLOY;
            return;
        }
        use_dawn_fallback_url();
        g_dawn_next = DAWN_WORK_NONE;
        start_dawn_zip();
        return;
    case DAWN_WORK_EXTRACT:
        set_status("Extracting Dawn");
        if (extract_dawn_zip()) {
            g_dawn_next = DAWN_WORK_DEPLOY;
            return;
        }
        if (find_bundled_dawn(payload, sizeof(payload))) {
            snprintf(g_dawn_payload, sizeof(g_dawn_payload), "%s", payload);
            g_dawn_next = DAWN_WORK_DEPLOY;
            return;
        }
        g_dawn_next = DAWN_WORK_NONE;
        fail_job("Dawn extract failed");
        return;
    case DAWN_WORK_DEPLOY:
        set_status("Installing Dawn");
        if (g_dawn_payload[0] && deploy_dawn(g_dawn_payload)) {
            g_dawn_next = DAWN_WORK_FINISH;
            return;
        }
        if (find_bundled_dawn(payload, sizeof(payload)) && deploy_dawn(payload)) {
            g_dawn_next = DAWN_WORK_FINISH;
            return;
        }
        g_dawn_next = DAWN_WORK_NONE;
        fail_job("Dawn install failed");
        return;
    case DAWN_WORK_FINISH:
        g_dawn_next = DAWN_WORK_NONE;
        complete_install();
        return;
    default:
        g_dawn_next = DAWN_WORK_NONE;
        break;
    }
}

static void
finish_child(DWORD exit_code)
{
    close_child();
    if (g_cancel) {
        g_cancel = 0;
        g_busy = 0;
        g_session_job = 0;
        g_verify = 0;
        g_need = INSTALL_NEED_NONE;
        g_phase = INSTALL_IDLE;
        set_status("Cancelled");
        forget_depot_password();
        return;
    }
    if (g_session_job || !g_user_start) {
        g_session_job = 0;
        g_busy = 0;
        g_setup_creds = 0;
        g_need = INSTALL_NEED_NONE;
        if (exit_code == 0) {
            g_session_ready = 1;
            set_status("Ready to download");
        } else {
            set_status("Steam login failed");
        }
        return;
    }
    if (exit_code != 0) {
        if (g_step == STEP_DEPOT_CONTENT &&
            (contains_ci(g_log, "AccessDenied") || contains_ci(g_log, "401"))) {
            fail_job("Depot access denied");
            return;
        }
        if (g_step == STEP_DEPOT_CONTENT && adopt_depot_root() && depots_ready(g_dir)) {
            finish_depot_from_files();
            return;
        }
        if (g_step == STEP_DAWN_RELEASE) {
            set_status("Installing Dawn");
            g_busy = 1;
            g_dawn_next = DAWN_WORK_EXTRACT;
            return;
        }
        fail_job("Download failed");
        return;
    }

    g_depot_progress = 1.0f;
    g_filelist[0] = '\0';
    if (g_step == STEP_DEPOT_CONTENT) {
        set_dawn_language(g_dir, lang_steam());
        write_marker(g_dir);
        preserve_depot_cache();
        if (g_lang_only) {
            int launch = g_launch_after;
            g_lang_only = 0;
            g_launch_after = 0;
            g_busy = 0;
            g_phase = INSTALL_OK;
            g_installed = dawn_ready(g_dir);
            repair_vc_runtimes(g_dir);
            remove_steam_appid(g_dir);
            if (launch) {
                if (!launch_game_tracked()) {
                    set_status("Dawn failed to start");
                }
            } else if (g_installed) {
                set_status("Dawn is ready");
            } else {
                queue_dawn();
            }
            return;
        }
        queue_dawn();
        return;
    }
    if (g_step == STEP_DAWN_RELEASE) {
        set_status("Extracting Dawn");
        g_busy = 1;
        g_dawn_next = DAWN_WORK_EXTRACT;
        return;
    }
    queue_dawn();
}

void
install_job_init(const char *project_root)
{
    memset(g_root, 0, sizeof(g_root));
    memset(g_dir, 0, sizeof(g_dir));
    memset(g_exe, 0, sizeof(g_exe));
    memset(g_user, 0, sizeof(g_user));
    memset(g_tool, 0, sizeof(g_tool));
    g_filelist[0] = '\0';
    forget_depot_password();
    g_status[0] = '\0';
    g_status_ms = 0;
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
    g_user_start = 0;
    g_cancel = 0;
    g_paused = 0;
    g_sim = 0;
    g_lang_only = 0;
    g_launch_after = 0;
    g_dawn_next = DAWN_WORK_NONE;
    g_remove_next = REMOVE_NONE;
    g_dawn_payload[0] = '\0';
    g_busy = 0;
    g_depot_progress = 0.0f;
    g_installed = 0;
#ifdef _WIN32
    g_process = NULL;
    g_stdout_read = NULL;
    g_stdin_write = NULL;
    g_game_proc = NULL;
    g_game_pid = 0;
#else
    g_process = 0;
    g_stdout_read = -1;
    g_stdin_write = -1;
    g_game_pid = 0;
#endif
    g_game_launch_ms = 0;
    g_game_state = 0;
    g_d2_running = 0;
    g_d2_running_ms = 0;
    g_d2_window = 0;
    g_d2_window_ms = 0;
    if (project_root && project_root[0]) {
        snprintf(g_root, sizeof(g_root), "%s", project_root);
    }
    g_dawn_zip_url[0] = '\0';
    g_dawn_tag[0] = '\0';
    g_dawn_installed[0] = '\0';
    g_dawn_latest[0] = '\0';
    g_dawn_latest_json[0] = '\0';
    g_dawn_latest_ms = 0;
    g_dawn_installed_dirty = 1;
    g_dawn_ver_pending = 0;
#ifdef _WIN32
    g_dawn_ver_proc = NULL;
#else
    g_dawn_ver_proc = 0;
#endif
    {
        char dawn[MAX_PATH];
        char cache[MAX_PATH];
        os_data_dir(dawn, sizeof(dawn));
        if (os_join(cache, sizeof(cache), dawn, "cache")) {
            os_mkdirs(cache);
            os_join(g_dawn_latest_json, sizeof(g_dawn_latest_json), cache, "dawn-latest.json");
        }
    }
    finish_dawn_latest_check();
    load_install_dir();
    refresh_dawn_installed();
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
#ifdef _WIN32
    if (g_dawn_ver_proc) {
        TerminateProcess(g_dawn_ver_proc, 1);
        close_dawn_ver_proc();
    }
#else
    if (g_dawn_ver_proc > 0) {
        kill(g_dawn_ver_proc, SIGTERM);
        close_dawn_ver_proc();
    }
#endif
    install_job_cancel();
    forget_depot_password();
}

static int
is_container_folder(const char *dir)
{
    const char *base = path_base(dir);

    return base &&
        (os_stricmp(base, "Documents") == 0 ||
         os_stricmp(base, "Desktop") == 0 ||
         os_stricmp(base, "Downloads") == 0 ||
         os_stricmp(base, "Users") == 0 ||
         os_stricmp(base, "home") == 0 ||
         os_stricmp(base, "Public") == 0);
}

static void
normalize_install_dir(void)
{
    char nested[MAX_PATH];
    size_t n;

    if (!g_dir[0]) {
        return;
    }
    n = strlen(g_dir);
    while (n > 3 && (g_dir[n - 1] == '/' || g_dir[n - 1] == '\\')) {
        g_dir[--n] = '\0';
    }
    if (os_stricmp(path_base(g_dir), "Dawn") == 0) {
        return;
    }
    {
        char data[MAX_PATH];
        os_data_dir(data, sizeof(data));
        if (data[0] && os_strnicmp(g_dir, data, strlen(data)) == 0) {
            if (os_stricmp(g_dir, data) == 0 && os_join(nested, sizeof(nested), data, "Destiny2")) {
                snprintf(g_dir, sizeof(g_dir), "%s", nested);
            }
            return;
        }
    }
    if (!is_container_folder(g_dir) && content_files_ready(g_dir)) {
        return;
    }
    if (os_join(nested, sizeof(nested), g_dir, "Dawn")) {
        snprintf(g_dir, sizeof(g_dir), "%s", nested);
    }
}

void
install_job_set_dir(const char *dir)
{
    if (g_busy) {
        set_status("Folder locked");
        return;
    }
    if (!dir || !dir[0]) {
        return;
    }
    snprintf(g_dir, sizeof(g_dir), "%s", dir);
    normalize_install_dir();
    g_dir_pinned = 1;
    os_mkdirs(g_dir);
    if (g_exe[0]) {
        char parent[MAX_PATH];
        if (!path_parent(parent, sizeof(parent), g_exe) || os_stricmp(parent, g_dir) != 0) {
            g_exe[0] = '\0';
        }
    }
    persist_dir();
    persist_exe();
    invalidate_install_ready();
    mark_dawn_installed_dirty();
    if (is_live_latest_d2(g_dir) && !dawn_depots_present(g_dir)) {
        set_status("Live D2 folder, not 86657");
        return;
    }
    if (install_complete(g_dir)) {
        g_installed = 1;
        set_status("Dawn is ready");
        return;
    }
    g_installed = 0;
    set_status("Folder set");
}

const char *
install_job_dir(void)
{
    return g_dir;
}

const char *
install_job_exe(void)
{
    static char fallback[MAX_PATH];

    if (g_exe[0]) {
        return g_exe;
    }
    if (g_dir[0] && os_join(fallback, sizeof(fallback), g_dir, "destiny2.exe") && file_exists(fallback)) {
        return fallback;
    }
    fallback[0] = '\0';
    return fallback;
}

void
install_job_set_exe(const char *path)
{
    char parent[MAX_PATH];

    if (g_busy) {
        set_status("Folder locked");
        return;
    }
    if (!path || !path[0]) {
        return;
    }
    snprintf(g_exe, sizeof(g_exe), "%s", path);
    if (path_parent(parent, sizeof(parent), g_exe) && parent[0]) {
        snprintf(g_dir, sizeof(g_dir), "%s", parent);
        normalize_install_dir();
        g_dir_pinned = 1;
        os_mkdirs(g_dir);
    }
    persist_dir();
    persist_exe();
    invalidate_install_ready();
    if (!file_exists(g_exe)) {
        set_status("EXE path saved");
        return;
    }
    if (is_live_latest_d2(g_dir) && !dawn_depots_present(g_dir)) {
        set_status("Live D2 folder, not 86657");
        return;
    }
    if (install_complete(g_dir)) {
        g_installed = 1;
        set_status("Dawn is ready");
        return;
    }
    g_installed = 0;
    set_status("Game EXE set");
}

void
install_job_set_user(const char *username)
{
    if (!username) {
        g_user[0] = '\0';
        return;
    }
    snprintf(g_user, sizeof(g_user), "%s", username);
    if (g_user[0]) {
        steam_auth_set_dd_user(g_user);
    }
}

void
install_job_submit_secret(const char *text)
{
    if (!text || !text[0]) {
        return;
    }
    if (g_need == INSTALL_NEED_ACCOUNT) {
        install_job_set_user(text);
        clear_secret();
        g_need = INSTALL_NEED_PASSWORD;
        set_status("Enter password");
        return;
    }
    snprintf(g_secret, sizeof(g_secret), "%s", text);
    if (g_setup_creds && !g_busy) {
        g_setup_creds = 0;
        g_need = INSTALL_NEED_NONE;
        bind_steam_identity();
        set_status("Ready to download");
        return;
    }
    if (g_busy && g_need != INSTALL_NEED_NONE) {
        send_secret();
    }
}

#if APP_DEV
static void
start_sim(void)
{
    g_sim = 1;
    g_paused = 0;
    g_cancel = 0;
    g_busy = 1;
    g_session_job = 0;
    g_verify = 0;
    g_user_start = 1;
    g_step = STEP_DEPOT_CONTENT;
    g_phase = INSTALL_RUNNING;
    g_need = INSTALL_NEED_NONE;
    if (g_depot_progress < 0.08f) {
        g_depot_progress = 0.14f;
    }
    set_status("Downloading game");
}

static void
poll_sim(void)
{
    static uint32_t last;
    static int last_pct = -1;
    uint32_t now;
    float dt;
    int pct;

    if (!g_sim || g_paused || !g_busy) {
        last = 0;
        return;
    }
    now = os_tick_ms();
    dt = last ? (float)(now - last) / 1000.0f : 0.04f;
    last = now;
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (dt > 0.2f) {
        dt = 0.2f;
    }
    g_depot_progress += dt * 0.055f;
    if (g_depot_progress > 0.86f) {
        g_depot_progress = 0.16f;
    }
    pct = (int)(g_depot_progress * 100.0f + 0.5f);
    if (pct != last_pct) {
        last_pct = pct;
        set_status("Downloading game");
    }
}
#endif

int
install_job_start(void)
{
    if (g_paused) {
        if (!require_licenses()) {
            return 0;
        }
        install_job_pause();
        return 1;
    }
    if (g_busy) {
        return 0;
    }
    if (!require_licenses()) {
        g_phase = INSTALL_FAILED;
        return 0;
    }
    g_user_start = 1;
    bind_steam_identity();
    normalize_install_dir();
    os_mkdirs(g_dir);
    persist_dir();
    if (g_dir[0] && is_live_latest_d2(g_dir) && !dawn_depots_present(g_dir)) {
        set_status("Live D2 folder, not 86657");
        g_phase = INSTALL_FAILED;
        invalidate_install_ready();
        return 0;
    }
    if (install_complete(g_dir)) {
        g_phase = INSTALL_OK;
        g_installed = 1;
        set_status("Already installed");
        return 1;
    }
    if (depots_ready(g_dir)) {
        finish_depot_from_files();
        return 1;
    }
    if (apply_cached_install()) {
        if (depots_ready(g_dir)) {
            finish_depot_from_files();
            return 1;
        }
        if (install_complete(g_dir)) {
            g_phase = INSTALL_OK;
            g_installed = 1;
            g_depot_progress = 1.0f;
            set_status("Using cached game files");
            return 1;
        }
        if (content_depot_present(g_dir)) {
            set_status("Using cached game files");
            if (start_missing_language(0)) {
                return 1;
            }
            set_status("Need Steam login for language");
            g_phase = INSTALL_FAILED;
            return 0;
        }
    }
    if (steam_auth_owns_d2() == 0) {
        set_status("No Destiny 2 license");
        g_phase = INSTALL_FAILED;
        return 0;
    }
    bind_steam_identity();
    if (g_user[0] == '\0') {
        set_status("No Steam username");
        g_phase = INSTALL_FAILED;
        return 0;
    }
    if (g_dir[0] == '\0') {
        set_status("No install folder");
        g_phase = INSTALL_FAILED;
        return 0;
    }
    if (!find_tool()) {
        set_status("DepotDownloader missing");
        g_phase = INSTALL_FAILED;
        return 0;
    }

    g_phase = INSTALL_RUNNING;
    g_filelist[0] = '\0';
    return start_depot();
}

void
install_job_cancel(void)
{
    if (g_remove_next) {
        return;
    }
    if (!g_busy && !g_paused && g_phase != INSTALL_RUNNING && g_need == INSTALL_NEED_NONE) {
        return;
    }
    g_cancel = 1;
    stop_child_tree();
    close_child();
    g_busy = 0;
    g_paused = 0;
    g_sim = 0;
    g_session_job = 0;
    g_user_start = 0;
    g_setup_creds = 0;
    g_verify = 0;
    g_lang_only = 0;
    g_launch_after = 0;
    g_dawn_next = DAWN_WORK_NONE;
    g_need = INSTALL_NEED_NONE;
    g_phase = INSTALL_IDLE;
    g_depot_progress = 0.0f;
    g_cancel = 0;
    invalidate_install_ready();
    set_status("Cancelled");
    forget_depot_password();
}

int
install_job_can_pause(void)
{
    if (g_remove_next || g_dawn_next != DAWN_WORK_NONE) {
        return 0;
    }
    if (g_session_job) {
        return 0;
    }
    if (g_paused || g_sim) {
        return 1;
    }
    return g_busy && g_step == STEP_DEPOT_CONTENT;
}

int
install_job_paused(void)
{
    return g_paused;
}

void
install_job_pause(void)
{
    if (g_paused) {
        g_paused = 0;
        g_cancel = 0;
        g_phase = INSTALL_RUNNING;
        g_user_start = 1;
        if (g_sim) {
            g_busy = 1;
            set_status("Downloading game");
            return;
        }
        {
            float keep = g_depot_progress;
            if (!find_tool()) {
                fail_job("DepotDownloader missing");
                return;
            }
            if (!start_depot()) {
                return;
            }
            if (keep > g_depot_progress) {
                g_depot_progress = keep;
            }
        }
        return;
    }
    if (!install_job_can_pause()) {
        return;
    }
    g_paused = 1;
    g_cancel = 1;
    if (!g_sim) {
        stop_child_tree();
        close_child();
    }
    g_busy = 0;
    g_cancel = 0;
    g_phase = INSTALL_RUNNING;
    set_status("Paused");
}

int
install_job_can_simulate(void)
{
#if !APP_DEV
    return 0;
#else
    if (steam_auth_play_block()) {
        return 0;
    }
    if (g_busy || g_paused || g_dawn_next != DAWN_WORK_NONE || g_remove_next) {
        return 0;
    }
    return 1;
#endif
}

void
install_job_simulate(void)
{
#if APP_DEV
    if (!install_job_can_simulate()) {
        return;
    }
    close_child();
    start_sim();
#endif
}

void
install_job_poll(void)
{
    poll_dawn_release_version();
    poll_game();
    expire_hold_status();
#if APP_DEV
    if (g_sim) {
        poll_sim();
        return;
    }
#endif
    if (g_remove_next) {
        apply_remove();
        return;
    }
#ifdef _WIN32
    if (g_dawn_next && !(g_busy && g_process)) {
#else
    if (g_dawn_next && !(g_busy && g_process > 0)) {
#endif
        static uint32_t last_dawn;
        uint32_t now = os_tick_ms();
        if (last_dawn != 0 && (now - last_dawn) < 180u) {
            return;
        }
        last_dawn = now;
        advance_dawn();
        if (!g_dawn_next) {
            last_dawn = 0;
        }
        return;
    }
#ifdef _WIN32
    if (!g_busy || !g_process) {
#else
    if (!g_busy || g_process <= 0) {
#endif
        static uint32_t last_idle;
        uint32_t now = os_tick_ms();
        if (last_idle != 0 && (now - last_idle) < 50u) {
            return;
        }
        last_idle = now;
        bind_steam_identity();
        if (steam_auth_consume_fresh_login()) {
            if (needs_depot_download()) {
                begin_depot_login();
            } else {
                g_setup_creds = 0;
                g_need = INSTALL_NEED_NONE;
            }
        }
        maybe_prepare_session();
        if (g_phase == INSTALL_RUNNING &&
            g_user_start &&
            !g_paused &&
            !g_sim &&
            !g_dawn_next &&
            !g_remove_next &&
            !g_verify &&
            depots_finished_throttled() &&
            !dawn_ready(g_dir)) {
            finish_depot_from_files();
        }
        return;
    }
    bind_steam_identity();

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
        if (g_step == STEP_DEPOT_CONTENT && !g_session_job && !g_verify && depots_finished_throttled()) {
            finish_depot_from_files();
        }
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
        if (g_step == STEP_DEPOT_CONTENT && !g_session_job && !g_verify && depots_finished_throttled()) {
            finish_depot_from_files();
        }
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
    return g_busy || g_paused || g_dawn_next != DAWN_WORK_NONE || g_remove_next != REMOVE_NONE;
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
    if (g_step == STEP_DAWN_RELEASE) {
        return 0.97f + local * 0.03f;
    }
    return local * 0.97f;
}

const char *
install_job_status(void)
{
    expire_hold_status();
    return g_status;
}

int
install_job_ready(void)
{
    char root[MAX_PATH];
    uint32_t now;

    if (g_busy || g_dawn_next || g_remove_next) {
        return 0;
    }
    now = os_tick_ms();
    if (g_installed_check != 0 && (now - g_installed_check) < 4000u) {
        return g_installed;
    }
    g_installed_check = now;
    if (g_dir[0] && install_complete(g_dir)) {
        g_installed = 1;
        return 1;
    }
    if (g_dir[0] && game_root_in(g_dir, root, sizeof(root)) && install_complete(root)) {
        if (os_stricmp(g_dir, root) != 0) {
            snprintf(g_dir, sizeof(g_dir), "%s", root);
            persist_dir();
        }
        g_installed = 1;
        return 1;
    }
    /* The disk walk visits up to WALK_BUDGET folders on the UI thread; run it
     * once and then only after something invalidated the result (folder
     * change, cancel, uninstall) or a long while later. */
    if (g_scan_check != 0 && (now - g_scan_check) < 300000u) {
        g_installed = 0;
        return 0;
    }
    g_scan_check = now;
    if (refresh_installed() && install_complete(g_dir)) {
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
    const char *block = steam_auth_play_block();

    if (block) {
        set_status(block);
        return 0;
    }

    repair_vc_runtimes(g_dir);
    remove_steam_appid(g_dir);
    preserve_depot_cache();
    purge_zeroed_sidecars(g_dir, NULL);
    set_dawn_language(g_dir, lang_steam());
    if (!language_depot_present(g_dir)) {
        if (g_busy) {
            set_status("Downloading language");
            return 0;
        }
        if (start_missing_language(1)) {
            return 1;
        }
        set_status("Need language depot");
        return 0;
    }
    if (!sku_config_ok(g_dir)) {
        if (g_busy) {
            set_status("Repairing game files");
            return 0;
        }
        if (!start_depot_repair()) {
            set_status("Game files incomplete");
            return 0;
        }
        return 0;
    }
    if (!find_game_exe(exe, sizeof(exe))) {
        if (!g_installed) {
            set_status("Dawn not installed");
            return 0;
        }
        snprintf(exe, sizeof(exe), "%s", g_dir);
    }
    if (!launch_game_tracked()) {
        set_status("Dawn failed to start");
        return 0;
    }
    return 1;
}

int
install_job_game_state(void)
{
    return g_game_state;
}

void
install_job_game_stop(void)
{
    kill_destiny2();
    clear_game_proc();
    invalidate_game_scan();
    g_game_state = 0;
    set_status("Stopped");
}

void
install_job_set_language(const char *steam)
{
    g_lang = lang_by_steam(steam);
    persist_lang();
}

const char *
install_job_language(void)
{
    return lang_steam();
}

const char *
install_job_language_label(void)
{
    return game_language()->label;
}

const char *
install_job_dawn_version(void)
{
    return g_dawn_installed;
}

const char *
install_job_dawn_latest(void)
{
    return g_dawn_latest;
}

static void
wipe_dd_store_dir(const char *dir, int depth)
{
    WalkKids kids;
    int i;

    if (depth > 10 || !dir || !dir[0] || !os_dir_exists(dir)) {
        return;
    }
    kids.count = 0;
    snprintf(kids.parent, sizeof(kids.parent), "%s", dir);
    os_list_dir(dir, collect_wipe_cb, &kids);
    for (i = 0; i < kids.count; i++) {
        if (kids.is_dir[i]) {
            wipe_dd_store_dir(kids.path[i], depth + 1);
        } else if (os_stricmp(kids.name[i], "account.config") == 0) {
            os_delete_file(kids.path[i]);
        }
    }
}

static void
wipe_dd_account_store(void)
{
    char root[MAX_PATH];
    char file[MAX_PATH];

#ifdef _WIN32
    const char *local = getenv("LOCALAPPDATA");
    const char *roam = getenv("APPDATA");
    if (local && local[0] && os_join(root, sizeof(root), local, "IsolatedStorage")) {
        wipe_dd_store_dir(root, 0);
    }
    if (roam && roam[0] && os_join(root, sizeof(root), roam, "IsolatedStorage")) {
        wipe_dd_store_dir(root, 0);
    }
#else
    const char *home = getenv("HOME");
    if (home && home[0]) {
        if (os_join(root, sizeof(root), home, ".local/share/IsolatedStorage")) {
            wipe_dd_store_dir(root, 0);
        }
        if (os_join(root, sizeof(root), home, ".isolated-storage")) {
            wipe_dd_store_dir(root, 0);
        }
    }
#endif
    depot_work_dir(root, MAX_PATH);
    if (os_join(file, sizeof(file), root, "account.config")) {
        os_delete_file(file);
    }
}

static void
forget_depot_password(void)
{
    clear_secret();
    g_session_ready = 0;
    g_session_tried = 0;
    wipe_dd_account_store();
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
            } else {
#ifdef _WIN32
                {
                    DWORD attr = GetFileAttributesA(kids.path[i]);
                    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY)) {
                        SetFileAttributesA(kids.path[i], attr & ~FILE_ATTRIBUTE_READONLY);
                    }
                }
#endif
                if (!os_delete_file(kids.path[i])) {
                    ok = 0;
                }
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

static int
dir_has_child(const char *dir, const char *name)
{
    char path[MAX_PATH];

    return dir && name && os_join(path, sizeof(path), dir, name) &&
        (os_dir_exists(path) || file_exists(path));
}

static int
dll_named(const char *dir, const char *product)
{
    char path[MAX_PATH];
    uint64_t size;

    if (!dawn_dll_path(dir, path, sizeof(path))) {
        return 0;
    }
#ifdef _WIN32
    (void)size;
    return dll_product_is(path, product);
#else
    size = os_file_size(path);
    if (os_stricmp(product, "Dawn") == 0) {
        return size > 8ull * 1024ull * 1024ull && size < 50ull * 1024ull * 1024ull;
    }
    if (os_stricmp(product, "Sunrise") == 0) {
        return size >= 50ull * 1024ull * 1024ull;
    }
    return 0;
#endif
}

static int
dawn_present(const char *dir)
{
    char settings[MAX_PATH];
    char nested[MAX_PATH];

    if (!dir || !dir[0]) {
        return 0;
    }
    if (dawn_settings_in(dir, settings, sizeof(settings))) {
        return 1;
    }
    if (dir_has_child(dir, "Dawn") ||
        dir_has_child(dir, ".dawn") ||
        dir_has_child(dir, "release.json") ||
        dir_has_child(dir, "launch-destiny.cmd") ||
        dir_has_child(dir, "launch-destiny.sh")) {
        return 1;
    }
    if (join4(nested, sizeof(nested), dir, "bin", "x64", "Dawn") && os_dir_exists(nested)) {
        return 1;
    }
    return dll_named(dir, "Dawn");
}

static int
sunrise_present(const char *dir)
{
    char nested[MAX_PATH];

    if (!dir || !dir[0]) {
        return 0;
    }
    if (dir_has_child(dir, "Sunrise") || dir_has_child(dir, ".sunrise")) {
        return 1;
    }
    if (join4(nested, sizeof(nested), dir, "bin", "x64", "Sunrise") && os_dir_exists(nested)) {
        return 1;
    }
    return dll_named(dir, "Sunrise");
}

static const char *
parts_dir(char *root, size_t max)
{
    if (g_dir[0] && game_root_in(g_dir, root, max)) {
        return root;
    }
    return g_dir;
}

int
install_job_parts(void)
{
    char root[MAX_PATH];
    const char *dir;
    int parts = 0;

    if (!g_dir[0] || !os_dir_exists(g_dir)) {
        return 0;
    }
    dir = parts_dir(root, sizeof(root));
    if (depots_ready(dir) || content_depot_present(dir) || content_files_ready(dir) ||
        game_root_in(dir, NULL, 0)) {
        parts |= INSTALL_PART_DEPOTS;
    }
    if (dawn_present(dir)) {
        parts |= INSTALL_PART_DAWN;
    }
    if (sunrise_present(dir)) {
        parts |= INSTALL_PART_SUNRISE;
    }
    return parts;
}

static int
wipe_child(const char *dir, const char *name)
{
    char path[MAX_PATH];

    if (!dir || !name || !os_join(path, sizeof(path), dir, name)) {
        return 0;
    }
    if (os_dir_exists(path)) {
        return wipe_tree(path);
    }
    if (file_exists(path)) {
#ifdef _WIN32
        {
            DWORD attr = GetFileAttributesA(path);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY)) {
                SetFileAttributesA(path, attr & ~FILE_ATTRIBUTE_READONLY);
            }
        }
#endif
        return os_delete_file(path);
    }
    return 1;
}

static void
stop_auto_dawn(void)
{
    g_user_start = 0;
    g_dawn_next = DAWN_WORK_NONE;
    g_dawn_payload[0] = '\0';
    g_verify = 0;
    g_lang_only = 0;
    g_launch_after = 0;
    g_session_job = 0;
    g_phase = INSTALL_IDLE;
}

static void
restore_steam_overlay(const char *dir)
{
    char mid[MAX_PATH];
    char backup_dir[MAX_PATH];
    char backup[MAX_PATH];
    char dest[MAX_PATH];
    char root_dll[MAX_PATH];
    char cache[MAX_PATH];

    if (os_join(root_dll, sizeof(root_dll), dir, "steam_api64.dll") && file_exists(root_dll)) {
        os_delete_file(root_dll);
    }
    backup[0] = '\0';
    if (os_join(mid, sizeof(mid), dir, ".dawn") &&
        os_join(backup_dir, sizeof(backup_dir), mid, "backup") &&
        os_join(backup, sizeof(backup), backup_dir, "steam_api64.dll") &&
        file_exists(backup)) {
        /* keep */
    } else {
        backup[0] = '\0';
        depot_cache_dir(cache, sizeof(cache));
        if (cache[0] && steam_dll_path(cache, backup, sizeof(backup)) &&
            file_exists(backup) && os_file_size(backup) < (1ull << 20)) {
            /* stock DLL from depot cache */
        } else {
            backup[0] = '\0';
        }
    }
    if (backup[0] && steam_dll_path(dir, dest, sizeof(dest))) {
        char parent[MAX_PATH];

        if (path_parent(parent, sizeof(parent), dest)) {
            os_mkdirs(parent);
        }
        os_copy_file(backup, dest);
    }
}

static int
uninstall_dawn_only(const char *dir)
{
    char bin[MAX_PATH];
    char x64[MAX_PATH];

    if (!dll_named(dir, "Sunrise")) {
        restore_steam_overlay(dir);
    }
    wipe_child(dir, "Dawn");
    if (os_join(bin, sizeof(bin), dir, "bin") && os_join(x64, sizeof(x64), bin, "x64")) {
        wipe_child(x64, "Dawn");
    }
    wipe_child(dir, "launch-destiny.cmd");
    wipe_child(dir, "launch-destiny.sh");
    wipe_child(dir, "release.json");
    wipe_child(dir, ".dawn");
    mark_dawn_installed_dirty();
    return 1;
}

static int
uninstall_sunrise_only(const char *dir)
{
    char bin[MAX_PATH];
    char x64[MAX_PATH];

    if (dll_named(dir, "Sunrise")) {
        restore_steam_overlay(dir);
    }
    wipe_child(dir, "Sunrise");
    if (os_join(bin, sizeof(bin), dir, "bin") && os_join(x64, sizeof(x64), bin, "x64")) {
        wipe_child(x64, "Sunrise");
    }
    wipe_child(dir, ".sunrise");
    return 1;
}

static int
uninstall_allowed(void)
{
    size_t n;

    if (g_busy) {
        set_status("Busy, can't uninstall");
        return 0;
    }
    if (destiny2_running()) {
        set_status("Close Destiny 2 first");
        return 0;
    }
    if (!g_dir[0] || !os_dir_exists(g_dir)) {
        set_status("Nothing to uninstall");
        return 0;
    }
    n = strlen(g_dir);
    if (n < 6) {
        set_status("Folder looks unsafe");
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
            set_status("Folder looks unsafe");
            return 0;
        }
    }
    if (looks_like_steam_common(g_dir) && !marker_matches(g_dir) && !dawn_depots_present(g_dir)) {
        set_status("Won't delete Steam install");
        return 0;
    }
    return 1;
}

static int
queue_remove(int kind, const char *status)
{
    if (!uninstall_allowed()) {
        return 0;
    }
    stop_auto_dawn();
    g_remove_next = kind;
    g_busy = 1;
    set_status(status);
    return 1;
}

static void
apply_remove(void)
{
    char root[MAX_PATH];
    const char *dir;
    int kind = g_remove_next;

    g_remove_next = REMOVE_NONE;
    stop_auto_dawn();
    dir = parts_dir(root, sizeof(root));
    if (kind == REMOVE_DAWN) {
        uninstall_dawn_only(dir);
        if (dawn_present(dir)) {
            uninstall_dawn_only(dir);
        }
        invalidate_install_ready();
        persist_dir();
        g_busy = 0;
        g_phase = INSTALL_IDLE;
        set_status(dawn_present(dir) ? "Could not remove Dawn" : "Ready to install Dawn");
        return;
    }
    if (kind == REMOVE_SUNRISE) {
        uninstall_sunrise_only(dir);
        if (sunrise_present(dir)) {
            uninstall_sunrise_only(dir);
        }
        invalidate_install_ready();
        persist_dir();
        g_busy = 0;
        g_phase = INSTALL_IDLE;
        if (sunrise_present(dir)) {
            set_status("Could not remove Sunrise");
        } else if (dawn_ready(dir)) {
            set_status("Dawn is ready");
        } else {
            set_status("Ready to install Dawn");
        }
        return;
    }
    preserve_depot_cache();
    wipe_tree(g_dir);
    os_mkdirs(g_dir);
    invalidate_install_ready();
    persist_dir();
    g_busy = 0;
    g_phase = INSTALL_IDLE;
    set_status("Folder removed");
}

int
install_job_uninstall(void)
{
    if (!uninstall_allowed()) {
        return 0;
    }
    if (!game_root_in(g_dir, NULL, 0) &&
        !dawn_present(g_dir) &&
        !sunrise_present(g_dir) &&
        !steam_dll_present(g_dir)) {
        set_status("No game files here");
        return 0;
    }
    return queue_remove(REMOVE_FULL, "Removing game files");
}

int
install_job_uninstall_part(int part)
{
    char root[MAX_PATH];
    const char *dir;

    if (part != INSTALL_PART_DAWN && part != INSTALL_PART_SUNRISE) {
        return install_job_uninstall();
    }
    if (!uninstall_allowed()) {
        return 0;
    }
    dir = parts_dir(root, sizeof(root));
    if (part == INSTALL_PART_DAWN) {
        if (!dawn_present(dir)) {
            set_status("Nothing to uninstall");
            return 0;
        }
        return queue_remove(REMOVE_DAWN, "Removing Dawn Mod");
    }
    if (!sunrise_present(dir)) {
        set_status("Nothing to uninstall");
        return 0;
    }
    return queue_remove(REMOVE_SUNRISE, "Removing Sunrise Mod");
}

int
install_job_verify(void)
{
    if (g_busy) {
        return 0;
    }
    if (g_dir[0] == '\0') {
        set_status("No install folder");
        return 0;
    }
    if (is_live_latest_d2(g_dir)) {
        set_status("Live D2 folder, not 86657");
        return 0;
    }
    if (!find_tool()) {
        set_status("DepotDownloader missing");
        return 0;
    }
    if (!require_licenses()) {
        return 0;
    }
    bind_steam_identity();
    if (g_user[0] == '\0') {
        set_status("No Steam username");
        return 0;
    }
    g_verify = 1;
    g_phase = INSTALL_RUNNING;
    g_filelist[0] = '\0';
    return start_depot();
}
