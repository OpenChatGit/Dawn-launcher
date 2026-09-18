#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00

#include "debug_console.h"
#include "video_bg.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfmediaengine.h>

#define VIDEO_URL_MAX 4096
#define VIDEO_MAX_EDGE 1280
#define VIDEO_SLOTS 3
#define VIDEO_GPU_BUFFERS 2

static CRITICAL_SECTION g_ctrl;
static int g_ready;
static char g_url[VIDEO_URL_MAX];
static char g_active[VIDEO_URL_MAX];
static int g_loop = 1;
static int g_muted;
static float g_volume = 1.0f;
static int g_active_loop = 1;

static ID3D11Device *g_d3d;
static ID3D11DeviceContext *g_ctx;
static IMFDXGIDeviceManager *g_dxgi;
static UINT g_reset_token;
static IMFMediaEngine *g_engine;
static IMFMediaEngineNotify *g_notify;
static ID3D11Texture2D *g_gpu[VIDEO_GPU_BUFFERS];
static ID3D11Texture2D *g_staging;
static IMFMediaBuffer *g_surface[VIDEO_GPU_BUFFERS];
static HANDLE g_share[VIDEO_GPU_BUFFERS];
static int g_use_share;
static int g_tex_w;
static int g_tex_h;
static int g_want_w;
static int g_want_h;
static int g_write;
static volatile LONG g_shown;
static HANDLE g_thread;
static HANDLE g_wake;
static volatile LONG g_thread_run;
static volatile LONG g_reload;
static volatile LONG g_gain_dirty;

static uint8_t *g_buf[VIDEO_SLOTS];
static int g_buf_w;
static int g_buf_h;
static int g_fstride;
static volatile LONG g_published;
static volatile LONG g_locked = -1;
static volatile LONG g_have;
static volatile LONG g_gen;

