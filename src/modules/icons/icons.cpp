#include "shared/icons.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d2d1_3.h>
#include <d2d1svg.h>
#include <d3d11.h>
#include <dxgi.h>
#include <gdiplus.h>

using namespace Gdiplus;

static ULONG_PTR g_token;
static int g_started;
static int g_icon_alpha = 255;
static FontFamily *g_family;
static Font *g_font;
static float g_font_px;
static int g_font_weight;

static Font *
cached_font(float px, int weight)
{
    INT style = weight >= 600 ? FontStyleBold : FontStyleRegular;
    if (!g_family) {
        g_family = new FontFamily(L"Segoe UI");
    }
    if (g_font && g_font_px == px && g_font_weight == weight) {
        return g_font;
    }
    delete g_font;
    g_font = new Font(g_family, px, style, UnitPixel);
    g_font_px = px;
    g_font_weight = weight;
    return g_font;
}

void
icon_set_alpha(int alpha)
{
    if (alpha < 0) {
        alpha = 0;
    }
    if (alpha > 255) {
        alpha = 255;
    }
    g_icon_alpha = alpha;
}

static void
ensure_gdiplus(void)
{
    if (g_started) {
        return;
    }
    GdiplusStartupInput input;
    if (GdiplusStartup(&g_token, &input, NULL) == Ok) {
        g_started = 1;
    }
}

static Color
argb(uint32_t rgb, int alpha)
{
    if (alpha < 0) {
        alpha = 0;
    }
    if (alpha > 255) {
        alpha = 255;
    }
    return Color(
        (BYTE)alpha,
        (BYTE)((rgb >> 16) & 0xff),
        (BYTE)((rgb >> 8) & 0xff),
        (BYTE)(rgb & 0xff)
    );
}

static float
clamp_corner(float r, float w, float h)
{
    if (r < 0.0f) {
        r = 0.0f;
    }
    if (r * 2.0f > w) {
        r = w * 0.5f;
    }
    if (r * 2.0f > h) {
        r = h * 0.5f;
    }
    return r;
}

static void
add_rounded_rect_corners(GraphicsPath *path, float x, float y, float w, float h, float tl, float tr, float br, float bl)
{
    tl = clamp_corner(tl, w, h);
    tr = clamp_corner(tr, w, h);
    br = clamp_corner(br, w, h);
    bl = clamp_corner(bl, w, h);

    if (tl < 0.5f && tr < 0.5f && br < 0.5f && bl < 0.5f) {
        path->AddRectangle(RectF(x, y, w, h));
        return;
    }

    if (tl >= 0.5f) {
        path->AddArc(x, y, tl * 2.0f, tl * 2.0f, 180.0f, 90.0f);
    } else {
        path->AddLine(PointF(x, y + 0.01f), PointF(x, y));
    }
    if (tr >= 0.5f) {
        path->AddArc(x + w - tr * 2.0f, y, tr * 2.0f, tr * 2.0f, 270.0f, 90.0f);
    } else {
        path->AddLine(PointF(x + w, y), PointF(x + w, y));
    }
    if (br >= 0.5f) {
        path->AddArc(x + w - br * 2.0f, y + h - br * 2.0f, br * 2.0f, br * 2.0f, 0.0f, 90.0f);
    } else {
        path->AddLine(PointF(x + w, y + h), PointF(x + w, y + h));
    }
    if (bl >= 0.5f) {
        path->AddArc(x, y + h - bl * 2.0f, bl * 2.0f, bl * 2.0f, 90.0f, 90.0f);
    } else {
        path->AddLine(PointF(x, y + h), PointF(x, y + h));
    }
    path->CloseFigure();
}

static void
add_rounded_rect(GraphicsPath *path, float x, float y, float w, float h, float r)
{
    add_rounded_rect_corners(path, x, y, w, h, r, r, r, r);
}

static void
stroke_path(Graphics *g, GraphicsPath *path, uint32_t rgb, float stroke)
{
    Pen pen(argb(rgb, g_icon_alpha), stroke);
    pen.SetLineJoin(LineJoinRound);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g->DrawPath(&pen, path);
}

