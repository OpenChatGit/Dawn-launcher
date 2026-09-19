#include "shared/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <shellapi.h>
#else
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

void
os_init(void)
{
}

uint32_t
os_tick_ms(void)
{
#ifdef _WIN32
    return GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000u));
#endif
}

void
os_sleep_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

int
os_join(char *out, size_t max, const char *dir, const char *name)
{
    if (!out || !dir || !name || max < 4) {
        return 0;
    }
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    int need_sep = dir_len > 0 && dir[dir_len - 1] != '/' && dir[dir_len - 1] != '\\';
    size_t total = dir_len + (need_sep ? 1 : 0) + name_len + 1;
    if (total > max) {
        return 0;
    }
    memcpy(out, dir, dir_len);
    size_t n = dir_len;
    if (need_sep) {
        out[n++] = OS_SEP;
    }
    memcpy(out + n, name, name_len + 1);
    return 1;
}

int
os_mkdirs(const char *path)
{
    char buf[MAX_PATH];
    size_t i;

    if (!path || !path[0]) {
        return 0;
    }
    snprintf(buf, sizeof(buf), "%s", path);
    for (i = 1; buf[i]; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char save = buf[i];
            buf[i] = '\0';
#ifdef _WIN32
            CreateDirectoryA(buf, NULL);
#else
            mkdir(buf, 0755);
#endif
            buf[i] = save;
        }
    }
#ifdef _WIN32
    return CreateDirectoryA(buf, NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS || os_dir_exists(buf);
#else
    return mkdir(buf, 0755) == 0 || errno == EEXIST;
#endif
}

int
os_file_exists(const char *path)
{
    if (!path || !path[0]) {
        return 0;
    }
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

uint64_t
os_file_size(const char *path)
{
    if (!path || !path[0]) {
        return 0;
    }
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) {
        return 0;
    }
    return ((uint64_t)info.nFileSizeHigh << 32) | (uint64_t)info.nFileSizeLow;
#else
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    return (uint64_t)st.st_size;
#endif
}

int
os_dir_exists(const char *path)
{
    if (!path || !path[0]) {
        return 0;
    }
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

int
os_copy_file(const char *from, const char *to)
{
#ifdef _WIN32
    return CopyFileA(from, to, FALSE) != 0;
#else
    FILE *in = fopen(from, "rb");
    if (!in) {
        return 0;
    }
    FILE *out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    char buf[8192];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = 0;
            break;
        }
    }
    fclose(out);
    fclose(in);
    return ok;
#endif
}

int
os_delete_file(const char *path)
{
#ifdef _WIN32
    return DeleteFileA(path) != 0;
#else
    return unlink(path) == 0;
#endif
}

uint64_t
os_file_mtime(const char *path)
{
#ifdef _WIN32
    HANDLE file = CreateFileA(
        path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    FILETIME time;
    int ok = GetFileTime(file, NULL, NULL, &time);
    CloseHandle(file);
    if (!ok) {
        return 0;
    }
    return ((uint64_t)time.dwHighDateTime << 32) | time.dwLowDateTime;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return ((uint64_t)st.st_mtime << 32) | (uint64_t)st.st_mtime;
#endif
}

void
os_data_dir(char *out, size_t max)
{
    if (!out || max < 8) {
        return;
    }
#ifdef _WIN32
    char local[MAX_PATH];
    if (!GetEnvironmentVariableA("LOCALAPPDATA", local, MAX_PATH)) {
        snprintf(local, sizeof(local), ".");
    }
    snprintf(out, max, "%s\\Dawn", local);
    CreateDirectoryA(out, NULL);
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0]) {
        snprintf(out, max, "%s/Dawn", xdg);
    } else if (home && home[0]) {
        snprintf(out, max, "%s/.local/share/Dawn", home);
    } else {
        snprintf(out, max, "./Dawn");
    }
    os_mkdirs(out);
#endif
}

static void
os_trim_leaf(char *path)
{
    size_t n;

    if (!path) {
        return;
    }
    n = strlen(path);
    while (n > 1 && (path[n - 1] == '/' || path[n - 1] == '\\')) {
        path[--n] = '\0';
    }
    while (n > 0 && path[n - 1] != '/' && path[n - 1] != '\\') {
        path[--n] = '\0';
    }
    if (n > 1 && (path[n - 1] == '/' || path[n - 1] == '\\')) {
        path[n - 1] = '\0';
    }
}

int
os_exe_dir(char *out, size_t max)
{
    char exe[MAX_PATH];

    if (!out || max < 2) {
        return 0;
    }
#ifdef _WIN32
    {
        DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
        if (n == 0 || n >= (DWORD)sizeof(exe)) {
            return 0;
        }
    }
#else
    {
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n <= 0) {
            return 0;
        }
        exe[n] = '\0';
    }
