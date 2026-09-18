#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00

#include "media.h"
#include "video_bg.h"
#include "ytstream.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mfapi.h>
#include <mfplay.h>
#include <propidl.h>

#define MEDIA_URL_MAX 4096

static char g_root[512];
static char g_url[MEDIA_URL_MAX];
static char g_pending_path[MEDIA_URL_MAX];
static char g_ready_path[MEDIA_URL_MAX];
static volatile LONG g_busy;
static volatile LONG g_want_play;
static volatile LONG g_cancel;
static volatile LONG g_player_playing;
static volatile LONG g_ended;
static volatile LONG g_loop = 1;
static volatile LONG g_stream_failed;
static float g_level;
static HANDLE g_worker;
static HANDLE g_cap_thread;
static volatile LONG g_cap_run;
static CRITICAL_SECTION g_lock;
static int g_lock_ready;
static IMFPMediaPlayer *g_player;
static IMFPMediaPlayerCallback *g_callback;
static int g_com;

class PlayerCallback : public IMFPMediaPlayerCallback {
    LONG refs;
public:
    PlayerCallback() : refs(1) {}
    virtual ~PlayerCallback() {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv)
    {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IMFPMediaPlayerCallback) {
            *ppv = static_cast<IMFPMediaPlayerCallback *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()
    {
        return (ULONG)InterlockedIncrement(&refs);
    }
    ULONG STDMETHODCALLTYPE Release()
    {
        LONG n = InterlockedDecrement(&refs);
        if (n == 0) {
            delete this;
        }
        return (ULONG)n;
    }
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER *event)
    {
        if (!event) {
            return;
        }
        if (event->eEventType == MFP_EVENT_TYPE_ERROR) {
            InterlockedExchange(&g_player_playing, 0);
            InterlockedExchange(&g_ended, 1);
            InterlockedExchange(&g_stream_failed, 1);
        } else if (event->eEventType == MFP_EVENT_TYPE_PLAYBACK_ENDED) {
            if (InterlockedCompareExchange(&g_loop, 0, 0) &&
                InterlockedCompareExchange(&g_want_play, 0, 0)) {
                InterlockedExchange(&g_ended, 2);
            } else {
                InterlockedExchange(&g_player_playing, 0);
                InterlockedExchange(&g_ended, 1);
            }
        } else if (event->eEventType == MFP_EVENT_TYPE_PLAY) {
            InterlockedExchange(&g_player_playing, 1);
        } else if (event->eEventType == MFP_EVENT_TYPE_PAUSE ||
                   event->eEventType == MFP_EVENT_TYPE_STOP) {
            InterlockedExchange(&g_player_playing, 0);
        }
    }
};

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
extract_youtube_id(const char *url, char *id, int max)
{
    if (!url || !id || max < 12) {
        return 0;
    }
    id[0] = '\0';
    const char *p = url;
    const char *hit = strstr(url, "youtu.be/");
    if (hit) {
        p = hit + 9;
    } else if ((hit = strstr(url, "youtube.com/watch")) != NULL) {
        const char *v = strstr(hit, "v=");
        if (!v) {
            return 0;
        }
        p = v + 2;
    } else if ((hit = strstr(url, "youtube.com/embed/")) != NULL) {
        p = hit + 18;
    } else if ((hit = strstr(url, "youtube.com/shorts/")) != NULL) {
        p = hit + 19;
    } else if ((hit = strstr(url, "youtube.com/live/")) != NULL) {
        p = hit + 17;
    } else if ((hit = strstr(url, "music.youtube.com/watch")) != NULL) {
        const char *v = strstr(hit, "v=");
        if (!v) {
            return 0;
        }
        p = v + 2;
    } else {
        int n = 0;
        while (url[n] && ((url[n] >= 'A' && url[n] <= 'Z') ||
                          (url[n] >= 'a' && url[n] <= 'z') ||
                          (url[n] >= '0' && url[n] <= '9') ||
                          url[n] == '-' || url[n] == '_')) {
            n++;
        }
        if (n == 11 && url[n] == '\0') {
            memcpy(id, url, 11);
            id[11] = '\0';
            return 1;
        }
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

static int
is_youtube(const char *url)
{
    char id[16];
    return extract_youtube_id(url, id, (int)sizeof(id));
}

static int
is_direct_audio(const char *url)
{
    if (!url) {
        return 0;
    }
    const char *q = strchr(url, '?');
    char buf[MEDIA_URL_MAX];
    copy_str(buf, (int)sizeof(buf), url);
    if (q && q >= url) {
        buf[q - url] = '\0';
    }
    const char *dot = strrchr(buf, '.');
    if (!dot) {
        return 0;
    }
    return _stricmp(dot, ".mp3") == 0 ||
        _stricmp(dot, ".wav") == 0 ||
        _stricmp(dot, ".ogg") == 0 ||
        _stricmp(dot, ".flac") == 0 ||
        _stricmp(dot, ".m4a") == 0 ||
        _stricmp(dot, ".aac") == 0 ||
        _stricmp(dot, ".opus") == 0 ||
        _stricmp(dot, ".wma") == 0 ||
        _stricmp(dot, ".mp4") == 0 ||
        _stricmp(dot, ".m4v") == 0 ||
        _stricmp(dot, ".webm") == 0 ||
        _stricmp(dot, ".mov") == 0 ||
        _stricmp(dot, ".mkv") == 0 ||
        _stricmp(dot, ".avi") == 0 ||
        _stricmp(dot, ".wmv") == 0;
}

static int
first_http_line(const char *text, char *out, int max)
{
    if (!text || !out || max < 12) {
        return 0;
    }
    const char *p = text;
    while (p && *p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (strncmp(p, "http://", 7) == 0 || strncmp(p, "https://", 8) == 0) {
            int n = 0;
            while (p[n] && p[n] != '\r' && p[n] != '\n' && n < max - 1) {
                n++;
            }
            memcpy(out, p, (size_t)n);
            out[n] = '\0';
            return n > 8;
        }
        const char *nl = strchr(p, '\n');
        p = nl ? nl + 1 : NULL;
    }
    return 0;
}

static int
find_ytdlp(char *out, int max)
{
    if (SearchPathA(NULL, "yt-dlp.exe", NULL, (DWORD)max, out, NULL)) {
        return 1;
    }
    if (SearchPathA(NULL, "yt-dlp", NULL, (DWORD)max, out, NULL)) {
        return 1;
    }
    if (g_root[0]) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s/tools/yt-dlp.exe", g_root);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
            copy_str(out, max, path);
            return 1;
        }
    }
    return 0;
}