static void
build_icon(GraphicsPath *path, IconId id)
{
    switch (id) {
    case ICON_X:
        path->StartFigure();
        path->AddLine(PointF(18.0f, 6.0f), PointF(6.0f, 18.0f));
        path->StartFigure();
        path->AddLine(PointF(6.0f, 6.0f), PointF(18.0f, 18.0f));
        break;
    case ICON_MINUS:
        path->AddLine(PointF(5.0f, 12.0f), PointF(19.0f, 12.0f));
        break;
    case ICON_SQUARE:
        add_rounded_rect(path, 6.0f, 6.0f, 12.0f, 12.0f, 1.6f);
        break;
    case ICON_GLOBE:
        path->AddEllipse(4.0f, 4.0f, 16.0f, 16.0f);
        path->StartFigure();
        path->AddEllipse(9.0f, 4.0f, 6.0f, 16.0f);
        path->StartFigure();
        path->AddLine(PointF(4.0f, 12.0f), PointF(20.0f, 12.0f));
        break;
    case ICON_SEARCH:
        path->AddEllipse(5.0f, 5.0f, 10.5f, 10.5f);
        path->StartFigure();
        path->AddLine(PointF(14.8f, 14.8f), PointF(19.5f, 19.5f));
        break;
    case ICON_SETTINGS:
        path->AddEllipse(9.2f, 9.2f, 5.6f, 5.6f);
        path->StartFigure();
        path->AddEllipse(4.4f, 4.4f, 15.2f, 15.2f);
        break;
    case ICON_CHEVRON_DOWN:
        path->AddLine(PointF(6.0f, 9.0f), PointF(12.0f, 15.0f));
        path->AddLine(PointF(12.0f, 15.0f), PointF(18.0f, 9.0f));
        break;
    case ICON_CHEVRON_UP:
        path->AddLine(PointF(6.0f, 15.0f), PointF(12.0f, 9.0f));
        path->AddLine(PointF(12.0f, 9.0f), PointF(18.0f, 15.0f));
        break;
    case ICON_MENU:
        path->StartFigure();
        path->AddLine(PointF(4.5f, 8.0f), PointF(19.5f, 8.0f));
        path->StartFigure();
        path->AddLine(PointF(4.5f, 12.0f), PointF(19.5f, 12.0f));
        path->StartFigure();
        path->AddLine(PointF(4.5f, 16.0f), PointF(19.5f, 16.0f));
        break;
    case ICON_PLAY:
        path->AddLine(PointF(7.0f, 4.5f), PointF(19.5f, 12.0f));
        path->AddLine(PointF(19.5f, 12.0f), PointF(7.0f, 19.5f));
        path->CloseFigure();
        break;
    case ICON_PAUSE:
        add_rounded_rect(path, 6.0f, 4.5f, 4.0f, 15.0f, 1.1f);
        path->StartFigure();
        add_rounded_rect(path, 14.0f, 4.5f, 4.0f, 15.0f, 1.1f);
        break;
    case ICON_USER:
        path->AddEllipse(8.0f, 4.0f, 8.0f, 8.0f);
        path->StartFigure();
        path->AddArc(4.0f, 13.0f, 16.0f, 16.0f, 0.0f, -180.0f);
        break;
    case ICON_DOWNLOAD:
        path->StartFigure();
        path->AddLine(PointF(12.0f, 3.0f), PointF(12.0f, 15.0f));
        path->StartFigure();
        path->AddLine(PointF(7.0f, 10.0f), PointF(12.0f, 15.0f));
        path->AddLine(PointF(12.0f, 15.0f), PointF(17.0f, 10.0f));
        path->StartFigure();
        path->AddLine(PointF(4.0f, 15.0f), PointF(4.0f, 19.0f));
        path->AddLine(PointF(4.0f, 19.0f), PointF(20.0f, 19.0f));
        path->AddLine(PointF(20.0f, 19.0f), PointF(20.0f, 15.0f));
        break;
    case ICON_LOG_IN:
        path->AddLine(PointF(10.0f, 7.0f), PointF(15.0f, 12.0f));
        path->AddLine(PointF(15.0f, 12.0f), PointF(10.0f, 17.0f));
        path->StartFigure();
        path->AddLine(PointF(3.0f, 12.0f), PointF(15.0f, 12.0f));
        path->StartFigure();
        path->AddLine(PointF(15.0f, 3.0f), PointF(19.0f, 3.0f));
        path->AddArc(17.0f, 3.0f, 4.0f, 4.0f, 270.0f, 90.0f);
        path->AddLine(PointF(21.0f, 5.0f), PointF(21.0f, 19.0f));
        path->AddArc(17.0f, 17.0f, 4.0f, 4.0f, 0.0f, 90.0f);
        path->AddLine(PointF(19.0f, 21.0f), PointF(15.0f, 21.0f));
        break;
    default:
        break;
    }
}

static char g_root[512];
static Bitmap *g_steam_mask;
static Bitmap *g_lucide_src[ICON_COUNT];
#define LUCIDE_FIT_MAX 2
static Bitmap *g_lucide_fit[ICON_COUNT][LUCIDE_FIT_MAX];
static int g_lucide_fit_dim[ICON_COUNT][LUCIDE_FIT_MAX];

void
icon_set_root(const char *project_root)
{
    snprintf(g_root, sizeof(g_root), "%s", project_root ? project_root : ".");
    if (g_steam_mask) {
        delete g_steam_mask;
        g_steam_mask = NULL;
    }
    for (int i = 0; i < ICON_COUNT; ++i) {
        delete g_lucide_src[i];
        g_lucide_src[i] = NULL;
        for (int s = 0; s < LUCIDE_FIT_MAX; ++s) {
            delete g_lucide_fit[i][s];
            g_lucide_fit[i][s] = NULL;
            g_lucide_fit_dim[i][s] = 0;
        }
    }
}

template <typename T>
static void
release_com(T **ptr)
{
    if (ptr && *ptr) {
        (*ptr)->Release();
        *ptr = NULL;
    }
}

