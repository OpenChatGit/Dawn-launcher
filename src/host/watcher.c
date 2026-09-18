#include "watcher.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

#define WATCHER_COUNT 2
#define WATCHER_BUFFER_SIZE 32768

typedef struct WatchSlot {
    HANDLE dir;
    HANDLE event;
    OVERLAPPED overlapped;
    unsigned char buffer[WATCHER_BUFFER_SIZE];
    int pending;
} WatchSlot;

static WatchSlot g_slots[WATCHER_COUNT];
static int g_ready;

static int
start_read(WatchSlot *slot)
{
    memset(&slot->overlapped, 0, sizeof(slot->overlapped));
    slot->overlapped.hEvent = slot->event;
    ResetEvent(slot->event);

    BOOL ok = ReadDirectoryChangesW(
        slot->dir,
        slot->buffer,
        sizeof(slot->buffer),
        TRUE,
        FILE_NOTIFY_CHANGE_LAST_WRITE |
            FILE_NOTIFY_CHANGE_FILE_NAME |
            FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_SIZE,
        NULL,
        &slot->overlapped,
        NULL
    );

    slot->pending = ok || GetLastError() == ERROR_IO_PENDING;
    return slot->pending;
}

static int
open_slot(WatchSlot *slot, const char *root, const char *rel)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%s", root, rel);

    slot->dir = CreateFileA(
        path,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );
    if (slot->dir == INVALID_HANDLE_VALUE) {
        return 0;
    }

    slot->event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!slot->event) {
        CloseHandle(slot->dir);
        slot->dir = INVALID_HANDLE_VALUE;
        return 0;
    }

    return start_read(slot);
}

int
watcher_init(const char *project_root)
{
    memset(g_slots, 0, sizeof(g_slots));
    g_ready = 0;

    if (!open_slot(&g_slots[0], project_root, "src\\modules")) {
        return 0;
    }
    if (!open_slot(&g_slots[1], project_root, "include")) {
        watcher_shutdown();
        return 0;
    }

    g_ready = 1;
    return 1;
}

void
watcher_shutdown(void)
{
    for (int i = 0; i < WATCHER_COUNT; ++i) {
        if (g_slots[i].dir && g_slots[i].dir != INVALID_HANDLE_VALUE) {
            CancelIo(g_slots[i].dir);
            CloseHandle(g_slots[i].dir);
        }
        if (g_slots[i].event) {
            CloseHandle(g_slots[i].event);
        }
        memset(&g_slots[i], 0, sizeof(g_slots[i]));
    }
    g_ready = 0;
}

int
watcher_poll(void)
{
    if (!g_ready) {
        return 0;
    }

    int changed = 0;
    for (int i = 0; i < WATCHER_COUNT; ++i) {
        WatchSlot *slot = &g_slots[i];
        if (!slot->pending) {
            start_read(slot);
            continue;
        }

        DWORD bytes = 0;
        if (!GetOverlappedResult(slot->dir, &slot->overlapped, &bytes, FALSE)) {
            if (GetLastError() == ERROR_IO_INCOMPLETE) {
                continue;
            }
            start_read(slot);
            continue;
        }

        if (bytes > 0) {
            changed = 1;
        }
        start_read(slot);
    }

    return changed;
}

#else

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <unistd.h>

static int g_fd = -1;
static int g_ready;

int
watcher_init(const char *project_root)
{
    char src[1024];
    char inc[1024];
    snprintf(src, sizeof(src), "%s/src/modules", project_root ? project_root : ".");
    snprintf(inc, sizeof(inc), "%s/include", project_root ? project_root : ".");
    g_fd = inotify_init1(IN_NONBLOCK);
    if (g_fd < 0) {
        return 0;
    }
    if (inotify_add_watch(g_fd, src, IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_TO) < 0 ||
        inotify_add_watch(g_fd, inc, IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_TO) < 0) {
        close(g_fd);
        g_fd = -1;
        return 0;
    }
    g_ready = 1;
    return 1;
}

void
watcher_shutdown(void)
{
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
    g_ready = 0;
}

int
watcher_poll(void)
{
    if (!g_ready || g_fd < 0) {
        return 0;
    }
    char buf[4096];
    ssize_t n = read(g_fd, buf, sizeof(buf));
    if (n > 0) {
        return 1;
    }
    return 0;
}

#endif