static void
ensure_cache_dir(char *out, int max)
{
    snprintf(out, (size_t)max, "%s/build/cache", g_root[0] ? g_root : ".");
    CreateDirectoryA(out, NULL);
    snprintf(out, (size_t)max, "%s/build/cache/audio", g_root[0] ? g_root : ".");
    CreateDirectoryA(out, NULL);
}

static int
find_cached(const char *id, char *out, int max)
{
    char dir[MAX_PATH];
    char pattern[MAX_PATH];
    ensure_cache_dir(dir, (int)sizeof(dir));
    snprintf(pattern, sizeof(pattern), "%s\\%s.*", dir, id);
    WIN32_FIND_DATAA find;
    HANDLE handle = FindFirstFileA(pattern, &find);
    if (handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    snprintf(out, (size_t)max, "%s\\%s", dir, find.cFileName);
    FindClose(handle);
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}

static int
run_hidden(const char *cmd, char *out, int out_max)
{
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE read_pipe = NULL;
    HANDLE write_pipe = NULL;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        return 0;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    char mutable_cmd[2048];
    copy_str(mutable_cmd, (int)sizeof(mutable_cmd), cmd);
    BOOL ok = CreateProcessA(
        NULL, mutable_cmd, NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi
    );
    CloseHandle(write_pipe);
    if (!ok) {
        CloseHandle(read_pipe);
        return 0;
    }

    DWORD got = 0;
    int used = 0;
    char buf[512];
    while (ReadFile(read_pipe, buf, sizeof(buf), &got, NULL) && got > 0) {
        if (out && used < out_max - 1) {
            int copy = (int)got;
            if (used + copy > out_max - 1) {
                copy = out_max - 1 - used;
            }
            memcpy(out + used, buf, (size_t)copy);
            used += copy;
            out[used] = '\0';
        }
        if (InterlockedCompareExchange(&g_cancel, 0, 0)) {
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(read_pipe);
    return code == 0;
}

static int
resolve_source(const char *url, char *out, int max)
{
    if (!url || !url[0]) {
        return 0;
    }
    if (!is_http(url) && !is_youtube(url)) {
        copy_str(out, max, url);
        return 1;
    }
    if (is_direct_audio(url)) {
        copy_str(out, max, url);
        return 1;
    }

    char id[16];
    if (extract_youtube_id(url, id, (int)sizeof(id)) && find_cached(id, out, max)) {
        return 1;
    }

    char ytdlp[MAX_PATH];
    if (!find_ytdlp(ytdlp, (int)sizeof(ytdlp))) {
        OutputDebugStringA("yt-dlp not found, cannot resolve audio url\n");
        return 0;
    }

    char cmd[MEDIA_URL_MAX + 512];
    snprintf(
        cmd,
        sizeof(cmd),
        "\"%s\" -g -f \"bestaudio/ba/best\" --no-playlist --no-warnings --no-progress -- \"%s\"",
        ytdlp,
        url
    );

    char printed[MEDIA_URL_MAX];
    printed[0] = '\0';
    if (run_hidden(cmd, printed, (int)sizeof(printed)) && first_http_line(printed, out, max)) {
        return 1;
    }

    if (extract_youtube_id(url, id, (int)sizeof(id)) && find_cached(id, out, max)) {
        return 1;
    }
    return 0;
}

static DWORD WINAPI
worker_main(void *param)
{
    (void)param;
    char url[MEDIA_URL_MAX];
    EnterCriticalSection(&g_lock);
    copy_str(url, (int)sizeof(url), g_url);
    LeaveCriticalSection(&g_lock);

    char path[MEDIA_URL_MAX];
    path[0] = '\0';
    int ok = 0;
    if (!InterlockedCompareExchange(&g_cancel, 0, 0)) {
        ok = resolve_source(url, path, (int)sizeof(path));
    }

    EnterCriticalSection(&g_lock);
    if (ok && !InterlockedCompareExchange(&g_cancel, 0, 0)) {
        copy_str(g_pending_path, (int)sizeof(g_pending_path), path);
    } else {
        g_pending_path[0] = '\0';
        InterlockedExchange(&g_want_play, 0);
    }
    LeaveCriticalSection(&g_lock);
    InterlockedExchange(&g_busy, 0);
    return 0;
}

static void
stop_capture(void)
{
    InterlockedExchange(&g_cap_run, 0);
    if (g_cap_thread) {
        WaitForSingleObject(g_cap_thread, 1000);
        CloseHandle(g_cap_thread);
        g_cap_thread = NULL;
    }
    g_level = 0.0f;
}

static DWORD WINAPI
capture_main(void *param)
{
    (void)param;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    IAudioClient *client = NULL;
    IAudioCaptureClient *capture = NULL;
    WAVEFORMATEX *format = NULL;
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
        hr = client->GetMixFormat(&format);
    }
    if (SUCCEEDED(hr)) {
        hr = client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK,
            200000,
            0,
            format,
            NULL
        );
    }
    if (SUCCEEDED(hr)) {
        hr = client->GetService(__uuidof(IAudioCaptureClient), (void **)&capture);
    }
    if (SUCCEEDED(hr)) {
        hr = client->Start();
    }
    while (SUCCEEDED(hr) && InterlockedCompareExchange(&g_cap_run, 0, 0)) {
        UINT32 packet = 0;
        if (FAILED(capture->GetNextPacketSize(&packet))) {
            break;
        }
        float peak = 0.0f;
        while (packet > 0) {
            BYTE *data = NULL;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, NULL, NULL))) {
                break;
            }
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data && format) {
                int channels = format->nChannels > 0 ? format->nChannels : 1;
                if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                    (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->wBitsPerSample == 32)) {
                    const float *samples = (const float *)data;
                    for (UINT32 i = 0; i < frames; ++i) {
                        float s = samples[i * (UINT32)channels];
                        if (s < 0.0f) {
                            s = -s;
                        }
                        if (s > peak) {
                            peak = s;
                        }
                    }
                }
            }
            capture->ReleaseBuffer(frames);
            if (FAILED(capture->GetNextPacketSize(&packet))) {
                break;
            }
        }
        if (peak > 1.0f) {
            peak = 1.0f;
        }
        g_level = peak;
        Sleep(8);
    }
    if (client) {
        client->Stop();
    }
    if (format) {
        CoTaskMemFree(format);
    }
    if (capture) {
        capture->Release();
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
    CoUninitialize();
    return 0;
}