static int
file_exists(const char *path)
{
    return path && path[0] && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static int
asset_svg_path(char *out, int max, const char *rel)
{
    if (g_root[0]) {
        snprintf(out, max, "%s/%s", g_root, rel);
        if (file_exists(out)) {
            return 1;
        }
    }
    snprintf(out, max, "%s", rel);
    return file_exists(out);
}

static int
steam_svg_path(char *out, int max)
{
    return asset_svg_path(out, max, "assets/steam.svg");
}

static char *
read_all(const char *path, size_t *out_len)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0 || size > 200000) {
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
    if (out_len) {
        *out_len = n;
    }
    return buf;
}

static Bitmap *
copy_mapped_bitmap(int dim, const D2D1_MAPPED_RECT *map)
{
    Bitmap *out = new Bitmap(dim, dim, PixelFormat32bppPARGB);
    BitmapData data;
    Rect rect(0, 0, dim, dim);
    if (out->LockBits(&rect, ImageLockModeWrite, PixelFormat32bppPARGB, &data) != Ok) {
        delete out;
        return NULL;
    }
    for (int y = 0; y < dim; ++y) {
        memcpy(
            (unsigned char *)data.Scan0 + y * data.Stride,
            map->bits + y * map->pitch,
            (size_t)dim * 4
        );
    }
    out->UnlockBits(&data);
    return out;
}

static Bitmap *
rasterize_steam_svg(const void *svg, size_t svg_len, int dim)
{
    ID2D1Factory1 *factory = NULL;
    ID3D11Device *d3d = NULL;
    IDXGIDevice *dxgi = NULL;
    ID2D1Device *device = NULL;
    ID2D1DeviceContext *dc = NULL;
    ID2D1DeviceContext5 *dc5 = NULL;
    ID2D1Bitmap1 *target = NULL;
    ID2D1Bitmap1 *cpu = NULL;
    IStream *stream = NULL;
    ID2D1SvgDocument *doc = NULL;
    Bitmap *out = NULL;
    HGLOBAL mem = NULL;
    void *locked = NULL;
    D2D1_FACTORY_OPTIONS opts = {};
    D2D1_BITMAP_PROPERTIES1 target_props = {};
    D2D1_BITMAP_PROPERTIES1 cpu_props = {};
    D2D1_SIZE_U pixels = {};
    D2D1_MAPPED_RECT map = {};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr;

    if (FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory1),
            &opts,
            (void **)&factory))) {
        return NULL;
    }

    hr = D3D11CreateDevice(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        flags,
        NULL,
        0,
        D3D11_SDK_VERSION,
        &d3d,
        NULL,
        NULL
    );
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            NULL,
            D3D_DRIVER_TYPE_WARP,
            NULL,
            flags,
            NULL,
            0,
            D3D11_SDK_VERSION,
            &d3d,
            NULL,
            NULL
        );
    }
    if (FAILED(hr) ||
        FAILED(d3d->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi)) ||
        FAILED(factory->CreateDevice(dxgi, &device)) ||
        FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc)) ||
        FAILED(dc->QueryInterface(__uuidof(ID2D1DeviceContext5), (void **)&dc5))) {
        goto done;
    }

    target_props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    cpu_props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    pixels = D2D1::SizeU((UINT32)dim, (UINT32)dim);
    if (FAILED(dc5->CreateBitmap(pixels, NULL, 0, target_props, &target)) ||
        FAILED(dc5->CreateBitmap(pixels, NULL, 0, cpu_props, &cpu))) {
        goto done;
    }

    mem = GlobalAlloc(GMEM_MOVEABLE, svg_len);
    if (!mem) {
        goto done;
    }
    locked = GlobalLock(mem);
    if (!locked) {
        GlobalFree(mem);
        mem = NULL;
        goto done;
    }
    memcpy(locked, svg, svg_len);
    GlobalUnlock(mem);
    locked = NULL;
    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &stream))) {
        goto done;
    }
    mem = NULL;
    if (FAILED(dc5->CreateSvgDocument(stream, D2D1::SizeF((float)dim, (float)dim), &doc))) {
        goto done;
    }

    dc5->SetTarget(target);
    dc5->BeginDraw();
    dc5->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    dc5->DrawSvgDocument(doc);
    dc5->EndDraw();
    dc5->SetTarget(NULL);
    if (FAILED(cpu->CopyFromBitmap(NULL, target, NULL)) ||
        FAILED(cpu->Map(D2D1_MAP_OPTIONS_READ, &map))) {
        goto done;
    }
    out = copy_mapped_bitmap(dim, &map);
    cpu->Unmap();

done:
    release_com(&doc);
    release_com(&stream);
    release_com(&cpu);
    release_com(&target);
    release_com(&dc5);
    release_com(&dc);
    release_com(&device);
    release_com(&dxgi);
    release_com(&d3d);
    release_com(&factory);
    return out;
}

static Bitmap *
steam_mask(void)
{
    if (g_steam_mask) {
        return g_steam_mask;
    }
    ensure_gdiplus();
    char path[MAX_PATH];
    if (!steam_svg_path(path, (int)sizeof(path))) {
        return NULL;
    }
    size_t len = 0;
    char *svg = read_all(path, &len);
    if (!svg) {
        return NULL;
    }
    g_steam_mask = rasterize_steam_svg(svg, len, 512);
    free(svg);
    return g_steam_mask;
}

