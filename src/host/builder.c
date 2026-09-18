#include "builder.h"
#include "config.h"
#include "shared/os.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

static HANDLE g_process;
static HANDLE g_read_pipe;
static BuildStatus g_status = BUILD_IDLE;
static char g_log[8192];
static size_t g_log_len;

static void
append_log(const char *data, DWORD len)
{
    if (!data || len == 0) {
        return;
    }
    if (g_log_len + len >= sizeof(g_log)) {
        len = (DWORD)(sizeof(g_log) - g_log_len - 1);
    }
    memcpy(g_log + g_log_len, data, len);
    g_log_len += len;
    g_log[g_log_len] = '\0';
}

static void
close_build(void)
{
    if (g_process) {
        CloseHandle(g_process);
        g_process = NULL;
    }
    if (g_read_pipe) {
        CloseHandle(g_read_pipe);
        g_read_pipe = NULL;
    }
}

void
builder_prepare_path(void)
{
    char current[32768];
    char next[32768];
    DWORD len = GetEnvironmentVariableA("PATH", current, sizeof(current));
    if (len == 0 || len >= sizeof(current)) {
        SetEnvironmentVariableA("PATH", APP_TOOL_PATH);
        return;
    }
    size_t tool_len = strlen(APP_TOOL_PATH);
    size_t current_len = strlen(current);
    if (tool_len + 1 + current_len + 1 > sizeof(next)) {
        SetEnvironmentVariableA("PATH", APP_TOOL_PATH);
        return;
    }
    memcpy(next, APP_TOOL_PATH, tool_len);
    next[tool_len] = ';';
    memcpy(next + tool_len + 1, current, current_len + 1);
    SetEnvironmentVariableA("PATH", next);
}

int
builder_start(void)
{
    if (g_status == BUILD_RUNNING) {
        return 0;
    }

    close_build();
    g_log[0] = '\0';
    g_log_len = 0;

    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE write_pipe = NULL;
    if (!CreatePipe(&g_read_pipe, &write_pipe, &sa, 0)) {
        g_status = BUILD_FAILED;
        snprintf(g_log, sizeof(g_log), "CreatePipe failed");
        return 0;
    }
    SetHandleInformation(g_read_pipe, HANDLE_FLAG_INHERIT, 0);

    char command[2048];
    snprintf(
        command,
        sizeof(command),
        "\"%s\" --build \"%s\" --target app_logic",
        APP_CMAKE_COMMAND,
        APP_BUILD_DIR
    );

    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    BOOL ok = CreateProcessA(
        NULL,
        command,
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
        NULL,
        APP_BUILD_DIR,
        &si,
        &pi
    );

    CloseHandle(write_pipe);

    if (!ok) {
        close_build();
        g_status = BUILD_FAILED;
        snprintf(g_log, sizeof(g_log), "cmake start failed (%lu)", GetLastError());
        return 0;
    }

    CloseHandle(pi.hThread);
    g_process = pi.hProcess;
    g_status = BUILD_RUNNING;
    return 1;
}

void
builder_poll(void)
{
    if (g_status != BUILD_RUNNING) {
        return;
    }

    char chunk[1024];
    DWORD available = 0;
    if (PeekNamedPipe(g_read_pipe, NULL, 0, NULL, &available, NULL) && available > 0) {
        DWORD read_bytes = 0;
        if (available > sizeof(chunk)) {
            available = sizeof(chunk);
        }
        if (ReadFile(g_read_pipe, chunk, available, &read_bytes, NULL) && read_bytes > 0) {
            append_log(chunk, read_bytes);
        }
    }

    DWORD wait = WaitForSingleObject(g_process, 0);
    if (wait == WAIT_TIMEOUT) {
        return;
    }

    DWORD leftover = 0;
    if (PeekNamedPipe(g_read_pipe, NULL, 0, NULL, &leftover, NULL) && leftover > 0) {
        DWORD read_bytes = 0;
        if (leftover > sizeof(chunk)) {
            leftover = sizeof(chunk);
        }
        if (ReadFile(g_read_pipe, chunk, leftover, &read_bytes, NULL) && read_bytes > 0) {
            append_log(chunk, read_bytes);
        }
    }

    DWORD exit_code = 1;
    GetExitCodeProcess(g_process, &exit_code);
    close_build();
    g_status = (exit_code == 0) ? BUILD_OK : BUILD_FAILED;
}

BuildStatus
builder_status(void)
{
    return g_status;
}

const char *
builder_log(void)
{
    return g_log;
}

void
builder_reset(void)
{
    if (g_status != BUILD_RUNNING) {
        g_status = BUILD_IDLE;
    }
}

#else

#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static FILE *g_pipe;
static BuildStatus g_status = BUILD_IDLE;
static char g_log[8192];
static size_t g_log_len;

void
builder_prepare_path(void)
{
    const char *current = getenv("PATH");
    if (!current) {
        setenv("PATH", APP_TOOL_PATH, 1);
        return;
    }
    char next[32768];
    snprintf(next, sizeof(next), "%s:%s", APP_TOOL_PATH, current);
    setenv("PATH", next, 1);
}

int
builder_start(void)
{
    if (g_status == BUILD_RUNNING) {
        return 0;
    }
    g_log[0] = '\0';
    g_log_len = 0;
    char command[2048];
    snprintf(
        command,
        sizeof(command),
        "\"%s\" --build \"%s\" --target app_logic 2>&1",
        APP_CMAKE_COMMAND,
        APP_BUILD_DIR
    );
    g_pipe = popen(command, "r");
    if (!g_pipe) {
        g_status = BUILD_FAILED;
        snprintf(g_log, sizeof(g_log), "cmake start failed");
        return 0;
    }
    g_status = BUILD_RUNNING;
    return 1;
}

void
builder_poll(void)
{
    if (g_status != BUILD_RUNNING || !g_pipe) {
        return;
    }
    char chunk[1024];
    if (fgets(chunk, sizeof(chunk), g_pipe)) {
        size_t n = strlen(chunk);
        if (g_log_len + n < sizeof(g_log)) {
            memcpy(g_log + g_log_len, chunk, n + 1);
            g_log_len += n;
        }
        return;
    }
    int code = pclose(g_pipe);
    g_pipe = NULL;
    g_status = (WIFEXITED(code) && WEXITSTATUS(code) == 0) ? BUILD_OK : BUILD_FAILED;
}

BuildStatus
builder_status(void)
{
    return g_status;
}

const char *
builder_log(void)
{
    return g_log;
}

void
builder_reset(void)
{
    if (g_status != BUILD_RUNNING) {
        g_status = BUILD_IDLE;
    }
}

#endif