static void
start_capture(void)
{
    if (g_cap_thread) {
        return;
    }
    InterlockedExchange(&g_cap_run, 1);
    g_cap_thread = CreateThread(NULL, 0, capture_main, NULL, 0, NULL);
}

static void
destroy_player(void)
{
    if (g_player) {
        g_player->Stop();
        g_player->Shutdown();
        g_player->Release();
        g_player = NULL;
    }
    if (g_callback) {
        g_callback->Release();
        g_callback = NULL;
    }
    InterlockedExchange(&g_player_playing, 0);
    stop_capture();
}

static void
play_path(const char *path)
{
    if (!path || !path[0]) {
        return;
    }
    wchar_t wide[MEDIA_URL_MAX];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, MEDIA_URL_MAX)) {
        MultiByteToWideChar(CP_ACP, 0, path, -1, wide, MEDIA_URL_MAX);
    }
    destroy_player();
    g_callback = new PlayerCallback();
    HRESULT hr = MFPCreateMediaPlayer(wide, TRUE, 0, g_callback, NULL, &g_player);
    if (FAILED(hr) || !g_player) {
        destroy_player();
        InterlockedExchange(&g_want_play, 0);
        return;
    }
    copy_str(g_ready_path, (int)sizeof(g_ready_path), path);
    InterlockedExchange(&g_player_playing, 1);
    InterlockedExchange(&g_ended, 0);
    start_capture();
}