static Bitmap *
mask_fit_box(Bitmap *src, int dim)
{
    int sw = (int)src->GetWidth();
    int sh = (int)src->GetHeight();
    BitmapData src_data;
    Rect src_rect(0, 0, sw, sh);
    if (src->LockBits(&src_rect, ImageLockModeRead, PixelFormat32bppPARGB, &src_data) != Ok) {
        return NULL;
    }
    Bitmap *out = new Bitmap(dim, dim, PixelFormat32bppPARGB);
    BitmapData dst_data;
    Rect dst_rect(0, 0, dim, dim);
    if (out->LockBits(&dst_rect, ImageLockModeWrite, PixelFormat32bppPARGB, &dst_data) != Ok) {
        src->UnlockBits(&src_data);
        delete out;
        return NULL;
    }
    for (int y = 0; y < dim; ++y) {
        unsigned char *dst = (unsigned char *)dst_data.Scan0 + y * dst_data.Stride;
        int sy0 = y * sh / dim;
        int sy1 = (y + 1) * sh / dim;
        if (sy1 <= sy0) {
            sy1 = sy0 + 1;
        }
        if (sy1 > sh) {
            sy1 = sh;
        }
        for (int x = 0; x < dim; ++x) {
            int sx0 = x * sw / dim;
            int sx1 = (x + 1) * sw / dim;
            if (sx1 <= sx0) {
                sx1 = sx0 + 1;
            }
            if (sx1 > sw) {
                sx1 = sw;
            }
            unsigned int r = 0;
            unsigned int g = 0;
            unsigned int b = 0;
            unsigned int a = 0;
            unsigned int n = 0;
            for (int sy = sy0; sy < sy1; ++sy) {
                const unsigned char *row = (const unsigned char *)src_data.Scan0 + sy * src_data.Stride;
                for (int sx = sx0; sx < sx1; ++sx) {
                    const unsigned char *p = row + sx * 4;
                    b += p[0];
                    g += p[1];
                    r += p[2];
                    a += p[3];
                    n += 1;
                }
            }
            if (n == 0) {
                n = 1;
            }
            dst[x * 4 + 0] = (unsigned char)(b / n);
            dst[x * 4 + 1] = (unsigned char)(g / n);
            dst[x * 4 + 2] = (unsigned char)(r / n);
            dst[x * 4 + 3] = (unsigned char)(a / n);
        }
    }
    out->UnlockBits(&dst_data);
    src->UnlockBits(&src_data);
    return out;
}

static Bitmap *
rasterize_lucide_gdi(IconId id, int dim)
{
    Bitmap *bmp = new Bitmap(dim, dim, PixelFormat32bppPARGB);
    if (!bmp || bmp->GetLastStatus() != Ok) {
        delete bmp;
        return NULL;
    }
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetCompositingQuality(CompositingQualityHighQuality);
    g.Clear(Color(0, 0, 0, 0));

    GraphicsPath path;
    build_icon(&path, id);
    Matrix matrix;
    float scale = (float)dim / 24.0f;
    matrix.Scale(scale, scale);
    path.Transform(&matrix);

    Pen pen(Color(255, 255, 255, 255), 2.0f * scale);
    pen.SetLineJoin(LineJoinRound);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawPath(&pen, &path);
    return bmp;
}

static Bitmap *
lucide_src(IconId id)
{
    if (id < 0 || id >= ICON_COUNT) {
        return NULL;
    }
    if (g_lucide_src[id]) {
        return g_lucide_src[id];
    }
    g_lucide_src[id] = rasterize_lucide_gdi(id, 256);
    return g_lucide_src[id];
}

static Bitmap *
lucide_fit(IconId id, int dim)
{
    if (dim < 8) {
        return NULL;
    }
    for (int i = 0; i < LUCIDE_FIT_MAX; ++i) {
        if (g_lucide_fit[id][i] && g_lucide_fit_dim[id][i] == dim) {
            return g_lucide_fit[id][i];
        }
    }
    Bitmap *src = lucide_src(id);
    if (!src) {
        return NULL;
    }
    Bitmap *made = mask_fit_box(src, dim);
    if (!made) {
        return NULL;
    }
    int slot = 0;
    for (int i = 0; i < LUCIDE_FIT_MAX; ++i) {
        if (!g_lucide_fit[id][i]) {
            slot = i;
            break;
        }
    }
    delete g_lucide_fit[id][slot];
    g_lucide_fit[id][slot] = made;
    g_lucide_fit_dim[id][slot] = dim;
    return made;
}