class EngineNotify : public IMFMediaEngineNotify {
    LONG refs;
public:
    EngineNotify() : refs(1) {}
    virtual ~EngineNotify() {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv)
    {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IMFMediaEngineNotify) {
            *ppv = static_cast<IMFMediaEngineNotify *>(this);
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
    HRESULT STDMETHODCALLTYPE EventNotify(DWORD event, DWORD_PTR param1, DWORD param2)
    {
        (void)param1;
        (void)param2;
        if (event == MF_MEDIA_ENGINE_EVENT_CANPLAY ||
            event == MF_MEDIA_ENGINE_EVENT_LOADEDDATA ||
            event == MF_MEDIA_ENGINE_EVENT_FIRSTFRAMEREADY) {
            if (g_engine) {
                g_engine->Play();
            }
        } else if (event == MF_MEDIA_ENGINE_EVENT_ENDED) {
            if (g_loop && g_engine) {
                g_engine->SetCurrentTime(0.0);
                g_engine->Play();
            }
        }
        return S_OK;
    }
};

static void
ensure_locks(void)
{
    if (g_ready) {
        return;
    }
    InitializeCriticalSection(&g_ctrl);
    g_ready = 1;
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

static void
to_wide(const char *in, wchar_t *out, int max)
{
    if (!out || max <= 0) {
        return;
    }
    out[0] = 0;
    if (!in) {
        return;
    }
    if (!MultiByteToWideChar(CP_UTF8, 0, in, -1, out, max)) {
        MultiByteToWideChar(CP_ACP, 0, in, -1, out, max);
    }
}

static void
fit_edge(UINT32 *w, UINT32 *h, UINT32 max_edge)
{
    if (!w || !h || *w == 0 || *h == 0) {
        return;
    }
    if (*w <= max_edge && *h <= max_edge) {
        return;
    }
    if (*w >= *h) {
        *h = (UINT32)((unsigned long long)*h * max_edge / *w);
        *w = max_edge;
    } else {
        *w = (UINT32)((unsigned long long)*w * max_edge / *h);
        *h = max_edge;
    }
    *w &= ~1u;
    *h &= ~1u;
    if (*w == 0) {
        *w = 2;
    }
    if (*h == 0) {
        *h = 2;
    }
}

static void
free_cpu(void)
{
    for (int i = 0; i < VIDEO_SLOTS; ++i) {
        free(g_buf[i]);
        g_buf[i] = NULL;
    }
    g_buf_w = 0;
    g_buf_h = 0;
    g_fstride = 0;
    g_have = 0;
    g_published = 0;
    g_locked = -1;
}

static int
ensure_cpu(int w, int h)
{
    if (w <= 0 || h <= 0) {
        return 0;
    }
    if (g_buf[0] && g_buf_w == w && g_buf_h == h) {
        return 1;
    }
    free_cpu();
    size_t bytes = (size_t)w * (size_t)h * 4u;
    for (int i = 0; i < VIDEO_SLOTS; ++i) {
        g_buf[i] = (uint8_t *)malloc(bytes);
        if (!g_buf[i]) {
            free_cpu();
            return 0;
        }
    }
    g_buf_w = w;
    g_buf_h = h;
    g_fstride = w * 4;
    return 1;
}

static void
free_textures(void)
{
    for (int i = 0; i < VIDEO_GPU_BUFFERS; ++i) {
        if (g_surface[i]) {
            g_surface[i]->Release();
            g_surface[i] = NULL;
        }
        if (g_gpu[i]) {
            g_gpu[i]->Release();
            g_gpu[i] = NULL;
        }
        g_share[i] = NULL;
    }
    if (g_staging) {
        g_staging->Release();
        g_staging = NULL;
    }
    g_use_share = 0;
    g_tex_w = 0;
    g_tex_h = 0;
    g_write = 0;
    InterlockedExchange(&g_shown, 0);
}

static HANDLE
share_of(ID3D11Texture2D *tex)
{
    if (!tex) {
        return NULL;
    }
    IDXGIResource *res = NULL;
    if (FAILED(tex->QueryInterface(__uuidof(IDXGIResource), (void **)&res)) || !res) {
        return NULL;
    }
    HANDLE handle = NULL;
    HRESULT hr = res->GetSharedHandle(&handle);
    res->Release();
    if (FAILED(hr) || !handle) {
        return NULL;
    }
    return handle;
}

static int
ensure_textures(int w, int h)
{
    if (w <= 0 || h <= 0 || !g_d3d) {
        return 0;
    }
    if (g_gpu[0] && g_tex_w == w && g_tex_h == h && (g_use_share || g_staging)) {
        return 1;
    }
    free_textures();

    D3D11_TEXTURE2D_DESC gpu;
    memset(&gpu, 0, sizeof(gpu));
    gpu.Width = (UINT)w;
    gpu.Height = (UINT)h;
    gpu.MipLevels = 1;
    gpu.ArraySize = 1;
    gpu.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    gpu.SampleDesc.Count = 1;
    gpu.Usage = D3D11_USAGE_DEFAULT;
    gpu.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    gpu.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    int shared_ok = 1;
    for (int i = 0; i < VIDEO_GPU_BUFFERS; ++i) {
        if (FAILED(g_d3d->CreateTexture2D(&gpu, NULL, &g_gpu[i])) || !g_gpu[i]) {
            shared_ok = 0;
            break;
        }
        g_share[i] = share_of(g_gpu[i]);
        if (!g_share[i]) {
            shared_ok = 0;
            break;
        }
        MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), g_gpu[i], 0, FALSE, &g_surface[i]);
    }

    if (!shared_ok) {
        free_textures();
        gpu.MiscFlags = 0;
        if (FAILED(g_d3d->CreateTexture2D(&gpu, NULL, &g_gpu[0])) || !g_gpu[0]) {
            return 0;
        }
        D3D11_TEXTURE2D_DESC staging = gpu;
        staging.Usage = D3D11_USAGE_STAGING;
        staging.BindFlags = 0;
        staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging.MiscFlags = 0;
        if (FAILED(g_d3d->CreateTexture2D(&staging, NULL, &g_staging)) || !g_staging) {
            free_textures();
            return 0;
        }
        if (!ensure_cpu(w, h)) {
            free_textures();
            return 0;
        }
        g_use_share = 0;
    } else {
        g_use_share = 1;
        debug_log("video GPU double-buffer %dx%d", w, h);
    }

    g_tex_w = w;
    g_tex_h = h;
    return 1;
}

static int
ensure_d3d(void)
{
    if (g_d3d && g_ctx && g_dxgi) {
        return 1;
    }
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    HRESULT hr = D3D11CreateDevice(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        levels,
        4,
        D3D11_SDK_VERSION,
        &g_d3d,
        NULL,
        &g_ctx
    );
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            NULL,
            D3D_DRIVER_TYPE_HARDWARE,
            NULL,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels,
            4,
            D3D11_SDK_VERSION,
            &g_d3d,
            NULL,
            &g_ctx
        );
    }
    if (FAILED(hr) || !g_d3d) {
        return 0;
    }
    ID3D10Multithread *mt = NULL;
    if (SUCCEEDED(g_d3d->QueryInterface(__uuidof(ID3D10Multithread), (void **)&mt)) && mt) {
        mt->SetMultithreadProtected(TRUE);
        mt->Release();
    }
    if (FAILED(MFCreateDXGIDeviceManager(&g_reset_token, &g_dxgi)) || !g_dxgi) {
        return 0;
    }
    if (FAILED(g_dxgi->ResetDevice(g_d3d, g_reset_token))) {
        return 0;
    }
    return 1;
}

