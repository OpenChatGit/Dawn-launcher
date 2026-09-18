#include "theme_desc.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <initguid.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>

using namespace D2D1;

template <typename T>
static void
release_ptr(T **ptr)
{
    if (ptr && *ptr) {
        (*ptr)->Release();
        *ptr = NULL;
    }
}

static ID2D1Factory1 *g_factory;
static ID3D11Device *g_d3d;
static ID3D11DeviceContext *g_d3d_ctx;
static ID2D1Device *g_device;
static ID2D1DeviceContext *g_dc;
static ID3D11Texture2D *g_tex;
static ID3D11Texture2D *g_readback[2];
static ID2D1Bitmap1 *g_target;
static ID2D1Bitmap1 *g_glow;
static ID2D1Effect *g_blur_outer;
static ID2D1Effect *g_blur_inner;
static ID2D1StrokeStyle *g_stroke;
static int g_started;
static int g_target_w;
static int g_target_h;
static int g_rb_i;
static int g_rb_ready;

typedef struct EmblemCache {
    ID2D1Bitmap1 *bitmap;
    char path[THEME_PATH_MAX];
    uint32_t color;
    int cache_size;
} EmblemCache;

static EmblemCache g_emblems[THEME_MAX_EMBLEMS];
static ID2D1Bitmap *g_video_bmp;
static ID2D1Bitmap1 *g_video_gpu;
static ID3D11Texture2D *g_video_opened;
static void *g_video_share;
static int g_video_w;
static int g_video_h;
static unsigned g_video_gen;
static float g_audio_level;

static D2D1_COLOR_F
color_rgb(uint32_t rgb, float alpha)
{
    if (alpha < 0.0f) {
        alpha = 0.0f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    return ColorF(
        ((rgb >> 16) & 0xff) / 255.0f,
        ((rgb >> 8) & 0xff) / 255.0f,
        (rgb & 0xff) / 255.0f,
        alpha
    );
}

static const char *
skip_sep(const char *s)
{
    while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t' || *s == ',') {
        s++;
    }
    return s;
}

static const char *
read_num(const char *s, float *out)
{
    s = skip_sep(s);
    char *end = NULL;
    *out = strtof(s, &end);
    return end ? end : s;
}

static char *
read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0 || size > 400000) {
        fclose(file);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(file);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)size, file);
    fclose(file);
    buf[n] = '\0';
    return buf;
}

static HRESULT
path_from_svg(const char *d, ID2D1GeometrySink *sink)
{
    sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
    const char *s = d;
    int started = 0;
    float last_x = 0.0f;
    float last_y = 0.0f;

    while (*s) {
        s = skip_sep(s);
        if (!*s) {
            break;
        }
        char cmd = *s;
        if (cmd == 'M' || cmd == 'm' || cmd == 'L' || cmd == 'l' || cmd == 'Z' || cmd == 'z') {
            s++;
        } else if ((cmd >= '0' && cmd <= '9') || cmd == '-' || cmd == '.') {
            cmd = started ? 'L' : 'M';
        } else {
            s++;
            continue;
        }

        if (cmd == 'Z' || cmd == 'z') {
            if (started) {
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                started = 0;
            }
            continue;
        }

        float x = 0.0f;
        float y = 0.0f;
        s = read_num(s, &x);
        s = read_num(s, &y);
        D2D1_POINT_2F pt = Point2F(x, y);
        if (cmd == 'M' || cmd == 'm') {
            if (started) {
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
            sink->BeginFigure(pt, D2D1_FIGURE_BEGIN_FILLED);
            last_x = x;
            last_y = y;
            started = 1;
        } else if (started) {
            sink->AddLine(pt);
            last_x = x;
            last_y = y;
        }
    }
    if (started) {
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }
    (void)last_x;
    (void)last_y;
    return S_OK;
}

static void
free_targets(void)
{
    if (g_dc) {
        g_dc->SetTarget(NULL);
    }
    release_ptr(&g_blur_outer);
    release_ptr(&g_blur_inner);
    release_ptr(&g_glow);
    release_ptr(&g_target);
    release_ptr(&g_tex);
    release_ptr(&g_readback[0]);
    release_ptr(&g_readback[1]);
    g_target_w = 0;
    g_target_h = 0;
    g_rb_i = 0;
    g_rb_ready = 0;
}

static void
free_emblem(void)
{
    for (int i = 0; i < THEME_MAX_EMBLEMS; ++i) {
        release_ptr(&g_emblems[i].bitmap);
        g_emblems[i].path[0] = '\0';
        g_emblems[i].color = 0;
        g_emblems[i].cache_size = 0;
    }
}

static void
free_video_gpu(void)
{
    release_ptr(&g_video_gpu);
    release_ptr(&g_video_opened);
    g_video_share = NULL;
}

static void
free_video_bmp(void)
{
    release_ptr(&g_video_bmp);
    free_video_gpu();
    g_video_w = 0;
    g_video_h = 0;
    g_video_gen = 0;
}

static int
bind_video_share(void *handle, int w, int h)
{
    if (!handle || !g_d3d || !g_dc || w <= 0 || h <= 0) {
        return 0;
    }
    if (handle == g_video_share && g_video_gpu && g_video_w == w && g_video_h == h) {
        return 1;
    }
    free_video_gpu();
    ID3D11Texture2D *tex = NULL;
    if (FAILED(g_d3d->OpenSharedResource(handle, __uuidof(ID3D11Texture2D), (void **)&tex)) || !tex) {
        OutputDebugStringA("video: OpenSharedResource failed\n");
        return 0;
    }
    IDXGISurface *surface = NULL;
    if (FAILED(tex->QueryInterface(__uuidof(IDXGISurface), (void **)&surface)) || !surface) {
        tex->Release();
        return 0;
    }
    D2D1_BITMAP_PROPERTIES1 props = BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        96.0f,
        96.0f
    );
    ID2D1Bitmap1 *bmp = NULL;
    HRESULT hr = g_dc->CreateBitmapFromDxgiSurface(surface, &props, &bmp);
    if (FAILED(hr) || !bmp) {
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        hr = g_dc->CreateBitmapFromDxgiSurface(surface, &props, &bmp);
    }
    surface->Release();
    if (FAILED(hr) || !bmp) {
        tex->Release();
        OutputDebugStringA("video: CreateBitmapFromDxgiSurface failed\n");
        return 0;
    }
    g_video_opened = tex;
    g_video_gpu = bmp;
    g_video_share = handle;
    g_video_w = w;
    g_video_h = h;
    return 1;
}