static int
draw_lucide(Graphics *g, IconId id, float cx, float cy, float size, uint32_t rgb)
{
    int dim = (int)floorf(size + 0.5f);
    if (dim < 16) {
        dim = 16;
    }
    Bitmap *mask = lucide_fit(id, dim);
    if (!mask) {
        return 0;
    }
    float left = floorf(cx - (float)dim * 0.5f + 0.5f);
    float top = floorf(cy - (float)dim * 0.5f + 0.5f);
    float cr = (float)((rgb >> 16) & 0xff) / 255.0f;
    float cg = (float)((rgb >> 8) & 0xff) / 255.0f;
    float cb = (float)(rgb & 0xff) / 255.0f;
    ColorMatrix cm;
    memset(&cm, 0, sizeof(cm));
    cm.m[0][0] = cr;
    cm.m[1][1] = cg;
    cm.m[2][2] = cb;
    cm.m[3][3] = (float)g_icon_alpha / 255.0f;
    cm.m[4][4] = 1.0f;
    ImageAttributes attr;
    attr.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
    g->SetInterpolationMode(InterpolationModeNearestNeighbor);
    g->SetPixelOffsetMode(PixelOffsetModeHalf);
    g->DrawImage(
        mask,
        RectF(left, top, (float)dim, (float)dim),
        0.0f,
        0.0f,
        (REAL)dim,
        (REAL)dim,
        UnitPixel,
        &attr
    );
    return 1;
}

static void
draw_steam(Graphics *g, float cx, float cy, float size, uint32_t rgb)
{
    Bitmap *mask = steam_mask();
    if (!mask) {
        return;
    }
    const int dim = (int)mask->GetWidth();
    float full = size;
    RectF dest(cx - full * 0.5f, cy - full * 0.5f, full, full);

    float cr = (float)((rgb >> 16) & 0xff) / 255.0f;
    float cg = (float)((rgb >> 8) & 0xff) / 255.0f;
    float cb = (float)(rgb & 0xff) / 255.0f;
    ColorMatrix cm;
    memset(&cm, 0, sizeof(cm));
    cm.m[0][0] = cr;
    cm.m[1][1] = cg;
    cm.m[2][2] = cb;
    cm.m[3][3] = (float)g_icon_alpha / 255.0f;
    cm.m[4][4] = 1.0f;
    ImageAttributes attr;
    attr.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);

    g->SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g->SetPixelOffsetMode(PixelOffsetModeHalf);
    g->DrawImage(
        mask,
        dest,
        0.0f,
        0.0f,
        (REAL)dim,
        (REAL)dim,
        UnitPixel,
        &attr
    );
}

void
icon_draw(void *hdc, IconId id, float cx, float cy, float size, uint32_t rgb, float stroke)
{
    if (!hdc || size < 1.0f || g_icon_alpha <= 0) {
        return;
    }
    ensure_gdiplus();
    Graphics g((HDC)hdc);
    g.SetPageUnit(UnitPixel);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetCompositingQuality(CompositingQualityHighQuality);
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);

    if (id == ICON_STEAM) {
        draw_steam(&g, cx, cy, size, rgb);
        return;
    }
    if ((id == ICON_DOWNLOAD || id == ICON_PLAY) && draw_lucide(&g, id, cx, cy, size, rgb)) {
        return;
    }

    GraphicsPath path;
    build_icon(&path, id);

    float dest = floorf(size + 0.5f);
    if (dest < 16.0f) {
        dest = size;
    }
    float scale = dest / 24.0f;
    float left = floorf(cx - dest * 0.5f + 0.5f);
    float top = floorf(cy - dest * 0.5f + 0.5f);

    Matrix matrix;
    if (id == ICON_USER || id == ICON_LOG_IN || id == ICON_CHEVRON_UP || id == ICON_CHEVRON_DOWN) {
        RectF bounds;
        path.GetBounds(&bounds);
        if (bounds.Width > 0.1f && bounds.Height > 0.1f) {
            matrix.Translate(cx, cy);
            matrix.Scale(scale, scale);
            matrix.Translate(
                -(bounds.X + bounds.Width * 0.5f),
                -(bounds.Y + bounds.Height * 0.5f)
            );
        } else {
            matrix.Translate(left, top);
            matrix.Scale(scale, scale);
        }
    } else {
        matrix.Translate(left, top);
        matrix.Scale(scale, scale);
    }
    path.Transform(&matrix);

    float sw = 1.75f * scale;
    if (stroke > 0.1f && stroke < 4.0f && stroke > sw * 1.35f) {
        sw = stroke;
    }
    if (sw < 1.25f) {
        sw = 1.25f;
    }
    if (id == ICON_PLAY || id == ICON_PAUSE) {
        SolidBrush brush(argb(rgb, g_icon_alpha));
        g.FillPath(&brush, &path);
    } else {
        stroke_path(&g, &path, rgb, sw);
    }
}