static void
destroy_engine(void)
{
    if (g_engine) {
        g_engine->Shutdown();
        g_engine->Release();
        g_engine = NULL;
    }
    if (g_notify) {
        g_notify->Release();
        g_notify = NULL;
    }
    free_textures();
}

static BSTR
make_source(const char *url)
{
    wchar_t wide[VIDEO_URL_MAX];
    to_wide(url, wide, VIDEO_URL_MAX);
    if (is_http(url)) {
        return SysAllocString(wide);
    }
    for (wchar_t *p = wide; *p; ++p) {
        if (*p == L'/') {
            *p = L'\\';
        }
    }
    wchar_t fileurl[VIDEO_URL_MAX + 16];
    swprintf(fileurl, VIDEO_URL_MAX + 16, L"file:///%s", wide);
    for (wchar_t *p = fileurl; *p; ++p) {
        if (*p == L'\\') {
            *p = L'/';
        }
    }
    return SysAllocString(fileurl);
}

static int
create_engine(const char *url, int loop)
{
    destroy_engine();
    if (!url || !url[0] || !ensure_d3d()) {
        return 0;
    }

    EngineNotify *notify = new EngineNotify();
    g_notify = notify;

    IMFAttributes *attrs = NULL;
    if (FAILED(MFCreateAttributes(&attrs, 4)) || !attrs) {
        destroy_engine();
        return 0;
    }
    attrs->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify);
    attrs->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, g_dxgi);
    attrs->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
    attrs->SetUINT32(MF_MEDIA_ENGINE_SYNCHRONOUS_CLOSE, TRUE);

    IMFMediaEngineClassFactory *factory = NULL;
    HRESULT hr = CoCreateInstance(
        CLSID_MFMediaEngineClassFactory,
        NULL,
        CLSCTX_INPROC_SERVER,
        __uuidof(IMFMediaEngineClassFactory),
        (void **)&factory
    );
    if (SUCCEEDED(hr) && factory) {
        hr = factory->CreateInstance(0, attrs, &g_engine);
        factory->Release();
    }
    attrs->Release();
    if (FAILED(hr) || !g_engine) {
        destroy_engine();
        return 0;
    }

    g_engine->SetAutoPlay(TRUE);
    g_engine->SetLoop(loop ? TRUE : FALSE);
    g_engine->SetMuted(g_muted ? TRUE : FALSE);
    g_engine->SetVolume(g_muted ? 0.0 : (double)g_volume);
    g_engine->SetPlaybackRate(1.0);

    BSTR source = make_source(url);
    if (!source) {
        destroy_engine();
        return 0;
    }
    hr = g_engine->SetSource(source);
    SysFreeString(source);
    if (FAILED(hr)) {
        destroy_engine();
        return 0;
    }
    InterlockedExchange(&g_gain_dirty, 1);
    g_engine->Load();
    g_engine->Play();
    return 1;
}

static void
publish_mapped(const uint8_t *src, int pitch, int w, int h)
{
    if (!ensure_cpu(w, h) || !src) {
        return;
    }
    LONG published = InterlockedCompareExchange(&g_published, 0, 0);
    LONG locked = InterlockedCompareExchange(&g_locked, 0, 0);
    int slot = (int)((published + 1) % VIDEO_SLOTS);
    if (slot == locked) {
        slot = (slot + 1) % VIDEO_SLOTS;
    }
    uint8_t *dst = g_buf[slot];
    int dst_stride = w * 4;
    int copy = dst_stride < pitch ? dst_stride : pitch;
    for (int y = 0; y < h; ++y) {
        memcpy(dst + y * dst_stride, src + y * pitch, (size_t)copy);
    }
    InterlockedExchange(&g_published, slot);
    InterlockedExchange(&g_have, 1);
    InterlockedIncrement(&g_gen);
}

