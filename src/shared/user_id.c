#include "shared/user_id.h"
#include "shared/os.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#define UID_PREFIX "Usr"
#define UID_HEX 16
#define UID_LEN 19

static char g_uid[32];
static int g_ready;

static uint64_t
mix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static int
uid_valid(const char *text)
{
    int i;

    if (!text || (int)strlen(text) != UID_LEN) {
        return 0;
    }
    if (strncmp(text, UID_PREFIX, 3) != 0) {
        return 0;
    }
    for (i = 3; i < UID_LEN; i++) {
        if (!isxdigit((unsigned char)text[i])) {
            return 0;
        }
    }
    return 1;
}

static void
uid_path(char *out, size_t max)
{
    char dawn[MAX_PATH];

    os_data_dir(dawn, sizeof(dawn));
    os_join(out, max, dawn, "uid.txt");
}

static int
fill_random(unsigned char *out, int n)
{
    int i;

    if (!out || n <= 0) {
        return 0;
    }
#ifdef _WIN32
    {
        uint64_t seed = mix64(((uint64_t)os_tick_ms() << 32) ^ (uint64_t)(uintptr_t)out);
        seed ^= mix64((uint64_t)GetCurrentProcessId() * 0xD1B54A32D192ED03ull);
        {
            LARGE_INTEGER qpc;
            if (QueryPerformanceCounter(&qpc)) {
                seed ^= mix64((uint64_t)qpc.QuadPart);
            }
        }
        for (i = 0; i < n; i += 8) {
            uint64_t v = mix64(seed + (uint64_t)i * 0x9E3779B97F4A7C15ull);
            seed = v;
            memcpy(out + i, &v, (size_t)((n - i) < 8 ? (n - i) : 8));
        }
        return 1;
    }
#else
    {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd >= 0) {
            ssize_t got = read(fd, out, (size_t)n);
            close(fd);
            if (got == n) {
                return 1;
            }
        }
        uint64_t seed = mix64(((uint64_t)os_tick_ms() << 32) ^ (uint64_t)getpid());
        seed ^= mix64((uint64_t)(uintptr_t)out);
        for (i = 0; i < n; i += 8) {
            uint64_t v = mix64(seed + (uint64_t)i);
            seed = v;
            memcpy(out + i, &v, (size_t)((n - i) < 8 ? (n - i) : 8));
        }
        return 1;
    }
#endif
}

static int
load_uid(void)
{
    char path[MAX_PATH];
    FILE *file;
    char line[64];
    size_t n;

    uid_path(path, sizeof(path));
    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    n = fread(line, 1, sizeof(line) - 1, file);
    fclose(file);
    line[n] = '\0';
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) {
        line[--n] = '\0';
    }
    if (!uid_valid(line)) {
        return 0;
    }
    memcpy(g_uid, line, UID_LEN);
    g_uid[UID_LEN] = '\0';
    return 1;
}

static void
save_uid(void)
{
    char path[MAX_PATH];
    FILE *file;

    uid_path(path, sizeof(path));
    file = fopen(path, "wb");
    if (!file) {
        return;
    }
    fwrite(g_uid, 1, strlen(g_uid), file);
    fputc('\n', file);
    fclose(file);
}

static void
make_uid(void)
{
    unsigned char bytes[8];
    int i;

    memset(bytes, 0, sizeof(bytes));
    fill_random(bytes, (int)sizeof(bytes));
    memcpy(g_uid, UID_PREFIX, 3);
    for (i = 0; i < 8; i++) {
        snprintf(g_uid + 3 + i * 2, 3, "%02X", bytes[i]);
    }
    g_uid[UID_LEN] = '\0';
    save_uid();
}

const char *
user_id_get(void)
{
    if (g_ready && uid_valid(g_uid)) {
        return g_uid;
    }
    g_uid[0] = '\0';
    if (!load_uid()) {
        make_uid();
    }
    g_ready = uid_valid(g_uid);
    return g_uid;
}

int
user_id_copy(void)
{
    const char *id = user_id_get();
    return id && id[0] && os_clipboard_set(id);
}