void
icon_fill_rect(void *hdc, float x, float y, float w, float h, uint32_t rgb, int alpha)
{
    if (!hdc || w < 1.0f || h < 1.0f || alpha <= 0) {
        return;
    }
    if (alpha > 255) {
        alpha = 255;
    }

    static HDC tint_dc = NULL;
    static HBITMAP tint_bmp = NULL;
    static uint32_t *tint_bits = NULL;
    if (!tint_dc) {
        BITMAPINFO info;
        memset(&info, 0, sizeof(info));
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 1;
        info.bmiHeader.biHeight = -1;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        tint_dc = CreateCompatibleDC((HDC)hdc);
        tint_bmp = CreateDIBSection(tint_dc, &info, DIB_RGB_COLORS, (void **)&tint_bits, NULL, 0);
        if (tint_dc && tint_bmp) {
            SelectObject(tint_dc, tint_bmp);
        }
    }
    if (tint_dc && tint_bits) {
        uint32_t r = (rgb >> 16) & 0xffu;
        uint32_t g = (rgb >> 8) & 0xffu;
        uint32_t b = rgb & 0xffu;
        tint_bits[0] = 0xff000000u | (b << 16) | (g << 8) | r;
        BLENDFUNCTION blend;
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = (BYTE)alpha;
        blend.AlphaFormat = 0;
        if (AlphaBlend(
                (HDC)hdc,
                (int)x,
                (int)y,
                (int)(w + 0.5f),
                (int)(h + 0.5f),
                tint_dc,
                0,
                0,
                1,
                1,
                blend
            )) {
            return;
        }
    }

    ensure_gdiplus();
    Graphics graphics((HDC)hdc);
    graphics.SetSmoothingMode(SmoothingModeNone);
    graphics.SetCompositingQuality(CompositingQualityHighSpeed);
    graphics.SetPixelOffsetMode(PixelOffsetModeNone);
    SolidBrush brush(argb(rgb, alpha));
    graphics.FillRectangle(&brush, (INT)x, (INT)y, (INT)(w + 0.5f), (INT)(h + 0.5f));
}

void
icon_round_rect(void *hdc, float x, float y, float w, float h, float radius, uint32_t rgb, int alpha)
{
    if (radius <= 0.5f) {
        icon_fill_rect(hdc, x, y, w, h, rgb, alpha);
        return;
    }
    icon_round_rect_corners(hdc, x, y, w, h, radius, radius, radius, radius, rgb, alpha);
}