static UINT32
output_max_edge(void)
{
    UINT32 edge = VIDEO_MAX_EDGE;
    if (g_want_w > 0 && g_want_h > 0) {
        UINT32 span = g_want_w > g_want_h ? (UINT32)g_want_w : (UINT32)g_want_h;
        if (span < 640) {
            span = 640;
        }
        if (span < edge) {
            edge = span;
        }
    }
    return edge;
}

static void
pump_frame(void)
{
    if (!g_engine || !g_ctx) {
        return;
    }
    if (InterlockedCompareExchange(&g_gain_dirty, 0, 1)) {
        g_engine->SetMuted(g_muted ? TRUE : FALSE);
        g_engine->SetVolume(g_muted ? 0.0 : (double)g_volume);
    }
    LONGLONG pts = 0;
    if (g_engine->OnVideoStreamTick(&pts) != S_OK) {
        if (g_loop && g_engine->IsEnded()) {
            g_engine->SetCurrentTime(0.0);
            g_engine->Play();
        }
        return;
    }

    DWORD nw = 0;
    DWORD nh = 0;
    if (FAILED(g_engine->GetNativeVideoSize(&nw, &nh)) || nw == 0 || nh == 0) {
        return;
    }
    UINT32 ow = nw;
    UINT32 oh = nh;
    fit_edge(&ow, &oh, output_max_edge());
    if (!ensure_textures((int)ow, (int)oh) || !g_gpu[0]) {
        return;
    }

    int dest_slot = g_use_share ? g_write : 0;
    ID3D11Texture2D *dest_tex = g_gpu[dest_slot];
    if (!dest_tex) {
        return;
    }
    RECT dest = {0, 0, g_tex_w, g_tex_h};
    MFARGB clear = {0, 0, 0, 255};
    IUnknown *target = (IUnknown *)dest_tex;
    HRESULT hr = g_engine->TransferVideoFrame(target, NULL, &dest, &clear);
    if (FAILED(hr) && g_surface[dest_slot]) {
        target = (IUnknown *)g_surface[dest_slot];
        hr = g_engine->TransferVideoFrame(target, NULL, &dest, &clear);
    }
    if (FAILED(hr)) {
        return;
    }

    if (g_use_share) {
        InterlockedExchange(&g_shown, dest_slot);
        InterlockedExchange(&g_have, 1);
        InterlockedIncrement(&g_gen);
        g_write = dest_slot ^ 1;
        return;
    }
    if (!g_staging) {
        return;
    }
    g_ctx->CopyResource(g_staging, dest_tex);
    D3D11_MAPPED_SUBRESOURCE mapped;
    memset(&mapped, 0, sizeof(mapped));
    if (FAILED(g_ctx->Map(g_staging, 0, D3D11_MAP_READ, 0, &mapped)) || !mapped.pData) {
        return;
    }
    publish_mapped((const uint8_t *)mapped.pData, (int)mapped.RowPitch, g_tex_w, g_tex_h);
    g_ctx->Unmap(g_staging, 0);
}

static void
apply_pending_source(void)
{
    if (!InterlockedCompareExchange(&g_reload, 0, 0)) {
        return;
    }
    EnterCriticalSection(&g_ctrl);
    char url[VIDEO_URL_MAX];
    copy_str(url, (int)sizeof(url), g_url);
    int loop = g_loop;
    InterlockedExchange(&g_reload, 0);
    if (url[0]) {
        if (!create_engine(url, loop)) {
            OutputDebugStringA("video_bg: media engine failed\n");
            debug_log("video engine failed");
        }
    } else {
        destroy_engine();
        InterlockedExchange(&g_have, 0);
    }
    LeaveCriticalSection(&g_ctrl);
}

static DWORD WINAPI
video_thread_main(void *param)
{
    (void)param;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    while (InterlockedCompareExchange(&g_thread_run, 0, 0)) {
        apply_pending_source();
        EnterCriticalSection(&g_ctrl);
        IMFMediaEngine *engine = g_engine;
        LeaveCriticalSection(&g_ctrl);
        if (engine) {
            pump_frame();
        }
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        HANDLE wake = g_wake;
        if (wake) {
            MsgWaitForMultipleObjects(1, &wake, FALSE, engine ? 8 : 16, QS_ALLINPUT);
        } else {
            Sleep(engine ? 8 : 16);
        }
    }
    EnterCriticalSection(&g_ctrl);
    destroy_engine();
    LeaveCriticalSection(&g_ctrl);
    CoUninitialize();
    return 0;
}

void
video_bg_tick(void)
{
    if (g_wake) {
        SetEvent(g_wake);
    }
}