static void
draw_video_cover(ID2D1Bitmap *bmp, int width, int height, float opacity)
{
    if (!bmp || !g_dc || g_video_w <= 0 || g_video_h <= 0 || opacity <= 0.0f) {
        return;
    }
    float sx = (float)width / (float)g_video_w;
    float sy = (float)height / (float)g_video_h;
    float s = sx > sy ? sx : sy;
    float dw = (float)g_video_w * s;
    float dh = (float)g_video_h * s;
    float dx = ((float)width - dw) * 0.5f;
    float dy = ((float)height - dh) * 0.5f;
    g_dc->DrawBitmap(
        bmp,
        RectF(dx, dy, dx + dw, dy + dh),
        opacity,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
    );
}

static void
draw_video_bg(Platform *platform, const ThemeDesc *desc, int width, int height)
{
    if (!platform || !g_dc) {
        return;
    }
    float opacity = 1.0f;
    if (desc && desc->background.enabled && desc->background.url[0]) {
        opacity = desc->background.opacity;
    }
    if (opacity <= 0.0f) {
        return;
    }

    void *share = platform->video_shared_handle ? platform->video_shared_handle() : NULL;
    int vw = 0;
    int vh = 0;
    if (share && platform->video_frame_size && platform->video_frame_size(&vw, &vh) &&
        bind_video_share(share, vw, vh) && g_video_gpu) {
        draw_video_cover((ID2D1Bitmap *)g_video_gpu, width, height, opacity);
        return;
    }

    if (!platform->video_lock_frame || !platform->video_unlock_frame) {
        if (g_video_bmp) {
            draw_video_cover(g_video_bmp, width, height, opacity);
        }
        return;
    }
    unsigned gen = platform->video_frame_gen ? platform->video_frame_gen() : 0;
    const unsigned char *bgra = NULL;
    int w = 0;
    int h = 0;
    int stride = 0;
    if (gen != g_video_gen &&
        platform->video_lock_frame(&bgra, &w, &h, &stride) && bgra && w > 0 && h > 0 && stride > 0) {
        if (!g_video_bmp || g_video_w != w || g_video_h != h) {
            release_ptr(&g_video_bmp);
            D2D1_BITMAP_PROPERTIES props = BitmapProperties(
                PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)
            );
            if (SUCCEEDED(g_dc->CreateBitmap(SizeU((UINT32)w, (UINT32)h), bgra, (UINT32)stride, props, &g_video_bmp))) {
                g_video_w = w;
                g_video_h = h;
            }
        } else {
            D2D1_RECT_U rect = {0, 0, (UINT32)w, (UINT32)h};
            g_video_bmp->CopyFromMemory(&rect, bgra, (UINT32)stride);
        }
        g_video_gen = gen;
        platform->video_unlock_frame();
    }
    if (g_video_bmp) {
        draw_video_cover(g_video_bmp, width, height, opacity);
    }
}

