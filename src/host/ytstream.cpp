#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00

#include "debug_console.h"
#include "ytstream.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#define YT_URL_MAX 4096
#define YT_RATE 48000
#define YT_CH 2
#define YT_BITS 16
#define YT_FRAME (YT_CH * (YT_BITS / 8))
#define YT_RING (YT_RATE * YT_FRAME * 2)

static char g_root[512];
static char g_url[YT_URL_MAX];
static volatile LONG g_want;
static volatile LONG g_busy;
static volatile LONG g_playing;
static volatile LONG g_run;
static volatile LONG g_loop = 1;
static volatile LONG g_eof;
static volatile LONG g_got_pcm;
static volatile float g_gain = 0.5f;
static float g_level;
static HANDLE g_thread;
static HANDLE g_job;
static CRITICAL_SECTION g_lock;
static int g_lock_ready;

static uint8_t g_ring[YT_RING];
static volatile LONG g_rpos;
static volatile LONG g_wpos;

static void
ensure_lock(void)
{
    if (!g_lock_ready) {
        InitializeCriticalSection(&g_lock);
        g_lock_ready = 1;
    }
}

static void
copy_str(char *out, int max, const char *in)
{
    if (!out || max <= 0) {
        return;
    }
    if (!in) {
        out[0] = '\0';
        return;
    }
    snprintf(out, (size_t)max, "%s", in);
}