void
video_bg_init(void)
{
    ensure_locks();
    ensure_d3d();
    if (!g_wake) {
        g_wake = CreateEventA(NULL, FALSE, FALSE, NULL);
    }
    if (!g_thread) {
        InterlockedExchange(&g_thread_run, 1);
        g_thread = CreateThread(NULL, 0, video_thread_main, NULL, 0, NULL);
    }
}

void
video_bg_shutdown(void)
{
    if (!g_ready) {
        return;
    }
    InterlockedExchange(&g_thread_run, 0);
    if (g_wake) {
        SetEvent(g_wake);
    }
    if (g_thread) {
        WaitForSingleObject(g_thread, 4000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    if (g_wake) {
        CloseHandle(g_wake);
        g_wake = NULL;
    }
    EnterCriticalSection(&g_ctrl);
    destroy_engine();
    g_url[0] = '\0';
    g_active[0] = '\0';
    LeaveCriticalSection(&g_ctrl);
    free_cpu();
    if (g_dxgi) {
        g_dxgi->Release();
        g_dxgi = NULL;
    }
    if (g_ctx) {
        g_ctx->Release();
        g_ctx = NULL;
    }
    if (g_d3d) {
        g_d3d->Release();
        g_d3d = NULL;
    }
}

void
video_bg_set_source(const char *url, int loop)
{
    ensure_locks();
    if (!url) {
        url = "";
    }
    int want_loop = loop ? 1 : 0;
    EnterCriticalSection(&g_ctrl);
    if (strcmp(g_active, url) == 0 && g_active_loop == want_loop) {
        LeaveCriticalSection(&g_ctrl);
        return;
    }
    copy_str(g_url, (int)sizeof(g_url), url);
    copy_str(g_active, (int)sizeof(g_active), url);
    g_loop = want_loop;
    g_active_loop = want_loop;
    InterlockedExchange(&g_reload, 1);
    LeaveCriticalSection(&g_ctrl);
    if (g_wake) {
        SetEvent(g_wake);
    }
}

void
video_bg_set_gain(int muted, float volume)
{
    ensure_locks();
    if (volume < 0.0f) {
        volume = 0.0f;
    }
    if (volume > 1.0f) {
        volume = 1.0f;
    }
    int want_mute = muted ? 1 : 0;
    EnterCriticalSection(&g_ctrl);
    if (g_muted == want_mute && g_volume == volume) {
        LeaveCriticalSection(&g_ctrl);
        return;
    }
    g_muted = want_mute;
    g_volume = volume;
    InterlockedExchange(&g_gain_dirty, 1);
    LeaveCriticalSection(&g_ctrl);
    if (g_wake) {
        SetEvent(g_wake);
    }
}

void
video_bg_set_output(int w, int h)
{
    if (w < 2) {
        w = 2;
    }
    if (h < 2) {
        h = 2;
    }
    g_want_w = w;
    g_want_h = h;
}

void *
video_bg_shared_handle(void)
{
    if (!g_use_share || !InterlockedCompareExchange(&g_have, 0, 0)) {
        return NULL;
    }
    LONG slot = InterlockedCompareExchange(&g_shown, 0, 0);
    if (slot < 0 || slot >= VIDEO_GPU_BUFFERS || !g_share[slot]) {
        return NULL;
    }
    return (void *)g_share[slot];
}

int
video_bg_frame_size(int *w, int *h)
{
    if (!w || !h || g_tex_w <= 0 || g_tex_h <= 0) {
        return 0;
    }
    *w = g_tex_w;
    *h = g_tex_h;
    return 1;
}

int
video_bg_lock_frame(const unsigned char **bgra, int *w, int *h, int *stride)
{
    if (!g_ready || !bgra || !w || !h || !stride) {
        return 0;
    }
    if (!InterlockedCompareExchange(&g_have, 0, 0)) {
        return 0;
    }
    LONG slot = InterlockedCompareExchange(&g_published, 0, 0);
    if (slot < 0 || slot >= VIDEO_SLOTS || !g_buf[slot]) {
        return 0;
    }
    InterlockedExchange(&g_locked, slot);
    *bgra = g_buf[slot];
    *w = g_buf_w;
    *h = g_buf_h;
    *stride = g_fstride;
    return 1;
}

void
video_bg_unlock_frame(void)
{
    InterlockedExchange(&g_locked, -1);
}

unsigned
video_bg_generation(void)
{
    return (unsigned)InterlockedCompareExchange(&g_gen, 0, 0);
}