static int
gpu_init(void)
{
    if (g_started) {
        return 1;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        flags,
        levels,
        3,
        D3D11_SDK_VERSION,
        &g_d3d,
        NULL,
        &g_d3d_ctx
    );
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            NULL,
            D3D_DRIVER_TYPE_WARP,
            NULL,
            flags,
            levels,
            3,
            D3D11_SDK_VERSION,
            &g_d3d,
            NULL,
            &g_d3d_ctx
        );
    }
    if (FAILED(hr)) {
        return 0;
    }

    IDXGIDevice *dxgi = NULL;
    hr = g_d3d->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi);
    if (FAILED(hr)) {
        return 0;
    }

    D2D1_FACTORY_OPTIONS options = {};
    hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),
        &options,
        (void **)&g_factory
    );
    if (FAILED(hr)) {
        release_ptr(&dxgi);
        return 0;
    }

    hr = g_factory->CreateDevice(dxgi, &g_device);
    release_ptr(&dxgi);
    if (FAILED(hr)) {
        return 0;
    }

    hr = g_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_dc);
    if (FAILED(hr)) {
        return 0;
    }
    g_dc->SetDpi(96.0f, 96.0f);
    g_dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    g_dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    D2D1_STROKE_STYLE_PROPERTIES stroke = StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND,
        D2D1_CAP_STYLE_ROUND,
        D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND,
        10.0f,
        D2D1_DASH_STYLE_SOLID,
        0.0f
    );
    g_factory->CreateStrokeStyle(stroke, NULL, 0, &g_stroke);

    g_started = 1;
    return 1;
}

static int
ensure_targets(int width, int height)
{
    if (width < 2 || height < 2) {
        return 0;
    }
    if (g_target && g_glow && g_target_w == width && g_target_h == height) {
        return 1;
    }

    free_targets();

    D3D11_TEXTURE2D_DESC td;
    memset(&td, 0, sizeof(td));
    td.Width = (UINT)width;
    td.Height = (UINT)height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = 0;

    HRESULT hr = g_d3d->CreateTexture2D(&td, NULL, &g_tex);
    if (FAILED(hr)) {
        return 0;
    }

    IDXGISurface *surface = NULL;
    hr = g_tex->QueryInterface(__uuidof(IDXGISurface), (void **)&surface);
    if (FAILED(hr)) {
        return 0;
    }

    D2D1_BITMAP_PROPERTIES1 props = BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f
    );
    hr = g_dc->CreateBitmapFromDxgiSurface(surface, &props, &g_target);
    release_ptr(&surface);
    if (FAILED(hr)) {
        return 0;
    }

    D2D1_BITMAP_PROPERTIES1 glow_props = BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f
    );
    hr = g_dc->CreateBitmap(SizeU((UINT32)width, (UINT32)height), NULL, 0, glow_props, &g_glow);
    if (FAILED(hr)) {
        return 0;
    }

    g_dc->CreateEffect(CLSID_D2D1GaussianBlur, &g_blur_outer);
    g_dc->CreateEffect(CLSID_D2D1GaussianBlur, &g_blur_inner);
    if (g_blur_outer) {
        g_blur_outer->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, (UINT32)0);
        g_blur_outer->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
    }
    if (g_blur_inner) {
        g_blur_inner->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, (UINT32)0);
        g_blur_inner->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
    }

    g_target_w = width;
    g_target_h = height;
    return 1;
}

static void
present_to_hdc(HDC hdc, int width, int height, uint32_t *pixels)
{
    (void)hdc;
    if (!pixels || !g_d3d || !g_d3d_ctx || !g_tex) {
        return;
    }

    if (!g_readback[0] || !g_readback[1] || g_target_w != width || g_target_h != height) {
        release_ptr(&g_readback[0]);
        release_ptr(&g_readback[1]);
        D3D11_TEXTURE2D_DESC td;
        memset(&td, 0, sizeof(td));
        g_tex->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        td.MiscFlags = 0;
        if (FAILED(g_d3d->CreateTexture2D(&td, NULL, &g_readback[0])) ||
            FAILED(g_d3d->CreateTexture2D(&td, NULL, &g_readback[1]))) {
            release_ptr(&g_readback[0]);
            release_ptr(&g_readback[1]);
            return;
        }
        g_rb_i = 0;
        g_rb_ready = 0;
    }

    int write = g_rb_i;
    int read = write ^ 1;
    g_d3d_ctx->CopyResource(g_readback[write], g_tex);

    int map_i = g_rb_ready ? read : write;
    D3D11_MAPPED_SUBRESOURCE mapped;
    memset(&mapped, 0, sizeof(mapped));
    if (SUCCEEDED(g_d3d_ctx->Map(g_readback[map_i], 0, D3D11_MAP_READ, 0, &mapped)) && mapped.pData) {
        UINT8 *src = (UINT8 *)mapped.pData;
        for (int y = 0; y < height; ++y) {
            memcpy(pixels + y * width, src + y * mapped.RowPitch, sizeof(uint32_t) * (size_t)width);
        }
        g_d3d_ctx->Unmap(g_readback[map_i], 0);
        g_rb_ready = 1;
    }
    g_rb_i = read;
}