static int
is_http(const char *url)
{
    return url && (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

static int
has_ext(const char *url, const char *ext)
{
    char buf[YT_URL_MAX];
    copy_str(buf, (int)sizeof(buf), url);
    char *q = strchr(buf, '?');
    if (q) {
        *q = '\0';
    }
    const char *dot = strrchr(buf, '.');
    return dot && _stricmp(dot, ext) == 0;
}

static int
is_direct_media(const char *url)
{
    return has_ext(url, ".mp3") || has_ext(url, ".wav") || has_ext(url, ".ogg") ||
        has_ext(url, ".flac") || has_ext(url, ".m4a") || has_ext(url, ".aac") ||
        has_ext(url, ".opus") || has_ext(url, ".wma") || has_ext(url, ".mp4") ||
        has_ext(url, ".m4v") || has_ext(url, ".webm") || has_ext(url, ".mov");
}

int
ytstream_handles_url(const char *url)
{
    if (!url || !url[0] || !is_http(url)) {
        return 0;
    }
    if (strstr(url, "youtu.be/") || strstr(url, "youtube.com/") ||
        strstr(url, "music.youtube.com/") || strstr(url, "soundcloud.com/")) {
        return 1;
    }
    return !is_direct_media(url);
}

static int
file_exists(const char *path)
{
    return path && path[0] && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static int
find_tool(const char *name, char *out, int max)
{
    if (SearchPathA(NULL, name, NULL, (DWORD)max, out, NULL)) {
        return 1;
    }
    char path[MAX_PATH];
    const char *home = getenv("LOCALAPPDATA");
    if (g_root[0]) {
        snprintf(path, sizeof(path), "%s\\tools\\%s", g_root, name);
        if (file_exists(path)) {
            copy_str(out, max, path);
            return 1;
        }
    }
    if (home) {
        snprintf(path, sizeof(path), "%s\\Dawn\\tools\\%s", home, name);
        if (file_exists(path)) {
            copy_str(out, max, path);
            return 1;
        }
        snprintf(path, sizeof(path), "%s\\Microsoft\\WinGet\\Links\\%s", home, name);
        if (file_exists(path)) {
            copy_str(out, max, path);
            return 1;
        }
    }
    return 0;
}

static HANDLE
open_nul(void)
{
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    return CreateFileA(
        "NUL",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        OPEN_EXISTING,
        0,
        NULL
    );
}

static HANDLE
make_job(void)
{
    HANDLE job = CreateJobObjectA(NULL, NULL);
    if (!job) {
        return NULL;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
    memset(&info, 0, sizeof(info));
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
    return job;
}

static int
spawn_cmd(const char *cmd, HANDLE in, HANDLE out, HANDLE err, HANDLE job, PROCESS_INFORMATION *pi)
{
    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    memset(pi, 0, sizeof(*pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = in;
    si.hStdOutput = out;
    si.hStdError = err;

    char *mutable_cmd = (char *)malloc(32768);
    if (!mutable_cmd) {
        return 0;
    }
    copy_str(mutable_cmd, 32768, cmd);
    BOOL ok = CreateProcessA(
        NULL, mutable_cmd, NULL, NULL, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED,
        NULL, NULL, &si, pi
    );
    free(mutable_cmd);
    if (!ok) {
        return 0;
    }
    if (job) {
        AssignProcessToJobObject(job, pi->hProcess);
    }
    ResumeThread(pi->hThread);
    return 1;
}

static int
ring_used(void)
{
    LONG w = InterlockedCompareExchange(&g_wpos, 0, 0);
    LONG r = InterlockedCompareExchange(&g_rpos, 0, 0);
    int used = (int)(w - r);
    if (used < 0) {
        used += YT_RING;
    }
    if (used >= YT_RING) {
        used = YT_RING - 1;
    }
    return used;
}

static int
ring_write(const uint8_t *data, int n)
{
    int written = 0;
    while (written < n) {
        if (!InterlockedCompareExchange(&g_run, 0, 0)) {
            break;
        }
        int used = ring_used();
        int free_b = YT_RING - 1 - used;
        if (free_b <= 0) {
            Sleep(4);
            continue;
        }
        LONG w = InterlockedCompareExchange(&g_wpos, 0, 0);
        int room = YT_RING - (w % YT_RING);
        if (room > free_b) {
            room = free_b;
        }
        if (room > n - written) {
            room = n - written;
        }
        memcpy(g_ring + (w % YT_RING), data + written, (size_t)room);
        InterlockedExchange(&g_wpos, (w + room) % YT_RING);
        written += room;
    }
    return written;
}

static int
ring_read(uint8_t *data, int n)
{
    int used = ring_used();
    if (used < n) {
        n = used;
    }
    if (n <= 0) {
        return 0;
    }
    LONG r = InterlockedCompareExchange(&g_rpos, 0, 0);
    int first = YT_RING - (r % YT_RING);
    if (first > n) {
        first = n;
    }
    memcpy(data, g_ring + (r % YT_RING), (size_t)first);
    if (n > first) {
        memcpy(data + first, g_ring, (size_t)(n - first));
    }
    InterlockedExchange(&g_rpos, (r + n) % YT_RING);
    return n;
}

static float
pcm_peak(const int16_t *samples, int frames)
{
    float peak = 0.0f;
    for (int i = 0; i < frames * YT_CH; ++i) {
        float s = samples[i] / 32768.0f;
        if (s < 0.0f) {
            s = -s;
        }
        if (s > peak) {
            peak = s;
        }
    }
    return peak;
}

static DWORD WINAPI
pump_main(void *param)
{
    HANDLE pcm = (HANDLE)param;
    uint8_t chunk[8192];
    while (InterlockedCompareExchange(&g_run, 0, 0)) {
        DWORD got = 0;
        if (!ReadFile(pcm, chunk, sizeof(chunk), &got, NULL) || got == 0) {
            break;
        }
        ring_write(chunk, (int)got);
        if (got >= 4) {
            int frames = (int)got / YT_FRAME;
            if (frames < 1) {
                frames = 1;
            }
            g_level = pcm_peak((const int16_t *)chunk, frames);
            InterlockedExchange(&g_got_pcm, 1);
            if (!InterlockedCompareExchange(&g_playing, 0, 0)) {
                InterlockedExchange(&g_playing, 1);
                InterlockedExchange(&g_busy, 0);
                debug_log("audio stream playing");
            }
        }
    }
    return 0;
}

static DWORD WINAPI
render_main(void *param)
{
    (void)param;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    IAudioClient *client = NULL;
    IAudioRenderClient *render = NULL;
    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = YT_CH;
    fmt.nSamplesPerSec = YT_RATE;
    fmt.wBitsPerSample = YT_BITS;
    fmt.nBlockAlign = (WORD)YT_FRAME;
    fmt.nAvgBytesPerSec = YT_RATE * YT_FRAME;

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void **)&enumerator
    );
    if (SUCCEEDED(hr)) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    }
    if (SUCCEEDED(hr)) {
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&client);
    }
    if (SUCCEEDED(hr)) {
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 200000, 0, &fmt, NULL);
    }
    UINT32 buffer_frames = 0;
    if (SUCCEEDED(hr)) {
        hr = client->GetBufferSize(&buffer_frames);
    }
    if (SUCCEEDED(hr)) {
        hr = client->GetService(__uuidof(IAudioRenderClient), (void **)&render);
    }
    if (FAILED(hr) || !client || !render) {
        OutputDebugStringA("ytstream: WASAPI init failed\n");
        debug_log("WASAPI init failed (hr=0x%08lx)", (unsigned long)hr);
        InterlockedExchange(&g_playing, 0);
        InterlockedExchange(&g_busy, 0);
        CoUninitialize();
        return 0;
    }

    ISimpleAudioVolume *volume = NULL;
    if (SUCCEEDED(client->GetService(__uuidof(ISimpleAudioVolume), (void **)&volume)) && volume) {
        volume->SetMasterVolume(1.0f, NULL);
        volume->SetMute(FALSE, NULL);
        volume->Release();
    }

    client->Start();
    debug_log("WASAPI render started (%u frames)", buffer_frames);
    while (InterlockedCompareExchange(&g_run, 0, 0)) {
        UINT32 padding = 0;
        if (FAILED(client->GetCurrentPadding(&padding))) {
            break;
        }
        UINT32 frames = buffer_frames - padding;
        if (frames == 0) {
            Sleep(2);
            continue;
        }
        BYTE *out = NULL;
        if (FAILED(render->GetBuffer(frames, &out)) || !out) {
            Sleep(2);
            continue;
        }
        int need = (int)frames * YT_FRAME;
        int have = ring_read(out, need);
        if (have < need) {
            memset(out + have, 0, (size_t)(need - have));
        }
        if (have >= 2) {
            float gain = g_gain;
            if (gain < 0.0f) {
                gain = 0.0f;
            }
            if (gain > 1.0f) {
                gain = 1.0f;
            }
            if (gain < 0.999f) {
                int16_t *samples = (int16_t *)out;
                int count = have / 2;
                for (int i = 0; i < count; ++i) {
                    int v = (int)((float)samples[i] * gain);
                    if (v > 32767) {
                        v = 32767;
                    }
                    if (v < -32768) {
                        v = -32768;
                    }
                    samples[i] = (int16_t)v;
                }
            }
        }
        DWORD flags = 0;
        if (have == 0) {
            flags = AUDCLNT_BUFFERFLAGS_SILENT;
            if (InterlockedCompareExchange(&g_eof, 0, 0)) {
                break;
            }
            Sleep(4);
        }
        render->ReleaseBuffer(frames, flags);
    }

    client->Stop();
    if (render) {
        render->Release();
    }
    if (client) {
        client->Release();
    }
    if (device) {
        device->Release();
    }
    if (enumerator) {
        enumerator->Release();
    }
    g_level = 0.0f;
    CoUninitialize();
    return 0;
}

static int
extract_youtube_id(const char *url, char *id, int max)
{
    if (!url || !id || max < 12) {
        return 0;
    }
    id[0] = '\0';
    const char *p = NULL;
    const char *hit = strstr(url, "youtu.be/");
    if (hit) {
        p = hit + 9;
    } else if (strstr(url, "youtube.com/") || strstr(url, "music.youtube.com/")) {
        if ((hit = strstr(url, "v=")) != NULL) {
            p = hit + 2;
        } else if ((hit = strstr(url, "/embed/")) != NULL) {
            p = hit + 7;
        } else if ((hit = strstr(url, "/shorts/")) != NULL) {
            p = hit + 8;
        }
    }
    if (!p) {
        return 0;
    }
    int n = 0;
    while (p[n] && n < 11 &&
           ((p[n] >= 'A' && p[n] <= 'Z') ||
            (p[n] >= 'a' && p[n] <= 'z') ||
            (p[n] >= '0' && p[n] <= '9') ||
            p[n] == '-' || p[n] == '_')) {
        n++;
    }
    if (n != 11) {
        return 0;
    }
    memcpy(id, p, 11);
    id[11] = '\0';
    return 1;
}

static void
canonical_page(const char *in, char *out, int max)
{
    char id[16];
    if (extract_youtube_id(in, id, (int)sizeof(id))) {
        snprintf(out, (size_t)max, "https://www.youtube.com/watch?v=%s", id);
        return;
    }
    copy_str(out, max, in);
}

static int
ensure_dir(const char *path)
{
    if (!path || !path[0]) {
        return 0;
    }
    if (CreateDirectoryA(path, NULL)) {
        return 1;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

static int
make_cache_dir(char *out, int max)
{
    const char *home = getenv("LOCALAPPDATA");
    if (!home || !home[0]) {
        return 0;
    }
    char dawn[MAX_PATH];
    char cache[MAX_PATH];
    snprintf(dawn, sizeof(dawn), "%s\\Dawn", home);
    snprintf(cache, sizeof(cache), "%s\\cache", dawn);
    snprintf(out, (size_t)max, "%s\\audio", cache);
    ensure_dir(dawn);
    ensure_dir(cache);
    return ensure_dir(out);
}

static int
find_cached(const char *id, char *out, int max)
{
    if (!id || !id[0]) {
        return 0;
    }
    char dir[MAX_PATH];
    if (!make_cache_dir(dir, (int)sizeof(dir))) {
        return 0;
    }
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\%s.*", dir, id);
    WIN32_FIND_DATAA fd;
    HANDLE find = FindFirstFileA(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) {
        return 0;
    }
    int ok = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        size_t n = strlen(fd.cFileName);
        if (n > 5 && _stricmp(fd.cFileName + n - 5, ".part") == 0) {
            continue;
        }
        if (fd.nFileSizeHigh == 0 && fd.nFileSizeLow < 4096) {
            continue;
        }
        snprintf(out, (size_t)max, "%s\\%s", dir, fd.cFileName);
        ok = 1;
        break;
    } while (FindNextFileA(find, &fd));
    FindClose(find);
    return ok;
}

static void
trim_copy_path(char *out, int max, const char *text)
{
    out[0] = '\0';
    if (!text) {
        return;
    }
    const char *best = NULL;
    int best_n = 0;
    const char *p = text;
    while (*p) {
        while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *eol = p;
        while (*eol && *eol != '\r' && *eol != '\n') {
            eol++;
        }
        int n = (int)(eol - p);
        if (n > 3 && (strstr(p, ":\\") || strncmp(p, "\\\\", 2) == 0 || strstr(p, "/"))) {
            best = p;
            best_n = n;
        }
        p = *eol ? eol + 1 : eol;
    }
    if (!best) {
        return;
    }
    if (best_n > max - 1) {
        best_n = max - 1;
    }
    memcpy(out, best, (size_t)best_n);
    out[best_n] = '\0';
}

static void
log_file_tail(const char *path, const char *prefix)
{
    if (!path || !path[0]) {
        return;
    }
    HANDLE file = CreateFileA(
        path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL
    );
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE || size == 0) {
        CloseHandle(file);
        return;
    }
    DWORD start = size > 800 ? size - 800 : 0;
    SetFilePointer(file, (LONG)start, NULL, FILE_BEGIN);
    char buf[900];
    DWORD got = 0;
    if (!ReadFile(file, buf, sizeof(buf) - 1, &got, NULL) || got == 0) {
        CloseHandle(file);
        return;
    }
    CloseHandle(file);
    buf[got] = '\0';
    char *last = buf;
    for (char *s = buf; *s; ++s) {
        if (*s == '\n' && s[1]) {
            last = s + 1;
        }
    }
    for (char *s = last; *s; ++s) {
        if (*s == '\r' || *s == '\n') {
            *s = '\0';
            break;
        }
    }
    if (last[0]) {
        debug_log("%s %s", prefix ? prefix : "log:", last);
    }
}

static int
run_cmd_capture(const char *cmd, char *out, int max, char *err_path, int err_max)
{
    if (out && max > 0) {
        out[0] = '\0';
    }
    if (err_path && err_max > 0) {
        err_path[0] = '\0';
    }
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE read_pipe = NULL;
    HANDLE write_pipe = NULL;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 1 << 16)) {
        return 0;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    char tmp[MAX_PATH];
    char errfile[MAX_PATH];
    errfile[0] = '\0';
    GetTempPathA((DWORD)sizeof(tmp), tmp);
    GetTempFileNameA(tmp, "yt", 0, errfile);
    HANDLE err = CreateFileA(
        errfile,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY,
        NULL
    );
    HANDLE nul = open_nul();

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = write_pipe;
    si.hStdError = (err && err != INVALID_HANDLE_VALUE) ? err : nul;
    si.hStdInput = nul;

    char *mutable_cmd = (char *)malloc(32768);
    if (!mutable_cmd) {
        CloseHandle(write_pipe);
        CloseHandle(read_pipe);
        if (err && err != INVALID_HANDLE_VALUE) {
            CloseHandle(err);
        }
        if (nul && nul != INVALID_HANDLE_VALUE) {
            CloseHandle(nul);
        }
        return 0;
    }
    copy_str(mutable_cmd, 32768, cmd);
    BOOL ok = CreateProcessA(
        NULL, mutable_cmd, NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi
    );
    free(mutable_cmd);
    CloseHandle(write_pipe);
    if (nul && nul != INVALID_HANDLE_VALUE) {
        CloseHandle(nul);
    }
    if (err && err != INVALID_HANDLE_VALUE) {
        CloseHandle(err);
    }
    if (!ok) {
        CloseHandle(read_pipe);
        DeleteFileA(errfile);
        return 0;
    }

    DWORD got = 0;
    int used = 0;
    char buf[1024];
    while (ReadFile(read_pipe, buf, sizeof(buf), &got, NULL) && got > 0) {
        if (out && used < max - 1) {
            int copy = (int)got;
            if (used + copy > max - 1) {
                copy = max - 1 - used;
            }
            memcpy(out + used, buf, (size_t)copy);
            used += copy;
            out[used] = '\0';
        }
        if (!InterlockedCompareExchange(&g_run, 0, 0)) {
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(read_pipe);
    if (err_path && err_max > 0) {
        copy_str(err_path, err_max, errfile);
    } else {
        DeleteFileA(errfile);
    }
    return code == 0;
}

static void
sleep_run(DWORD ms)
{
    DWORD start = GetTickCount();
    while (InterlockedCompareExchange(&g_run, 0, 0) &&
           InterlockedCompareExchange(&g_want, 0, 0)) {
        if (GetTickCount() - start >= ms) {
            break;
        }
        Sleep(100);
    }
}

static int
download_audio(const char *page, char *path, int path_max)
{
    char ytdlp[MAX_PATH];
    if (!find_tool("yt-dlp.exe", ytdlp, (int)sizeof(ytdlp)) &&
        !find_tool("yt-dlp", ytdlp, (int)sizeof(ytdlp))) {
        debug_log("yt-dlp not found");
        return 0;
    }

    char watch[YT_URL_MAX];
    canonical_page(page, watch, (int)sizeof(watch));

    char id[16];
    id[0] = '\0';
    extract_youtube_id(watch, id, (int)sizeof(id));
    if (id[0] && find_cached(id, path, path_max)) {
        debug_log("using cached audio");
        return 1;
    }

    char dir[MAX_PATH];
    if (!make_cache_dir(dir, (int)sizeof(dir))) {
        debug_log("audio cache dir failed");
        return 0;
    }

    debug_log("yt-dlp: %s", ytdlp);
    debug_log("downloading %s", watch);
    char cmd[YT_URL_MAX + 2048];
    snprintf(
        cmd,
        sizeof(cmd),
        "\"%s\" -f ba/bestaudio/b/18 --no-playlist --no-warnings --no-progress --no-overwrites "
        "--extractor-args youtube:player_client=web_embedded,android "
        "--print after_move:filepath -o \"%s\\%%(id)s.%%(ext)s\" -- \"%s\"",
        ytdlp,
        dir,
        watch
    );

    char printed[4096];
    char errpath[MAX_PATH];
    printed[0] = '\0';
    errpath[0] = '\0';
    if (!run_cmd_capture(cmd, printed, (int)sizeof(printed), errpath, (int)sizeof(errpath))) {
        log_file_tail(errpath, "yt-dlp:");
        DeleteFileA(errpath);
        debug_log("yt-dlp download failed");
        return 0;
    }
    DeleteFileA(errpath);
    trim_copy_path(path, path_max, printed);
    if ((!path[0] || !file_exists(path)) && id[0]) {
        find_cached(id, path, path_max);
    }
    if (!file_exists(path)) {
        debug_log("yt-dlp did not produce a file");
        return 0;
    }
    debug_log("cached audio ready");
    return 1;
}

static int
decode_file(const char *path)
{
    char ffmpeg[MAX_PATH];
    if (!find_tool("ffmpeg.exe", ffmpeg, (int)sizeof(ffmpeg)) &&
        !find_tool("ffmpeg", ffmpeg, (int)sizeof(ffmpeg))) {
        debug_log("ffmpeg not found");
        return 0;
    }

    InterlockedExchange(&g_got_pcm, 0);
    InterlockedExchange(&g_rpos, 0);
    InterlockedExchange(&g_wpos, 0);
    InterlockedExchange(&g_eof, 0);

    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE pcm_read = NULL;
    HANDLE pcm_write = NULL;
    if (!CreatePipe(&pcm_read, &pcm_write, &sa, 1 << 16)) {
        return 0;
    }
    SetHandleInformation(pcm_read, HANDLE_FLAG_INHERIT, 0);

    char tmp[MAX_PATH];
    char errpath[MAX_PATH];
    GetTempPathA((DWORD)sizeof(tmp), tmp);
    GetTempFileNameA(tmp, "ff", 0, errpath);
    HANDLE err = CreateFileA(
        errpath,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY,
        NULL
    );
    HANDLE nul = open_nul();
    HANDLE job = make_job();
    g_job = job;

    char ff_cmd[MAX_PATH * 2 + 512];
    snprintf(
        ff_cmd,
        sizeof(ff_cmd),
        "\"%s\" -hide_banner -nostdin -loglevel error -i \"%s\" -vn -f s16le -ac 2 -ar 48000 pipe:1",
        ffmpeg,
        path
    );

    PROCESS_INFORMATION ff_pi;
    memset(&ff_pi, 0, sizeof(ff_pi));
    int ok = spawn_cmd(ff_cmd, nul, pcm_write, (err && err != INVALID_HANDLE_VALUE) ? err : nul, job, &ff_pi);
    CloseHandle(pcm_write);
    if (nul && nul != INVALID_HANDLE_VALUE) {
        CloseHandle(nul);
    }
    if (err && err != INVALID_HANDLE_VALUE) {
        CloseHandle(err);
    }
    if (!ok) {
        CloseHandle(pcm_read);
        if (job) {
            CloseHandle(job);
            g_job = NULL;
        }
        DeleteFileA(errpath);
        debug_log("ffmpeg spawn failed");
        return 0;
    }
    CloseHandle(ff_pi.hThread);
    debug_log("ffmpeg decoding audio file");

    HANDLE pump = CreateThread(NULL, 0, pump_main, pcm_read, 0, NULL);
    HANDLE render = CreateThread(NULL, 0, render_main, NULL, 0, NULL);
    if (pump) {
        WaitForSingleObject(pump, INFINITE);
        CloseHandle(pump);
    }
    InterlockedExchange(&g_eof, 1);
    if (render) {
        WaitForSingleObject(render, 4000);
        CloseHandle(render);
    }
    CloseHandle(pcm_read);
    WaitForSingleObject(ff_pi.hProcess, 500);
    CloseHandle(ff_pi.hProcess);
    if (g_job) {
        CloseHandle(g_job);
        g_job = NULL;
    }
    int got = InterlockedCompareExchange(&g_got_pcm, 0, 0) ? 1 : 0;
    if (!got && InterlockedCompareExchange(&g_run, 0, 0)) {
        log_file_tail(errpath, "ffmpeg:");
        debug_log("ffmpeg produced no PCM");
    }
    DeleteFileA(errpath);
    return got;
}

static DWORD WINAPI
worker_main(void *param)
{
    (void)param;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    char file[MAX_PATH];
    file[0] = '\0';
    while (InterlockedCompareExchange(&g_run, 0, 0) &&
           InterlockedCompareExchange(&g_want, 0, 0)) {
        char url[YT_URL_MAX];
        EnterCriticalSection(&g_lock);
        copy_str(url, (int)sizeof(url), g_url);
        LeaveCriticalSection(&g_lock);
        if (!file[0] || !file_exists(file)) {
            file[0] = '\0';
            if (!download_audio(url, file, (int)sizeof(file))) {
                debug_log("audio download retry in 8s");
                sleep_run(8000);
                continue;
            }
        }
        if (!decode_file(file)) {
            if (!InterlockedCompareExchange(&g_run, 0, 0) ||
                !InterlockedCompareExchange(&g_want, 0, 0)) {
                break;
            }
            debug_log("audio decode retry in 8s");
            file[0] = '\0';
            sleep_run(8000);
            continue;
        }
        if (!InterlockedCompareExchange(&g_loop, 0, 0)) {
            break;
        }
        debug_log("looping audio");
    }

    InterlockedExchange(&g_busy, 0);
    InterlockedExchange(&g_playing, 0);
    InterlockedExchange(&g_run, 0);
    CoUninitialize();
    return 0;
}

void
ytstream_init(const char *project_root)
{
    ensure_lock();
    copy_str(g_root, (int)sizeof(g_root), project_root);
}

void
ytstream_shutdown(void)
{
    ytstream_stop();
}

void
ytstream_poll(void)
{
    if (g_thread && WaitForSingleObject(g_thread, 0) == WAIT_OBJECT_0) {
        CloseHandle(g_thread);
        g_thread = NULL;
        if (InterlockedCompareExchange(&g_want, 0, 0) &&
            InterlockedCompareExchange(&g_loop, 0, 0) &&
            g_url[0]) {
            ytstream_play(g_url);
        }
    }
}

void
ytstream_set_loop(int loop)
{
    InterlockedExchange(&g_loop, loop ? 1 : 0);
}

void
ytstream_set_volume(float volume)
{
    if (volume < 0.0f) {
        volume = 0.0f;
    }
    if (volume > 1.0f) {
        volume = 1.0f;
    }
    g_gain = volume;
}

void
ytstream_stop(void)
{
    InterlockedExchange(&g_want, 0);
    InterlockedExchange(&g_run, 0);
    if (g_job) {
        CloseHandle(g_job);
        g_job = NULL;
    }
    if (g_thread) {
        WaitForSingleObject(g_thread, 4000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    InterlockedExchange(&g_playing, 0);
    InterlockedExchange(&g_busy, 0);
    g_level = 0.0f;
}

void
ytstream_play(const char *url)
{
    ensure_lock();
    if (!url || !url[0]) {
        return;
    }
    EnterCriticalSection(&g_lock);
    int same = strcmp(g_url, url) == 0;
    copy_str(g_url, (int)sizeof(g_url), url);
    LeaveCriticalSection(&g_lock);

    if (same && g_thread &&
        (InterlockedCompareExchange(&g_playing, 0, 0) ||
         InterlockedCompareExchange(&g_busy, 0, 0) ||
         InterlockedCompareExchange(&g_want, 0, 0))) {
        debug_log("YouTube audio already playing");
        return;
    }

    debug_log("starting YouTube audio");
    ytstream_stop();
    InterlockedExchange(&g_want, 1);
    InterlockedExchange(&g_run, 1);
    InterlockedExchange(&g_busy, 1);
    InterlockedExchange(&g_playing, 0);
    g_thread = CreateThread(NULL, 0, worker_main, NULL, 0, NULL);
}

int
ytstream_playing(void)
{
    return InterlockedCompareExchange(&g_playing, 0, 0) ? 1 : 0;
}

int
ytstream_busy(void)
{
    return InterlockedCompareExchange(&g_busy, 0, 0) ? 1 : 0;
}

float
ytstream_level(void)
{
    float level = g_level;
    if (level < 0.0f) {
        return 0.0f;
    }
    if (level > 1.0f) {
        return 1.0f;
    }
    return level;
}