static void
start_resolve(void)
{
    if (g_worker) {
        WaitForSingleObject(g_worker, 50);
        if (WaitForSingleObject(g_worker, 0) == WAIT_OBJECT_0) {
            CloseHandle(g_worker);
            g_worker = NULL;
        }
    }
    if (g_worker) {
        return;
    }
    InterlockedExchange(&g_cancel, 0);
    InterlockedExchange(&g_busy, 1);
    g_pending_path[0] = '\0';
    g_worker = CreateThread(NULL, 0, worker_main, NULL, 0, NULL);
}

void
media_init(void *hwnd, const char *project_root)
{
    ensure_lock();
    copy_str(g_root, (int)sizeof(g_root), project_root);
    if (!g_com) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr) || hr == S_FALSE) {
            g_com = 1;
        }
    }
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    video_bg_init();
    ytstream_init(project_root);
    (void)hwnd;
}

void
media_shutdown(void)
{
    InterlockedExchange(&g_cancel, 1);
    InterlockedExchange(&g_want_play, 0);
    if (g_worker) {
        WaitForSingleObject(g_worker, 4000);
        CloseHandle(g_worker);
        g_worker = NULL;
    }
    destroy_player();
    ytstream_shutdown();
    video_bg_shutdown();
    MFShutdown();
    if (g_com) {
        CoUninitialize();
        g_com = 0;
    }
}