static ID2D1PathGeometry *
make_line_geo(const D2D1_POINT_2F *pts, int count, int closed, D2D1_POINT_2F origin)
{
    ID2D1PathGeometry *geo = NULL;
    if (FAILED(g_factory->CreatePathGeometry(&geo))) {
        return NULL;
    }
    ID2D1GeometrySink *sink = NULL;
    if (FAILED(geo->Open(&sink))) {
        release_ptr(&geo);
        return NULL;
    }
    if (closed) {
        sink->BeginFigure(origin, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(pts[0]);
        sink->AddLines(pts, (UINT32)count);
        sink->AddLine(origin);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    } else {
        sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
        if (count > 1) {
            sink->AddLines(pts + 1, (UINT32)(count - 1));
        }
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }
    sink->Close();
    release_ptr(&sink);
    return geo;
}

static void
make_arc_points(
    const ThemeLayer *layer,
    float cx,
    float cy,
    float radius,
    float time,
    D2D1_POINT_2F *pts,
    int seg
)
{
    const float deg = 0.01745329252f;
    float start_a = layer->start_deg * deg;
    float end_a = layer->end_deg * deg;
    float phase = layer->phase;
    for (int i = 0; i <= seg; ++i) {
        float t = (float)i / (float)seg;
        float a = start_a + (end_a - start_a) * t;
        float envelope = 0.4f + 0.6f * (float)sin((double)(t * 3.141592653589793f));
        float w = 0.0f;
        if (layer->animate) {
            w = ((float)sin((double)(time * 1.3f + t * 4.2f + phase)) * layer->flow_amp
                + (float)cos((double)(time * 0.9f - t * 2.8f + phase * 1.5f)) * (layer->flow_amp * 0.45f))
                * envelope;
        }
        pts[i] = Point2F(
            cx + (float)cos((double)a) * (radius + w),
            cy + (float)sin((double)a) * (radius + w)
        );
    }
}

static void
fill_radial_ellipse(
    ID2D1DeviceContext *dc,
    float cx,
    float cy,
    float rx,
    float ry,
    uint32_t inner,
    uint32_t mid,
    uint32_t outer,
    float mid_stop
)
{
    D2D1_GRADIENT_STOP stops[3];
    stops[0].position = 0.0f;
    stops[0].color = color_rgb(inner, 1.0f);
    stops[1].position = mid_stop > 0.05f && mid_stop < 0.95f ? mid_stop : 0.42f;
    stops[1].color = color_rgb(mid, 1.0f);
    stops[2].position = 1.0f;
    stops[2].color = color_rgb(outer, 1.0f);

    ID2D1GradientStopCollection *stops_c = NULL;
    if (FAILED(dc->CreateGradientStopCollection(stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &stops_c))) {
        return;
    }

    ID2D1RadialGradientBrush *brush = NULL;
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES radial = RadialGradientBrushProperties(
        Point2F(cx, cy),
        Point2F(0.0f, 0.0f),
        rx,
        ry
    );
    if (SUCCEEDED(dc->CreateRadialGradientBrush(radial, stops_c, &brush))) {
        dc->FillEllipse(Ellipse(Point2F(cx, cy), rx, ry), brush);
    }
    release_ptr(&brush);
    release_ptr(&stops_c);
}

static void
fill_cap(
    ID2D1DeviceContext *dc,
    ID2D1PathGeometry *cap,
    float cx,
    float cy,
    float radius,
    uint32_t inner,
    uint32_t outer
)
{
    D2D1_GRADIENT_STOP stops[2];
    stops[0].position = 0.0f;
    stops[0].color = color_rgb(inner, 1.0f);
    stops[1].position = 1.0f;
    stops[1].color = color_rgb(outer, 1.0f);
    ID2D1GradientStopCollection *stops_c = NULL;
    if (FAILED(dc->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &stops_c))) {
        return;
    }
    ID2D1RadialGradientBrush *brush = NULL;
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES radial = RadialGradientBrushProperties(
        Point2F(cx, cy),
        Point2F(0.0f, 0.0f),
        radius,
        radius
    );
    if (SUCCEEDED(dc->CreateRadialGradientBrush(radial, stops_c, &brush))) {
        dc->FillGeometry(cap, brush);
    }
    release_ptr(&brush);
    release_ptr(&stops_c);
}