void
icon_round_rect_corners(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    float tl,
    float tr,
    float br,
    float bl,
    uint32_t rgb,
    int alpha
)
{
    if (!hdc || w < 1.0f || h < 1.0f || alpha <= 0) {
        return;
    }
    ensure_gdiplus();
    Graphics g((HDC)hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    GraphicsPath path;
    add_rounded_rect_corners(&path, x, y, w, h, tl, tr, br, bl);
    SolidBrush brush(argb(rgb, alpha));
    g.FillPath(&brush, &path);
}

void
icon_round_stroke(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    float radius,
    uint32_t rgb,
    int alpha,
    float stroke
)
{
    if (!hdc || w < 1.0f || h < 1.0f || alpha <= 0) {
        return;
    }
    ensure_gdiplus();
    Graphics g((HDC)hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    GraphicsPath path;
    add_rounded_rect(&path, x + stroke * 0.5f, y + stroke * 0.5f, w - stroke, h - stroke, radius);
    Pen pen(argb(rgb, alpha), stroke);
    pen.SetLineJoin(LineJoinRound);
    g.DrawPath(&pen, &path);
}

static void
draw_label(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    float px,
    int weight,
    StringAlignment align,
    int alpha,
    int ellipsize
)
{
    if (!hdc || !text || w < 1.0f || h < 1.0f || px < 1.0f || alpha <= 0) {
        return;
    }
    ensure_gdiplus();
    Graphics g((HDC)hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetTextRenderingHint(alpha >= 250 ? TextRenderingHintClearTypeGridFit : TextRenderingHintAntiAlias);
    g.SetTextContrast(1200);

    Font *font = cached_font(px, weight);
    if (!font) {
        return;
    }
    SolidBrush brush(argb(rgb, alpha));
    StringFormat fmt(StringFormat::GenericTypographic());
    fmt.SetAlignment(align);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip | StringFormatFlagsMeasureTrailingSpaces);
    fmt.SetTrimming(ellipsize ? StringTrimmingEllipsisCharacter : StringTrimmingNone);
    g.DrawString(text, -1, font, RectF(x, y, w, h), &fmt, &brush);
}

void
icon_draw_label(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    float px,
    int weight
)
{
    draw_label(hdc, x, y, w, h, text, rgb, px, weight, StringAlignmentNear, 255, 1);
}

void
icon_draw_label_full(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    float px,
    int weight
)
{
    draw_label(hdc, x, y, w, h, text, rgb, px, weight, StringAlignmentNear, 255, 0);
}

void
icon_draw_label_alpha(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    float px,
    int weight,
    int alpha
)
{
    draw_label(hdc, x, y, w, h, text, rgb, px, weight, StringAlignmentNear, alpha, 1);
}

void
icon_draw_label_end_alpha(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    float px,
    int weight,
    int alpha
)
{
    draw_label(hdc, x, y, w, h, text, rgb, px, weight, StringAlignmentFar, alpha, 0);
}

float
icon_measure_label(void *hdc, const wchar_t *text, float px, int weight)
{
    if (!text || !text[0] || px < 1.0f) {
        return 0.0f;
    }
    ensure_gdiplus();

    HDC raw = hdc ? (HDC)hdc : GetDC(NULL);
    if (!raw) {
        return 0.0f;
    }

    Graphics g(raw);
    g.SetPageUnit(UnitPixel);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    Font *font = cached_font(px, weight);
    if (!font) {
        if (!hdc) {
            ReleaseDC(NULL, raw);
        }
        return 0.0f;
    }
    StringFormat fmt(StringFormat::GenericTypographic());
    fmt.SetAlignment(StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentNear);
    fmt.SetFormatFlags(StringFormatFlagsMeasureTrailingSpaces | StringFormatFlagsNoWrap | StringFormatFlagsNoClip);
    fmt.SetTrimming(StringTrimmingNone);

    int len = (int)wcslen(text);
    CharacterRange range(0, len);
    fmt.SetMeasurableCharacterRanges(1, &range);

    RectF layout(0.0f, 0.0f, 8192.0f, px * 6.0f);
    Region region;
    RectF bounds;
    float width = 0.0f;
    if (g.MeasureCharacterRanges(text, len, font, layout, &fmt, 1, &region) == Ok) {
        region.GetBounds(&bounds, &g);
        width = bounds.GetRight();
    } else {
        g.MeasureString(text, len, font, layout, &fmt, &bounds);
        width = bounds.Width;
    }

    if (!hdc) {
        ReleaseDC(NULL, raw);
    }
    if (width < 0.0f) {
        return 0.0f;
    }
    return width;
}

void
icon_draw_label_shimmer(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    uint32_t shine,
    float px,
    int weight,
    int alpha,
    float phase
)
{
    if (!hdc || !text || w < 8.0f || h < 4.0f || alpha <= 0 || px < 1.0f) {
        return;
    }
    if (phase < 0.0f) {
        phase = 0.0f;
    }
    if (phase > 1.0f) {
        phase -= floorf(phase);
    }

    x = floorf(x);
    y = floorf(y);
    w = floorf(w);
    h = floorf(h);
    px = floorf(px + 0.5f);

    ensure_gdiplus();
    Graphics g((HDC)hdc);
    g.SetSmoothingMode(SmoothingModeNone);
    g.SetPixelOffsetMode(PixelOffsetModeNone);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetTextContrast(2200);

    Font *font = cached_font(px, weight);
    if (!font) {
        return;
    }
    StringFormat fmt(StringFormat::GenericTypographic());
    fmt.SetAlignment(StringAlignmentFar);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip | StringFormatFlagsMeasureTrailingSpaces);
    fmt.SetTrimming(StringTrimmingNone);

    RectF box(x, y, w, h);
    SolidBrush base(argb(rgb, alpha >= 255 ? 255 : alpha));
    g.DrawString(text, -1, font, box, &fmt, &base);

    float band = 56.0f;
    if (band > w * 0.34f) {
        band = w * 0.34f;
    }
    if (band < 28.0f) {
        band = 28.0f;
    }
    float travel = w + band;
    float cx = floorf(x - band + travel * phase);
    g.SetClip(RectF(cx, y, band, h), CombineModeReplace);
    SolidBrush hi(argb(shine, 255));
    g.DrawString(text, -1, font, box, &fmt, &hi);
    g.ResetClip();
}

void
icon_draw_label_center(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    float px,
    int weight
)
{
    draw_label(hdc, x, y, w, h, text, rgb, px, weight, StringAlignmentCenter, 255, 1);
}

void
icon_draw_label_center_alpha(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    float px,
    int weight,
    int alpha
)
{
    draw_label(hdc, x, y, w, h, text, rgb, px, weight, StringAlignmentCenter, alpha, 1);
}

static Bitmap *g_avatar_src;
static char g_avatar_loaded[MAX_PATH];

#define AVATAR_FIT_MAX 2
static Bitmap *g_avatar_fit[AVATAR_FIT_MAX];
static int g_avatar_fit_dim[AVATAR_FIT_MAX];

static void
avatar_clear_fits(void)
{
    for (int i = 0; i < AVATAR_FIT_MAX; ++i) {
        delete g_avatar_fit[i];
        g_avatar_fit[i] = NULL;
        g_avatar_fit_dim[i] = 0;
    }
}

static Bitmap *
avatar_load_src(const char *path)
{
    wchar_t wide[MAX_PATH];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, MAX_PATH) <= 0) {
        return NULL;
    }
    Bitmap *file = Bitmap::FromFile(wide, FALSE);
    if (!file || file->GetLastStatus() != Ok) {
        delete file;
        return NULL;
    }
    int w = (int)file->GetWidth();
    int h = (int)file->GetHeight();
    if (w < 8 || h < 8) {
        delete file;
        return NULL;
    }
    Bitmap *src = new Bitmap(w, h, PixelFormat32bppARGB);
    Graphics copy(src);
    copy.SetCompositingMode(CompositingModeSourceCopy);
    copy.SetInterpolationMode(InterpolationModeNearestNeighbor);
    copy.SetPixelOffsetMode(PixelOffsetModeHalf);
    copy.DrawImage(file, 0, 0, w, h);
    delete file;
    return src;
}

