#include "debug_console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#ifdef _WIN32
static HANDLE g_out;
static CRITICAL_SECTION g_lock;
#else
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
#endif
static int g_open;
static int g_lock_ready;

static void
ensure_lock(void)
{
#ifdef _WIN32
    if (!g_lock_ready) {
        InitializeCriticalSection(&g_lock);
        g_lock_ready = 1;
    }
#else
    (void)g_lock_ready;
#endif
}

#ifdef _WIN32
static BOOL WINAPI
console_ctrl(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        return TRUE;
    }
    return FALSE;
}

static void
attach_streams(void)
{
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleOutputCP(CP_UTF8);
}
#endif

void
debug_console_init(void)
{
    ensure_lock();
}

void
debug_console_shutdown(void)
{
#ifdef _WIN32
    if (g_open) {
        FreeConsole();
        g_open = 0;
        g_out = NULL;
    }
#else
    g_open = 0;
#endif
}

int
debug_console_open(void)
{
    return g_open;
}

void
debug_console_toggle(void)
{
#ifdef _WIN32
    ensure_lock();
    EnterCriticalSection(&g_lock);
    if (g_open) {
        FreeConsole();
        g_open = 0;
        g_out = NULL;
        LeaveCriticalSection(&g_lock);
        return;
    }
    if (!AllocConsole()) {
        AttachConsole(ATTACH_PARENT_PROCESS);
    }
    SetConsoleTitleA("DAWN debug  (F12 to close)");
    SetConsoleCtrlHandler(console_ctrl, TRUE);
    HWND hwnd = GetConsoleWindow();
    if (hwnd) {
        HMENU menu = GetSystemMenu(hwnd, FALSE);
        if (menu) {
            DeleteMenu(menu, SC_CLOSE, MF_BYCOMMAND);
        }
    }
    attach_streams();
    g_open = 1;
    LeaveCriticalSection(&g_lock);
    debug_log("debug console ready");
    debug_log("F12 toggles this console");
#else
    g_open = !g_open;
    debug_log(g_open ? "debug log on" : "debug log off");
#endif
}

void
debug_log(const char *fmt, ...)
{
    if (!fmt) {
        return;
    }
    ensure_lock();
    char line[2048];
#ifdef _WIN32
    SYSTEMTIME st;
    GetLocalTime(&st);
    int n = snprintf(
        line,
        sizeof(line),
        "[%02u:%02u:%02u] ",
        (unsigned)st.wHour,
        (unsigned)st.wMinute,
        (unsigned)st.wSecond
    );
#else
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    int n = snprintf(
        line,
        sizeof(line),
        "[%02d:%02d:%02d] ",
        tm_now.tm_hour,
        tm_now.tm_min,
        tm_now.tm_sec
    );
#endif
    if (n < 0) {
        n = 0;
    }
    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(line + n, sizeof(line) - (size_t)n - 2, fmt, ap);
    va_end(ap);
    if (m < 0) {
        m = 0;
    }
    n += m;
    if (n > (int)sizeof(line) - 3) {
        n = (int)sizeof(line) - 3;
    }
    line[n++] = '\n';
    line[n] = '\0';

#ifdef _WIN32
    OutputDebugStringA(line);
    EnterCriticalSection(&g_lock);
    if (g_open && g_out && g_out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_out, line, (DWORD)n, &written, NULL);
    }
    LeaveCriticalSection(&g_lock);
#else
    pthread_mutex_lock(&g_lock);
    fputs(line, stderr);
    pthread_mutex_unlock(&g_lock);
#endif
}