static void
draw_stroke_pair(
    ID2D1DeviceContext *dc,
    ID2D1PathGeometry *a,
    ID2D1PathGeometry *b,
    uint32_t rgb,
    float alpha,
    float width
)
{
    ID2D1SolidColorBrush *brush = NULL;
    if (FAILED(dc->CreateSolidColorBrush(color_rgb(rgb, alpha), &brush))) {
        return;
    }
    if (a) {
        dc->DrawGeometry(a, brush, width, g_stroke);
    }
    if (b) {
        dc->DrawGeometry(b, brush, width, g_stroke);
    }
    release_ptr(&brush);
}

static ID2D1Bitmap1 *
build_emblem(const ThemeLayer *layer)
{
    if (!layer->path[0]) {
        return NULL;
    }
    int slot = -1;
    for (int i = 0; i < THEME_MAX_EMBLEMS; ++i) {
        if (g_emblems[i].bitmap &&
            strcmp(g_emblems[i].path, layer->path) == 0 &&
            g_emblems[i].color == layer->color &&
            g_emblems[i].cache_size == layer->cache) {
            return g_emblems[i].bitmap;
        }
        if (slot < 0 && !g_emblems[i].bitmap) {
            slot = i;
        }
    }
    if (slot < 0) {
        slot = 0;
        release_ptr(&g_emblems[0].bitmap);
    }

    char *svg = read_file(layer->path);
    if (!svg) {
        return NULL;
    }
    char *d = strstr(svg, " d=\"");
    if (!d) {
        free(svg);
        return NULL;
    }
    d += 4;
    char *end = strchr(d, '"');
    if (!end) {
        free(svg);
        return NULL;
    }
    *end = '\0';

    ID2D1PathGeometry *src = NULL;
    if (FAILED(g_factory->CreatePathGeometry(&src))) {
        free(svg);
        return NULL;
    }
    ID2D1GeometrySink *sink = NULL;
    if (FAILED(src->Open(&sink))) {
        release_ptr(&src);
        free(svg);
        return NULL;
    }
    path_from_svg(d, sink);
    sink->Close();
    release_ptr(&sink);
    free(svg);

    int cache = layer->cache > 0 ? layer->cache : 512;
    if (cache > 512) {
        cache = 512;
    }
    D2D1_RECT_F bounds;
    float view = layer->view > 1.0f ? layer->view : 1028.0f;
    float scale = (float)cache / view;
    float ox = 0.0f;
    float oy = 0.0f;
    if (SUCCEEDED(src->GetBounds(Matrix3x2F::Identity(), &bounds))) {
        float bw = bounds.right - bounds.left;
        float bh = bounds.bottom - bounds.top;
        if (bw > 1.0f && bh > 1.0f) {
            float pad = (float)cache * 0.06f;
            float fit = (float)cache - pad * 2.0f;
            scale = bw > bh ? fit / bw : fit / bh;
            float cx = (bounds.left + bounds.right) * 0.5f;
            float cy = (bounds.top + bounds.bottom) * 0.5f;
            ox = (float)cache * 0.5f - cx * scale;
            oy = (float)cache * 0.5f - cy * scale;
        }
    }

    D2D1_MATRIX_3X2_F matrix = Matrix3x2F::Scale(scale, scale) * Matrix3x2F::Translation(ox, oy);
    ID2D1TransformedGeometry *scaled = NULL;
    if (FAILED(g_factory->CreateTransformedGeometry(src, matrix, &scaled))) {
        release_ptr(&src);
        return NULL;
    }
    release_ptr(&src);

    D2D1_BITMAP_PROPERTIES1 props = BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f
    );
    ID2D1Bitmap1 *bitmap = NULL;
    if (FAILED(g_dc->CreateBitmap(SizeU((UINT32)cache, (UINT32)cache), NULL, 0, props, &bitmap))) {
        release_ptr(&scaled);
        return NULL;
    }

    g_dc->SetTarget(bitmap);
    g_dc->BeginDraw();
    g_dc->Clear(ColorF(0, 0.0f));
    ID2D1SolidColorBrush *brush = NULL;
    if (SUCCEEDED(g_dc->CreateSolidColorBrush(color_rgb(layer->color, 1.0f), &brush))) {
        g_dc->FillGeometry(scaled, brush);
        release_ptr(&brush);
    }
    g_dc->EndDraw();
    g_dc->SetTarget(NULL);
    release_ptr(&scaled);

    g_emblems[slot].bitmap = bitmap;
    snprintf(g_emblems[slot].path, sizeof(g_emblems[slot].path), "%s", layer->path);
    g_emblems[slot].color = layer->color;
    g_emblems[slot].cache_size = layer->cache;
    return bitmap;
}