void
media_poll(void)
{
    ytstream_poll();
    video_bg_tick();
    ensure_lock();
    if (g_worker && WaitForSingleObject(g_worker, 0) == WAIT_OBJECT_0) {
        CloseHandle(g_worker);
        g_worker = NULL;
    }

    char pending[MEDIA_URL_MAX];
    pending[0] = '\0';
    EnterCriticalSection(&g_lock);
    if (g_pending_path[0] && InterlockedCompareExchange(&g_want_play, 0, 0)) {
        copy_str(pending, (int)sizeof(pending), g_pending_path);
        g_pending_path[0] = '\0';
    }
    LeaveCriticalSection(&g_lock);

    if (pending[0]) {
        play_path(pending);
    }

    LONG ended = InterlockedCompareExchange(&g_ended, 0, 2);
    if (ended == 2) {
        if (g_player && InterlockedCompareExchange(&g_want_play, 0, 0)) {
            PROPVARIANT pos;
            PropVariantInit(&pos);
            pos.vt = VT_I8;
            pos.hVal.QuadPart = 0;
            HRESULT hr = g_player->SetPosition(MFP_POSITIONTYPE_100NS, &pos);
            PropVariantClear(&pos);
            if (SUCCEEDED(hr) && SUCCEEDED(g_player->Play())) {
                InterlockedExchange(&g_player_playing, 1);
                start_capture();
            } else if (g_ready_path[0]) {
                play_path(g_ready_path);
            }
        }
    } else if (InterlockedCompareExchange(&g_ended, 0, 1)) {
        InterlockedExchange(&g_want_play, 0);
        InterlockedExchange(&g_player_playing, 0);
        stop_capture();
    }

    if (!InterlockedCompareExchange(&g_player_playing, 0, 0)) {
        if (!InterlockedCompareExchange(&g_busy, 0, 0)) {
            g_level *= 0.82f;
            if (g_level < 0.002f) {
                g_level = 0.0f;
            }
        }
    }
}

void
media_set_url(const char *url)
{
    ensure_lock();
    EnterCriticalSection(&g_lock);
    copy_str(g_url, (int)sizeof(g_url), url);
    LeaveCriticalSection(&g_lock);
}

void
media_set_loop(int loop)
{
    InterlockedExchange(&g_loop, loop ? 1 : 0);
    ytstream_set_loop(loop);
}

void
media_set_volume(float volume)
{
    ytstream_set_volume(volume);
}

void
media_set_playing(int playing)
{
    ensure_lock();
    if (!playing) {
        InterlockedExchange(&g_want_play, 0);
        InterlockedExchange(&g_cancel, 1);
        ytstream_stop();
        if (g_player) {
            g_player->Pause();
        }
        InterlockedExchange(&g_player_playing, 0);
        stop_capture();
        return;
    }

    InterlockedExchange(&g_want_play, 1);
    InterlockedExchange(&g_ended, 0);

    char url[MEDIA_URL_MAX];
    EnterCriticalSection(&g_lock);
    copy_str(url, (int)sizeof(url), g_url);
    LeaveCriticalSection(&g_lock);

    if (!url[0]) {
        InterlockedExchange(&g_want_play, 0);
        return;
    }

    if (ytstream_handles_url(url)) {
        destroy_player();
        stop_capture();
        ytstream_play(url);
        return;
    }

    if (g_player && g_ready_path[0] &&
        (strcmp(g_ready_path, url) == 0 || is_direct_audio(url))) {
        g_player->Play();
        InterlockedExchange(&g_player_playing, 1);
        start_capture();
        return;
    }

    if (is_direct_audio(url) || !is_http(url)) {
        play_path(url);
        return;
    }

    start_resolve();
}

int
media_is_playing(void)
{
    if (ytstream_playing()) {
        return 1;
    }
    return InterlockedCompareExchange(&g_player_playing, 0, 0) ? 1 : 0;
}

int
media_is_busy(void)
{
    if (ytstream_busy()) {
        return 1;
    }
    return InterlockedCompareExchange(&g_busy, 0, 0) ? 1 : 0;
}

void
media_embed_set_view(int x, int y, int w, int h, int visible)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)visible;
}

float
media_level(void)
{
    if (ytstream_playing() || ytstream_busy()) {
        float level = ytstream_level();
        if (ytstream_playing() && level < 0.03f) {
            static float phase;
            phase += 0.07f;
            level = 0.08f + 0.05f * (0.5f + 0.5f * sinf(phase));
        }
        return level;
    }
    float level = g_level;
    if (level < 0.0f) {
        return 0.0f;
    }
    if (level > 1.0f) {
        return 1.0f;
    }
    if (media_is_playing() && level < 0.03f) {
        static float phase;
        phase += 0.07f;
        level = 0.08f + 0.05f * (0.5f + 0.5f * sinf(phase));
    }
    return level;
}