static Bitmap *
avatar_make_fit(Bitmap *src, int dim)
{
    int sw = (int)src->GetWidth();
    int sh = (int)src->GetHeight();
    BitmapData src_data;
    Rect src_rect(0, 0, sw, sh);
    if (src->LockBits(&src_rect, ImageLockModeRead, PixelFormat32bppARGB, &src_data) != Ok) {
        return NULL;
    }

    Bitmap *out = new Bitmap(dim, dim, PixelFormat32bppPARGB);
    BitmapData dst_data;
    Rect dst_rect(0, 0, dim, dim);
    if (out->LockBits(&dst_rect, ImageLockModeWrite, PixelFormat32bppPARGB, &dst_data) != Ok) {
        src->UnlockBits(&src_data);
        delete out;
        return NULL;
    }

    float mid = dim * 0.5f;
    float radius = mid - 0.35f;
    for (int y = 0; y < dim; ++y) {
        unsigned char *dst = (unsigned char *)dst_data.Scan0 + y * dst_data.Stride;
        int sy0 = y * sh / dim;
        int sy1 = (y + 1) * sh / dim;
        if (sy1 <= sy0) {
            sy1 = sy0 + 1;
        }
        if (sy1 > sh) {
            sy1 = sh;
        }
        for (int x = 0; x < dim; ++x) {
            float dx = (x + 0.5f) - mid;
            float dy = (y + 0.5f) - mid;
            float cover = radius - sqrtf(dx * dx + dy * dy);
            if (cover <= 0.0f) {
                dst[x * 4 + 0] = 0;
                dst[x * 4 + 1] = 0;
                dst[x * 4 + 2] = 0;
                dst[x * 4 + 3] = 0;
                continue;
            }
            if (cover > 1.0f) {
                cover = 1.0f;
            }

            int sx0 = x * sw / dim;
            int sx1 = (x + 1) * sw / dim;
            if (sx1 <= sx0) {
                sx1 = sx0 + 1;
            }
            if (sx1 > sw) {
                sx1 = sw;
            }

            unsigned int r = 0;
            unsigned int g = 0;
            unsigned int b = 0;
            unsigned int n = 0;
            for (int sy = sy0; sy < sy1; ++sy) {
                const unsigned char *row = (const unsigned char *)src_data.Scan0 + sy * src_data.Stride;
                for (int sx = sx0; sx < sx1; ++sx) {
                    const unsigned char *p = row + sx * 4;
                    b += p[0];
                    g += p[1];
                    r += p[2];
                    n += 1;
                }
            }
            if (n == 0) {
                n = 1;
            }
            float a = cover * 255.0f;
            dst[x * 4 + 0] = (unsigned char)((b / n) * cover + 0.5f);
            dst[x * 4 + 1] = (unsigned char)((g / n) * cover + 0.5f);
            dst[x * 4 + 2] = (unsigned char)((r / n) * cover + 0.5f);
            dst[x * 4 + 3] = (unsigned char)(a + 0.5f);
        }
    }

    out->UnlockBits(&dst_data);
    src->UnlockBits(&src_data);
    return out;
}

static Bitmap *
avatar_fit_for(int dim)
{
    for (int i = 0; i < AVATAR_FIT_MAX; ++i) {
        if (g_avatar_fit[i] && g_avatar_fit_dim[i] == dim) {
            return g_avatar_fit[i];
        }
    }
    Bitmap *made = avatar_make_fit(g_avatar_src, dim);
    if (!made) {
        return NULL;
    }
    int slot = 0;
    for (int i = 0; i < AVATAR_FIT_MAX; ++i) {
        if (!g_avatar_fit[i]) {
            slot = i;
            break;
        }
    }
    delete g_avatar_fit[slot];
    g_avatar_fit[slot] = made;
    g_avatar_fit_dim[slot] = dim;
    return made;
}

void
icon_draw_avatar(void *hdc, const char *path, float cx, float cy, float size)
{
    if (!hdc || !path || !path[0] || size < 4.0f) {
        return;
    }
    ensure_gdiplus();
    if (!g_avatar_src || strcmp(g_avatar_loaded, path) != 0) {
        delete g_avatar_src;
        g_avatar_src = NULL;
        avatar_clear_fits();
        g_avatar_src = avatar_load_src(path);
        if (!g_avatar_src) {
            g_avatar_loaded[0] = '\0';
            return;
        }
        snprintf(g_avatar_loaded, sizeof(g_avatar_loaded), "%s", path);
    }

    int dim = (int)(size + 0.5f);
    if (dim < 8) {
        dim = 8;
    }
    Bitmap *fit = avatar_fit_for(dim);
    if (!fit) {
        return;
    }

    int x = (int)(cx - dim * 0.5f + 0.5f);
    int y = (int)(cy - dim * 0.5f + 0.5f);
    Graphics g((HDC)hdc);
    g.SetCompositingMode(CompositingModeSourceOver);
    g.SetCompositingQuality(CompositingQualityHighQuality);
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetSmoothingMode(SmoothingModeNone);
    g.DrawImage(fit, x, y, dim, dim);
}