static void
draw_glow_stroke(ID2D1PathGeometry *stroke, uint32_t glow, float alpha, float width, float blur_outer, float blur_inner)
{
    if (!stroke || !g_glow) {
        return;
    }
    g_dc->SetTarget(g_glow);
    g_dc->Clear(ColorF(0, 0.0f));
    draw_stroke_pair(g_dc, stroke, NULL, glow, alpha, width);
    g_dc->SetTarget(g_target);
    if (g_blur_outer && blur_outer > 0.0f) {
        g_blur_outer->SetInput(0, g_glow);
        g_blur_outer->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, blur_outer);
        g_dc->DrawImage(g_blur_outer);
    }
    if (g_blur_inner && blur_inner > 0.0f) {
        g_blur_inner->SetInput(0, g_glow);
        g_blur_inner->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, blur_inner);
        g_dc->DrawImage(g_blur_inner);
    }
    draw_stroke_pair(g_dc, stroke, NULL, glow, alpha, width);
}

static void
draw_layer_radial(const ThemeLayer *layer, int width, int height)
{
    fill_radial_ellipse(
        g_dc,
        layer->x * (float)width,
        layer->y * (float)height,
        (float)width * layer->w * 0.5f,
        (float)height * layer->h * 0.5f,
        layer->inner,
        layer->mid,
        layer->outer,
        layer->mid_stop > 0.0f ? layer->mid_stop : 0.42f
    );
}

static void
draw_layer_vignette(const ThemeLayer *layer, int width, int height)
{
    D2D1_GRADIENT_STOP stops[2];
    stops[0].position = 0.0f;
    stops[0].color = color_rgb(layer->inner, 0.0f);
    stops[1].position = 1.0f;
    stops[1].color = color_rgb(layer->outer, layer->opacity);
    ID2D1GradientStopCollection *stops_c = NULL;
    if (FAILED(g_dc->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &stops_c))) {
        return;
    }
    float cx = layer->x * (float)width;
    float cy = layer->y * (float)height;
    float rx = (float)width * (layer->w > 0.1f ? layer->w : 1.2f) * 0.5f;
    float ry = (float)height * (layer->h > 0.1f ? layer->h : 1.2f) * 0.5f;
    ID2D1RadialGradientBrush *brush = NULL;
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES radial = RadialGradientBrushProperties(
        Point2F(cx, cy),
        Point2F(0.0f, 0.0f),
        rx,
        ry
    );
    if (SUCCEEDED(g_dc->CreateRadialGradientBrush(radial, stops_c, &brush))) {
        g_dc->FillRectangle(RectF(0.0f, 0.0f, (float)width, (float)height), brush);
    }
    release_ptr(&brush);
    release_ptr(&stops_c);
}

static void
draw_layer_arc(const ThemeLayer *layer, int width, int height, float time, float dpi)
{
    float cx = layer->x * (float)width;
    float cy = layer->y * (float)height;
    float scale = width < 500.0f * dpi ? layer->narrow_scale : layer->scale;
    float min_side = (float)(width < height ? width : height);
    float radius = min_side * layer->radius_ratio * scale;
    float px = dpi < 0.75f ? 1.0f : dpi;
    float anim = layer->animate ? time * layer->flow_speed : 0.0f;
    int seg = layer->segments < 8 ? 8 : layer->segments;
    if (seg > 64) {
        seg = 64;
    }

    D2D1_POINT_2F *pts = (D2D1_POINT_2F *)malloc(sizeof(D2D1_POINT_2F) * (size_t)(seg + 1));
    if (!pts) {
        return;
    }
    make_arc_points(layer, cx, cy, radius, anim, pts, seg);
    ID2D1PathGeometry *stroke = make_line_geo(pts, seg + 1, 0, Point2F(cx, cy));
    ID2D1PathGeometry *cap = layer->cap ? make_line_geo(pts, seg + 1, 1, Point2F(cx, cy)) : NULL;
    free(pts);

    if (cap) {
        fill_cap(g_dc, cap, cx, cy, radius * 1.15f, layer->cap_inner, layer->cap_outer);
    }
    draw_glow_stroke(
        stroke,
        layer->glow,
        layer->stroke_alpha * layer->opacity,
        layer->stroke_width * px,
        layer->blur_outer * px,
        layer->blur_inner * px
    );
    draw_stroke_pair(g_dc, stroke, NULL, layer->core, layer->opacity, layer->core_width * px);
    release_ptr(&stroke);
    release_ptr(&cap);
}