#endif
    os_trim_leaf(exe);
    snprintf(out, max, "%s", exe);
    return out[0] != '\0';
}

int
os_app_root(char *out, size_t max, const char *fallback)
{
    char exe[MAX_PATH];
    char themes[MAX_PATH];
    char parent[MAX_PATH];

    if (!out || max < 2) {
        return 0;
    }
    if (!os_exe_dir(exe, sizeof(exe))) {
        snprintf(out, max, "%s", fallback && fallback[0] ? fallback : ".");
        return 1;
    }
    if (os_join(themes, sizeof(themes), exe, "themes") && os_dir_exists(themes)) {
        snprintf(out, max, "%s", exe);
        return 1;
    }
    snprintf(parent, sizeof(parent), "%s", exe);
    os_trim_leaf(parent);
    if (parent[0] && os_join(themes, sizeof(themes), parent, "themes") && os_dir_exists(themes)) {
        snprintf(out, max, "%s", parent);
        return 1;
    }
    snprintf(out, max, "%s", fallback && fallback[0] ? fallback : exe);
    return 1;
}

int
os_utf8_to_wide(const char *utf8, wchar_t *out, int max)
{
    if (!out || max < 1) {
        return 0;
    }
    out[0] = 0;
    if (!utf8) {
        return 0;
    }
#ifdef _WIN32
    return MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, max);
#else
    int n = 0;
    const unsigned char *p = (const unsigned char *)utf8;
    while (*p && n + 1 < max) {
        unsigned int cp = 0;
        if (*p < 0x80) {
            cp = *p++;
        } else if ((*p & 0xe0) == 0xc0 && p[1]) {
            cp = ((unsigned int)(p[0] & 0x1f) << 6) | (p[1] & 0x3f);
            p += 2;
        } else if ((*p & 0xf0) == 0xe0 && p[1] && p[2]) {
            cp = ((unsigned int)(p[0] & 0x0f) << 12) | ((unsigned int)(p[1] & 0x3f) << 6) | (p[2] & 0x3f);
            p += 3;
        } else if ((*p & 0xf8) == 0xf0 && p[1] && p[2] && p[3]) {
            cp = ((unsigned int)(p[0] & 0x07) << 18) | ((unsigned int)(p[1] & 0x3f) << 12) |
                ((unsigned int)(p[2] & 0x3f) << 6) | (p[3] & 0x3f);
            p += 4;
        } else {
            p++;
            continue;
        }
        out[n++] = (wchar_t)cp;
    }
    out[n] = 0;
    return n + 1;
#endif
}

void
os_open_url(const char *url)
{
    if (!url || !url[0]) {
        return;
    }
#ifdef _WIN32
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
    pid_t pid = fork();
    if (pid == 0) {
        if (fork() == 0) {
            execlp("xdg-open", "xdg-open", url, (char *)NULL);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0) {
        waitpid(pid, NULL, 0);
    }
#endif
}

int
os_launch(const char *path)
{
    char dir[MAX_PATH];
    size_t i;

    if (!path || !path[0] || !os_file_exists(path)) {
        return 0;
    }
    snprintf(dir, sizeof(dir), "%s", path);
    for (i = strlen(dir); i > 0; i--) {
        if (dir[i - 1] == '/' || dir[i - 1] == '\\') {
            dir[i - 1] = '\0';
            break;
        }
    }
#ifdef _WIN32
    {
        INT_PTR rc = (INT_PTR)ShellExecuteA(NULL, "open", path, NULL, dir[0] ? dir : NULL, SW_SHOWNORMAL);
        return rc > 32;
    }
#else
    pid_t pid = fork();
    if (pid < 0) {
        return 0;
    }
    if (pid == 0) {
        if (dir[0] && chdir(dir) != 0) {
            _exit(1);
        }
        execlp("wine", "wine", path, (char *)NULL);
        execl(path, path, (char *)NULL);
        _exit(127);
    }
    return 1;
#endif
}

int
os_list_dir(const char *path, os_dir_cb cb, void *user)
{
    if (!path || !cb) {
        return 0;
    }
#ifdef _WIN32
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA find;
    HANDLE handle = FindFirstFileA(pattern, &find);
    if (handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    int count = 0;
    do {
        if (strcmp(find.cFileName, ".") == 0 || strcmp(find.cFileName, "..") == 0) {
            continue;
        }
        int is_dir = (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (cb(find.cFileName, is_dir, user) == 0) {
            break;
        }
        count += 1;
    } while (FindNextFileA(handle, &find));
    FindClose(handle);
    return count;
#else
    DIR *dir = opendir(path);
    if (!dir) {
        return 0;
    }
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
        struct stat st;
        int is_dir = 0;
        if (stat(full, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
        }
        if (cb(ent->d_name, is_dir, user) == 0) {
            break;
        }
        count += 1;
    }
    closedir(dir);
    return count;
#endif
}