static float
design_px(const ThemeLayer *layer, int height, float dpi)
{
    float design = 620.0f;
    float size = layer->size * ((float)height / design);
    if (dpi > 1.0f && size < layer->size * dpi * 0.85f) {
        size = layer->size * dpi;
    }
    return size;
}

static float
layer_audio_drive(const ThemeLayer *layer, float time)
{
    float drive = 0.0f;
    if (layer->audio) {
        drive = g_audio_level;
        if (drive < 0.0f) {
            drive = 0.0f;
        }
        if (drive > 1.0f) {
            drive = 1.0f;
        }
    }
    if (layer->animate) {
        float speed = layer->flow_speed > 0.01f ? layer->flow_speed : 0.4f;
        float idle = 0.5f + 0.5f * sinf(time * speed * 6.2831853f + layer->phase);
        if (drive < idle * 0.22f) {
            drive = idle * 0.22f;
        }
    }
    return drive;
}

static void
draw_layer_ring(const ThemeLayer *layer, int width, int height, float dpi, float time)
{
    float cx = layer->x * (float)width;
    float cy = layer->y * (float)height;
    float px = dpi < 0.75f ? 1.0f : dpi;
    float drive = layer_audio_drive(layer, time);
    float amp = layer->scale;
    if (amp > 0.6f) {
        amp = 0.16f;
    }
    float pulse = 1.0f + amp * drive;
    float rx;
    float ry;
    if (layer->size > 1.0f) {
        float radius = design_px(layer, height, dpi) * 0.5f * pulse;
        rx = radius;
        ry = radius;
    } else {
        rx = (float)width * layer->w * 0.5f * pulse;
        ry = (float)height * layer->h * 0.5f * pulse;
    }
    float stroke = layer->core_width * px * (1.0f + drive * 0.45f);
    float alpha = layer->opacity * (0.72f + 0.28f * drive);
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }

    ID2D1EllipseGeometry *geo = NULL;
    if (FAILED(g_factory->CreateEllipseGeometry(Ellipse(Point2F(cx, cy), rx, ry), &geo))) {
        return;
    }
    ID2D1PathGeometry *path = NULL;
    if (SUCCEEDED(g_factory->CreatePathGeometry(&path))) {
        ID2D1GeometrySink *sink = NULL;
        if (SUCCEEDED(path->Open(&sink))) {
            geo->Simplify(D2D1_GEOMETRY_SIMPLIFICATION_OPTION_CUBICS_AND_LINES, Matrix3x2F::Identity(), 0.25f, sink);
            sink->Close();
            release_ptr(&sink);
            if (layer->blur_outer > 0.0f || layer->blur_inner > 0.0f) {
                draw_glow_stroke(
                    path,
                    layer->glow ? layer->glow : layer->color,
                    layer->stroke_alpha * alpha,
                    layer->stroke_width * px * (1.0f + drive * 0.35f),
                    layer->blur_outer * px,
                    layer->blur_inner * px
                );
            }
            draw_stroke_pair(
                g_dc,
                path,
                NULL,
                layer->core ? layer->core : layer->color,
                alpha,
                stroke
            );
        }
        release_ptr(&path);
    }
    release_ptr(&geo);
}

static void
draw_layer_emblem(const ThemeLayer *layer, int width, int height, float dpi)
{
    ID2D1Bitmap1 *bitmap = build_emblem(layer);
    if (!bitmap) {
        return;
    }
    float size = design_px(layer, height, dpi);
    float x = layer->x * (float)width - size * 0.5f;
    float y = layer->y * (float)height - size * 0.5f;
    g_dc->DrawBitmap(
        bitmap,
        RectF(x, y, x + size, y + size),
        layer->opacity,
        D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC
    );
}

static float
wrap01(float t)
{
    t -= floorf(t);
    if (t < 0.0f) {
        t += 1.0f;
    }
    return t;
}

static void
draw_layer_drift(const ThemeLayer *layer, int width, int height, float dpi, float time)
{
    ID2D1Bitmap1 *bitmap = build_emblem(layer);
    if (!bitmap) {
        return;
    }

    int count = layer->count > 0 ? layer->count : 12;
    if (count > 24) {
        count = 24;
    }
    float speed = layer->flow_speed > 0.01f ? layer->flow_speed : 0.07f;
    float design = 620.0f;
    float base = layer->size * ((float)height / design);
    if (dpi > 1.0f && base < layer->size * dpi * 0.35f) {
        base = layer->size * dpi * 0.55f;
    }
    if (base < 18.0f) {
        base = 18.0f;
    }

    for (int i = 0; i < count; ++i) {
        float seed = (float)i * 0.618033988f + layer->phase;
        float t = wrap01(time * speed + seed);
        float lane = wrap01(seed * 3.17f) * 2.0f - 1.0f;
        float nx = -0.2f + t * 1.4f + lane * 0.4f;
        float ny = -0.2f + t * 1.4f - lane * 0.22f;
        if (layer->animate) {
            nx += 0.03f * sinf(time * 0.45f + seed * 6.2f);
            ny += 0.025f * cosf(time * 0.38f + seed * 5.1f);
        }
        float sz = base;
        float alpha = layer->opacity;
        float x = nx * (float)width - sz * 0.5f;
        float y = ny * (float)height - sz * 0.5f;
        g_dc->DrawBitmap(
            bitmap,
            RectF(x, y, x + sz, y + sz),
            alpha,
            D2D1_INTERPOLATION_MODE_LINEAR
        );
    }
}

static void
prepare_emblems(const ThemeDesc *desc)
{
    for (int i = 0; i < desc->layer_count; ++i) {
        ThemeLayerKind kind = desc->layers[i].kind;
        if ((kind == THEME_LAYER_EMBLEM || kind == THEME_LAYER_DRIFT) && desc->layers[i].enabled) {
            build_emblem(&desc->layers[i]);
        }
    }
}

extern "C" void
theme_render_set_audio(float level)
{
    if (level < 0.0f) {
        level = 0.0f;
    }
    if (level > 1.0f) {
        level = 1.0f;
    }
    g_audio_level = level;
}

extern "C" float
theme_render_audio(void)
{
    return g_audio_level;
}

extern "C" void
theme_render_init(void)
{
    gpu_init();
}

extern "C" void
theme_render_shutdown(void)
{
    free_emblem();
    free_video_bmp();
    free_targets();
    release_ptr(&g_stroke);
    release_ptr(&g_dc);
    release_ptr(&g_device);
    release_ptr(&g_factory);
    release_ptr(&g_d3d_ctx);
    release_ptr(&g_d3d);
    g_started = 0;
}

extern "C" void
    theme_render_reset(void)
{
    free_emblem();
    free_video_bmp();
}

extern "C" void
theme_render_draw(Platform *platform, const ThemeDesc *desc, float time)
{
    if (!platform || !desc) {
        return;
    }
    if (!gpu_init() || !g_dc || !platform->hdc) {
        platform->clear(desc->clear);
        return;
    }

    int width = platform->width;
    int height = platform->height;
    if (!ensure_targets(width, height)) {
        platform->clear(desc->clear);
        return;
    }

    float dpi = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    prepare_emblems(desc);
    void *share = platform->video_shared_handle ? platform->video_shared_handle() : NULL;
    int vw = 0;
    int vh = 0;
    if (share && platform->video_frame_size && platform->video_frame_size(&vw, &vh)) {
        bind_video_share(share, vw, vh);
    }
    g_dc->SetTarget(g_target);
    g_dc->BeginDraw();
    g_dc->SetTransform(Matrix3x2F::Identity());
    ID2D1SolidColorBrush *bg = NULL;
    g_dc->CreateSolidColorBrush(color_rgb(desc->clear, 1.0f), &bg);
    if (bg) {
        g_dc->FillRectangle(RectF(0.0f, 0.0f, (float)width, (float)height), bg);
        release_ptr(&bg);
    }
    draw_video_bg(platform, desc, width, height);
    for (int i = 0; i < desc->layer_count; ++i) {
        const ThemeLayer *layer = &desc->layers[i];
        if (!layer->enabled) {
            continue;
        }
        switch (layer->kind) {
        case THEME_LAYER_RADIAL:
            draw_layer_radial(layer, width, height);
            break;
        case THEME_LAYER_VIGNETTE:
            draw_layer_vignette(layer, width, height);
            break;
        case THEME_LAYER_ARC:
            draw_layer_arc(layer, width, height, time, dpi);
            break;
        case THEME_LAYER_RING:
            draw_layer_ring(layer, width, height, dpi, time);
            break;
        case THEME_LAYER_EMBLEM:
            draw_layer_emblem(layer, width, height, dpi);
            break;
        case THEME_LAYER_DRIFT:
            draw_layer_drift(layer, width, height, dpi, time);
            break;
        default:
            break;
        }
    }
    HRESULT hr = g_dc->EndDraw();
    g_dc->SetTarget(NULL);
    if (hr == D2DERR_RECREATE_TARGET) {
        free_targets();
        free_emblem();
        free_video_bmp();
        platform->clear(desc->clear);
        return;
    }

    present_to_hdc((HDC)platform->hdc, width, height, platform->pixels);
}
