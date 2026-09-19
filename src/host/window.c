#include "window.h"
#include "debug_console.h"
#include "install_job.h"
#include "media.h"
#include "self_update.h"
#include "steam_auth.h"
#include "video_bg.h"

#include "shared/app_font.h"
#include "shared/chrome.h"
#include "shared/os.h"

#define COBJMACROS
#include <shobjidl.h>
#include <shlobj.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windowsx.h>
#include <dwmapi.h>

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

static HostWindow *g_window;
static int g_text_cursor;
static void apply_dpi_scale(HostWindow *window, float scale);
static float scale_from_dpi(UINT dpi);

static void
force_english_ui(void)
{
    SetThreadUILanguage(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
}

static void
append_window_text(HostWindow *window, const char *bytes, int n)
{
    int i;

    if (!window || !bytes || n <= 0) {
        return;
    }
    for (i = 0; i < n && window->text_len < (int)sizeof(window->text) - 1; i++) {
        unsigned char ch = (unsigned char)bytes[i];
        if (ch < 32 || ch == 127) {
            continue;
        }
        window->text[window->text_len++] = (char)ch;
    }
    window->text[window->text_len] = '\0';
}

static void
paste_clipboard(HostWindow *window)
{
    HANDLE handle;
    const wchar_t *wide;
    char utf8[256];
    int n;

    if (!window || !OpenClipboard(window->hwnd)) {
        return;
    }
    handle = GetClipboardData(CF_UNICODETEXT);
    if (handle) {
        wide = (const wchar_t *)GlobalLock(handle);
        if (wide) {
            n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, (int)sizeof(utf8), NULL, NULL);
            if (n > 1) {
                append_window_text(window, utf8, n - 1);
            }
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
}

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

static int
chrome_px(float value, float scale)
{
    return (int)(value * scale + 0.5f);
}

static float
round_rect_coverage(int x, int y, int w, int h, float radius)
{
    float fx = (float)x + 0.5f;
    float fy = (float)y + 0.5f;
    if (radius < 1.0f || w < 2 || h < 2) {
        return 1.0f;
    }

    float r = radius;
    float cx = 0.0f;
    float cy = 0.0f;
    int in_corner = 0;

    if (fx < r && fy < r) {
        cx = r;
        cy = r;
        in_corner = 1;
    } else if (fx >= (float)w - r && fy < r) {
        cx = (float)w - r;
        cy = r;
        in_corner = 1;
    } else if (fx < r && fy >= (float)h - r) {
        cx = r;
        cy = (float)h - r;
        in_corner = 1;
    } else if (fx >= (float)w - r && fy >= (float)h - r) {
        cx = (float)w - r;
        cy = (float)h - r;
        in_corner = 1;
    }

    if (!in_corner) {
        return 1.0f;
    }

    float dx = fx - cx;
    float dy = fy - cy;
    float d = (float)sqrt((double)(dx * dx + dy * dy));
    float edge = r - d;
    if (edge >= 0.5f) {
        return 1.0f;
    }
    if (edge <= -0.5f) {
        return 0.0f;
    }
    return edge + 0.5f;
}

static void
window_apply_round(HostWindow *window)
{
    if (!window || !window->hwnd || window->width < 8 || window->height < 8) {
        return;
    }

    DWORD preference = DWMWCP_ROUND;
    HRESULT hr = DwmSetWindowAttribute(
        window->hwnd,
        DWMWA_WINDOW_CORNER_PREFERENCE,
        &preference,
        sizeof(preference)
    );
    if (SUCCEEDED(hr)) {
        return;
    }

    int radius = (int)(window->corner_radius + 0.5f);
    if (radius < 8) {
        radius = 8;
    }
    HRGN region = CreateRoundRectRgn(
        0,
        0,
        window->width + 1,
        window->height + 1,
        radius * 2,
        radius * 2
    );
    if (region) {
        SetWindowRgn(window->hwnd, region, TRUE);
    }
}

static COLORREF
to_color(uint32_t rgb)
{
    return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

static void
put_pixel(int x, int y, uint32_t rgb, int alpha)
{
    if (!g_window || !g_window->pixels) {
        return;
    }
    if (x < 0 || y < 0 || x >= g_window->width || y >= g_window->height) {
        return;
    }
    if (alpha <= 0) {
        return;
    }

    uint32_t *dest = &g_window->pixels[y * g_window->width + x];
    if (alpha >= 255) {
        *dest = 0xff000000u | (rgb & 0x00ffffffu);
        return;
    }

    uint32_t d = *dest;
    int db = (int)(d & 0xff);
    int dg = (int)((d >> 8) & 0xff);
    int dr = (int)((d >> 16) & 0xff);
    int sr = (int)((rgb >> 16) & 0xff);
    int sg = (int)((rgb >> 8) & 0xff);
    int sb = (int)(rgb & 0xff);
    int inv = 255 - alpha;
    int r = (sr * alpha + dr * inv) / 255;
    int g = (sg * alpha + dg * inv) / 255;
    int b = (sb * alpha + db * inv) / 255;
    *dest = 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void
ensure_backbuffer(HostWindow *window, int width, int height)
{
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }
    if (window->back_bitmap && window->width == width && window->height == height) {
        return;
    }

    if (window->back_bitmap) {
        SelectObject(window->back_dc, window->old_bitmap);
        DeleteObject(window->back_bitmap);
        window->back_bitmap = NULL;
        window->pixels = NULL;
    }

    window->width = width;
    window->height = height;

    BITMAPINFO info;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    window->back_bitmap = CreateDIBSection(
        window->hdc, &info, DIB_RGB_COLORS, (void **)&window->pixels, NULL, 0
    );
    window->old_bitmap = SelectObject(window->back_dc, window->back_bitmap);
}

static LRESULT CALLBACK
window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    HostWindow *window = g_window;

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (window && window->back_dc && window->back_bitmap) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            BitBlt(hdc, 0, 0, window->width, window->height, window->back_dc, 0, 0, SRCCOPY);
            EndPaint(hwnd, &ps);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_NCLBUTTONDBLCLK:
        return 0;
    case WM_NCHITTEST:
        if (window) {
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &pt);
            if (pt.x < 0 || pt.y < 0 || pt.x >= window->width || pt.y >= window->height) {
                return HTNOWHERE;
            }
            if (round_rect_coverage(pt.x, pt.y, window->width, window->height, window->corner_radius) < 0.45f) {
                return HTTRANSPARENT;
            }

            int bar = chrome_px(CHROME_TITLEBAR, window->dpi_scale);
            int controls = chrome_px(CHROME_CONTROLS_WIDTH, window->dpi_scale);
            if (pt.y < bar) {
                if (pt.x >= window->width - controls) {
                    return HTCLIENT;
                }
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        return HTCLIENT;
    case WM_SIZE:
        if (window && window->ready && wparam != SIZE_MINIMIZED) {
            int width = LOWORD(lparam);
            int height = HIWORD(lparam);
            if (width >= 8 && height >= 8) {
                ensure_backbuffer(window, width, height);
                window_apply_round(window);
            }
        }
        return 0;
    case WM_DPICHANGED:
        if (window && lparam) {
            RECT *hint = (RECT *)lparam;
            int w = hint->right - hint->left;
            int h = hint->bottom - hint->top;
            apply_dpi_scale(window, scale_from_dpi(HIWORD(wparam)));
            SetWindowPos(
                hwnd,
                NULL,
                hint->left,
                hint->top,
                w,
                h,
                SWP_NOZORDER | SWP_NOACTIVATE
            );
            if (w >= 8 && h >= 8) {
                ensure_backbuffer(window, w, h);
                window_apply_round(window);
            }
        }
        return 0;
    case WM_MOUSEMOVE:
        if (window) {
            window->mouse_x = GET_X_LPARAM(lparam);
            window->mouse_y = GET_Y_LPARAM(lparam);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (window) {
            window->mouse_x = GET_X_LPARAM(lparam);
            window->mouse_y = GET_Y_LPARAM(lparam);
            window->mouse_down = 1;
            window->mouse_pressed = 1;
            SetCapture(hwnd);
        }
        return 0;
    case WM_LBUTTONUP:
        if (window) {
            window->mouse_x = GET_X_LPARAM(lparam);
            window->mouse_y = GET_Y_LPARAM(lparam);
            window->mouse_down = 0;
            window->mouse_released = 1;
            ReleaseCapture();
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (window) {
            window->mouse_wheel += GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
        }
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    case WM_SETCURSOR:
        if (g_text_cursor && LOWORD(lparam) == HTCLIENT) {
            SetCursor(LoadCursorA(NULL, IDC_IBEAM));
            return TRUE;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_CHAR:
        /* Plain Ctrl shortcuts are handled on KEYDOWN. AltGr is Ctrl+Alt and must still type. */
        if ((GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
            return 0;
        }
        if (window && wparam >= 32 && window->text_len < (int)sizeof(window->text) - 1) {
            wchar_t wc = (wchar_t)wparam;
            char utf8[8];
            int n = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, utf8, (int)sizeof(utf8), NULL, NULL);
            if (n > 0) {
                append_window_text(window, utf8, n);
            }
            return 0;
        }
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wparam == VK_F12) {
            debug_console_toggle();
            return 0;
        }
        if (window &&
            (GetKeyState(VK_CONTROL) & 0x8000) &&
            !(GetKeyState(VK_MENU) & 0x8000) &&
            (wparam == 'V' || wparam == 'v')) {
            paste_clipboard(window);
            return 0;
        }
        if (window) {
            if (wparam == VK_BACK) {
                window->key = PLATFORM_KEY_BACKSPACE;
            } else if (wparam == VK_TAB) {
                window->key = PLATFORM_KEY_TAB;
            } else if (wparam == VK_RETURN) {
                window->key = PLATFORM_KEY_ENTER;
            } else if (wparam == VK_ESCAPE) {
                window->key = PLATFORM_KEY_ESCAPE;
            }
        }
        if (wparam == VK_BACK || wparam == VK_TAB || wparam == VK_RETURN || wparam == VK_ESCAPE) {
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_HOTKEY:
        if (wparam == 12) {
            debug_console_toggle();
            return 0;
        }
        return 0;
    case WM_CLOSE:
        if (window) {
            window->running = 0;
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

static void
platform_clear(uint32_t color)
{
    if (!g_window || !g_window->pixels) {
        return;
    }
    uint32_t packed = 0xff000000u | (color & 0x00ffffffu);
    int total = g_window->width * g_window->height;
    for (int i = 0; i < total; ++i) {
        g_window->pixels[i] = packed;
    }
}

static void
platform_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    RECT rect = {x, y, x + w, y + h};
    HBRUSH brush = CreateSolidBrush(to_color(color));
    FillRect(g_window->back_dc, &rect, brush);
    DeleteObject(brush);
}

static void
platform_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, int alpha)
{
    int x1 = x + w;
    int y1 = y + h;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x1 > g_window->width) {
        x1 = g_window->width;
    }
    if (y1 > g_window->height) {
        y1 = g_window->height;
    }
    for (int py = y; py < y1; ++py) {
        for (int px = x; px < x1; ++px) {
            put_pixel(px, py, color, alpha);
        }
    }
}

static void
platform_fill_circle(int cx, int cy, float radius, uint32_t color, int alpha)
{
    int r = (int)(radius + 1.5f);
    float r2 = radius * radius;
    for (int y = -r; y <= r; ++y) {
        for (int x = -r; x <= r; ++x) {
            float d2 = (float)(x * x + y * y);
            if (d2 > r2 + radius) {
                continue;
            }
            int a = alpha;
            float edge = (float)(radius * radius) - d2;
            if (edge < radius * 2.0f) {
                float t = edge / (radius * 2.0f);
                if (t < 0.0f) {
                    t = 0.0f;
                }
                a = (int)(alpha * t);
            }
            if (a > 0) {
                put_pixel(cx + x, cy + y, color, a);
            }
        }
    }
}

static void
copy_points(const int *xy, int points, POINT *out)
{
    for (int i = 0; i < points; ++i) {
        out[i].x = xy[i * 2];
        out[i].y = xy[i * 2 + 1];
    }
}

static void
platform_fill_poly(const int *xy, int points, uint32_t color)
{
    if (points < 3) {
        return;
    }
    POINT pts[256];
    if (points > 256) {
        points = 256;
    }
    copy_points(xy, points, pts);
    HBRUSH brush = CreateSolidBrush(to_color(color));
    HBRUSH old = SelectObject(g_window->back_dc, brush);
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    HPEN old_pen = SelectObject(g_window->back_dc, pen);
    SetPolyFillMode(g_window->back_dc, ALTERNATE);
    Polygon(g_window->back_dc, pts, points);
    SelectObject(g_window->back_dc, old_pen);
    SelectObject(g_window->back_dc, old);
    DeleteObject(pen);
    DeleteObject(brush);
}

static void
platform_fill_polys(const int *xy, const int *counts, int poly_count, uint32_t color)
{
    POINT pts[2048];
    int total = 0;
    for (int i = 0; i < poly_count; ++i) {
        total += counts[i];
    }
    if (total > 2048) {
        return;
    }
    copy_points(xy, total, pts);
    HBRUSH brush = CreateSolidBrush(to_color(color));
    HBRUSH old = SelectObject(g_window->back_dc, brush);
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    HPEN old_pen = SelectObject(g_window->back_dc, pen);
    SetPolyFillMode(g_window->back_dc, ALTERNATE);
    PolyPolygon(g_window->back_dc, pts, counts, poly_count);
    SelectObject(g_window->back_dc, old_pen);
    SelectObject(g_window->back_dc, old);
    DeleteObject(pen);
    DeleteObject(brush);
}

static void
platform_stroke_poly(const float *xy, int points, uint32_t color, float width, int alpha)
{
    if (points < 2) {
        return;
    }
    float radius = width * 0.5f;
    if (radius < 0.6f) {
        radius = 0.6f;
    }
    for (int i = 0; i < points - 1; ++i) {
        float x0 = xy[i * 2];
        float y0 = xy[i * 2 + 1];
        float x1 = xy[(i + 1) * 2];
        float y1 = xy[(i + 1) * 2 + 1];
        float dx = x1 - x0;
        float dy = y1 - y0;
        float len = (float)sqrt((double)(dx * dx + dy * dy));
        int steps = (int)(len / 1.4f) + 1;
        for (int s = 0; s <= steps; ++s) {
            float t = (float)s / (float)steps;
            int cx = (int)(x0 + dx * t + 0.5f);
            int cy = (int)(y0 + dy * t + 0.5f);
            platform_fill_circle(cx, cy, radius, color, alpha);
        }
    }
}

static void
platform_draw_line(int x0, int y0, int x1, int y1, uint32_t color, int width)
{
    HPEN pen = CreatePen(PS_SOLID, width < 1 ? 1 : width, to_color(color));
    HPEN old = SelectObject(g_window->back_dc, pen);
    MoveToEx(g_window->back_dc, x0, y0, NULL);
    LineTo(g_window->back_dc, x1, y1);
    SelectObject(g_window->back_dc, old);
    DeleteObject(pen);
}

static void platform_draw_label(int x, int y, const char *text, uint32_t color, int px, int weight, int tracking);

static void
platform_draw_text(int x, int y, const char *text, uint32_t color)
{
    platform_draw_label(x, y, text, color, 13, 400, 0);
}

typedef struct CachedFont {
    HFONT font;
    int px;
    int weight;
} CachedFont;

static CachedFont g_fonts[8];
static int g_font_count;

static HFONT
cached_font(int px, int weight)
{
    for (int i = 0; i < g_font_count; ++i) {
        if (g_fonts[i].px == px && g_fonts[i].weight == weight) {
            return g_fonts[i].font;
        }
    }
    HFONT font = CreateFontW(
        -px, 0, 0, 0, weight >= 600 ? FW_SEMIBOLD : weight,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS,
        weight >= 600 ? L"Segoe UI Semibold" : L"Segoe UI"
    );
    if (!font) {
        font = CreateFontW(
            -px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI"
        );
    }
    if (!font) {
        return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    if (g_font_count < 8) {
        g_fonts[g_font_count].font = font;
        g_fonts[g_font_count].px = px;
        g_fonts[g_font_count].weight = weight;
        g_font_count += 1;
        return font;
    }
    return font;
}

static void
free_cached_fonts(void)
{
    for (int i = 0; i < g_font_count; ++i) {
        DeleteObject(g_fonts[i].font);
        g_fonts[i].font = NULL;
    }
    g_font_count = 0;
}

static void
platform_measure_label(const char *text, int px, int weight, int *w, int *h, int *ascent)
{
    HFONT font;
    HFONT old;
    TEXTMETRIC tm;
    SIZE size = {0, 0};
    wchar_t wide[256];

    if (px < 1) {
        px = 1;
    }
    font = cached_font(px, weight);
    old = SelectObject(g_window->back_dc, font);
    SetGraphicsMode(g_window->back_dc, GM_COMPATIBLE);
    SetMapMode(g_window->back_dc, MM_TEXT);
    GetTextMetrics(g_window->back_dc, &tm);
    if (text && text[0] && os_utf8_to_wide(text, wide, 256) > 0) {
        GetTextExtentPoint32W(g_window->back_dc, wide, (int)wcslen(wide), &size);
    }
    if (w) {
        *w = size.cx;
    }
    if (h) {
        *h = tm.tmHeight;
    }
    if (ascent) {
        *ascent = tm.tmAscent - tm.tmInternalLeading;
    }
    SelectObject(g_window->back_dc, old);
}

static void
platform_draw_label(int x, int y, const char *text, uint32_t color, int px, int weight, int tracking)
{
    wchar_t wide[256];
    HFONT font;
    HFONT old;

    if (!text || !text[0]) {
        return;
    }
    if (px < 1) {
        px = 1;
    }
    if (os_utf8_to_wide(text, wide, 256) <= 0) {
        return;
    }
    font = cached_font(px, weight);
    old = SelectObject(g_window->back_dc, font);
    SetGraphicsMode(g_window->back_dc, GM_COMPATIBLE);
    SetMapMode(g_window->back_dc, MM_TEXT);
    SetBkMode(g_window->back_dc, TRANSPARENT);
    SetTextColor(g_window->back_dc, to_color(color));
    SetTextAlign(g_window->back_dc, TA_LEFT | TA_TOP);
    SetTextCharacterExtra(g_window->back_dc, tracking > 0 ? tracking : 0);
    TextOutW(g_window->back_dc, x, y, wide, (int)wcslen(wide));
    SetTextCharacterExtra(g_window->back_dc, 0);
    SelectObject(g_window->back_dc, old);
}

static void
platform_minimize(void)
{
    ShowWindow(g_window->hwnd, SW_MINIMIZE);
}

static void
platform_close(void)
{
    g_window->running = 0;
}

static void
platform_drag(void)
{
    ReleaseCapture();
    g_window->mouse_down = 0;
    SendMessageW(g_window->hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

static void
platform_log(const char *msg)
{
    debug_log("%s", msg ? msg : "");
}

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)(intptr_t)-4)
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((HANDLE)(intptr_t)-3)
#endif

static float
clamp_dpi_scale(float scale)
{
    if (scale < 0.75f) {
        return 1.0f;
    }
    if (scale > 4.0f) {
        return 4.0f;
    }
    return scale;
}

static float
scale_from_dpi(UINT dpi)
{
    if (dpi < 72) {
        return 1.0f;
    }
    return clamp_dpi_scale((float)dpi / 96.0f);
}

static UINT
monitor_effective_dpi(HMONITOR monitor)
{
    HMODULE shcore;
    UINT x = 0;
    UINT y = 0;

    if (!monitor) {
        return 0;
    }
    shcore = GetModuleHandleW(L"shcore.dll");
    if (!shcore) {
        shcore = LoadLibraryW(L"shcore.dll");
    }
    if (shcore) {
        typedef HRESULT (WINAPI *GetDpiForMonitorFn)(HMONITOR, int, UINT *, UINT *);
        GetDpiForMonitorFn get_dpi = NULL;
        FARPROC proc = GetProcAddress(shcore, "GetDpiForMonitor");
        memcpy(&get_dpi, &proc, sizeof(get_dpi));
        if (get_dpi && get_dpi(monitor, 0, &x, &y) == S_OK && x > 0) {
            return x;
        }
    }
    return 0;
}

static UINT
system_dpi(void)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef UINT (WINAPI *GetDpiForSystemFn)(void);
        GetDpiForSystemFn get_sys = NULL;
        FARPROC proc = GetProcAddress(user32, "GetDpiForSystem");
        memcpy(&get_sys, &proc, sizeof(get_sys));
        if (get_sys) {
            UINT dpi = get_sys();
            if (dpi > 0) {
                return dpi;
            }
        }
    }
    {
        HDC hdc = GetDC(NULL);
        if (hdc) {
            int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(NULL, hdc);
            if (dpi > 0) {
                return (UINT)dpi;
            }
        }
    }
    return 96;
}

static UINT
query_dpi(HWND hwnd, HMONITOR monitor)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (hwnd && user32) {
        typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
        GetDpiForWindowFn get_win = NULL;
        FARPROC proc = GetProcAddress(user32, "GetDpiForWindow");
        memcpy(&get_win, &proc, sizeof(get_win));
        if (get_win) {
            UINT dpi = get_win(hwnd);
            if (dpi > 0) {
                return dpi;
            }
        }
    }
    if (!monitor && hwnd) {
        monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    }
    {
        UINT dpi = monitor_effective_dpi(monitor);
        if (dpi > 0) {
            return dpi;
        }
    }
    return system_dpi();
}

void
window_enable_dpi_awareness(void)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL (WINAPI *SetDpiAwarenessContextFn)(HANDLE);
        SetDpiAwarenessContextFn set_ctx = NULL;
        FARPROC proc = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        memcpy(&set_ctx, &proc, sizeof(set_ctx));
        if (set_ctx) {
            if (set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                return;
            }
            if (set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) {
                return;
            }
        }
    }
    {
        HMODULE shcore = GetModuleHandleW(L"shcore.dll");
        if (!shcore) {
            shcore = LoadLibraryW(L"shcore.dll");
        }
        if (shcore) {
            typedef HRESULT (WINAPI *SetProcessDpiAwarenessFn)(int);
            SetProcessDpiAwarenessFn set = NULL;
            FARPROC proc = GetProcAddress(shcore, "SetProcessDpiAwareness");
            memcpy(&set, &proc, sizeof(set));
            if (set && set(2) == S_OK) {
                return;
            }
        }
    }
    if (user32) {
        typedef BOOL (WINAPI *SetProcessDPIAwareFn)(void);
        SetProcessDPIAwareFn set = NULL;
        FARPROC proc = GetProcAddress(user32, "SetProcessDPIAware");
        memcpy(&set, &proc, sizeof(set));
        if (set) {
            set();
        }
    }
}

static void
apply_dpi_scale(HostWindow *window, float scale)
{
    int px;

    if (!window) {
        return;
    }
    window->dpi_scale = clamp_dpi_scale(scale);
    window->corner_radius = 16.0f * window->dpi_scale;
    px = (int)(13.0f * window->dpi_scale + 0.5f);
    if (px < 11) {
        px = 11;
    }
    if (!window->back_dc) {
        return;
    }
    if (window->old_font) {
        SelectObject(window->back_dc, window->old_font);
        window->old_font = NULL;
    }
    if (window->font) {
        DeleteObject(window->font);
        window->font = NULL;
    }
    window->font = CreateFontW(
        -px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI"
    );
    if (window->font) {
        window->old_font = SelectObject(window->back_dc, window->font);
    }
}

int
window_create(HostWindow *window, const char *title, int width, int height)
{
    HMONITOR monitor;
    MONITORINFO mi;
    POINT cursor;
    float scale;
    int x;
    int y;
    int logical_w = width;
    int logical_h = height;

    memset(window, 0, sizeof(*window));
    g_window = window;
    window->running = 1;
    force_english_ui();
    window_enable_dpi_awareness();

    cursor.x = 0;
    cursor.y = 0;
    if (!GetCursorPos(&cursor)) {
        cursor.x = 0;
        cursor.y = 0;
    }
    monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    scale = scale_from_dpi(query_dpi(NULL, monitor));
    width = (int)((float)logical_w * scale + 0.5f);
    height = (int)((float)logical_h * scale + 0.5f);
    window->width = width;
    window->height = height;
    apply_dpi_scale(window, scale);

    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) {
        mi.rcWork.left = 0;
        mi.rcWork.top = 0;
        mi.rcWork.right = GetSystemMetrics(SM_CXSCREEN);
        mi.rcWork.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - width) / 2;
    y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - height) / 2;

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(1, 6, 19));
    wc.lpszClassName = L"AppHostWindow";

    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return 0;
        }
    }

    wchar_t wide_title[256];
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wide_title, 256);

    window->hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        wide_title,
        WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
        x,
        y,
        width,
        height,
        NULL,
        NULL,
        wc.hInstance,
        NULL
    );

    if (!window->hwnd) {
        return 0;
    }
    RegisterHotKey(window->hwnd, 12, 0, VK_F12);

    window->hdc = GetDC(window->hwnd);
    window->back_dc = CreateCompatibleDC(window->hdc);
    ensure_backbuffer(window, width, height);
    {
        float hwnd_scale = scale_from_dpi(query_dpi(window->hwnd, NULL));
        if (hwnd_scale > 0.1f && (hwnd_scale < scale - 0.01f || hwnd_scale > scale + 0.01f)) {
            int new_w = (int)((float)logical_w * hwnd_scale + 0.5f);
            int new_h = (int)((float)logical_h * hwnd_scale + 0.5f);
            apply_dpi_scale(window, hwnd_scale);
            SetWindowPos(window->hwnd, NULL, 0, 0, new_w, new_h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            ensure_backbuffer(window, new_w, new_h);
        } else {
            apply_dpi_scale(window, scale);
        }
    }

    {
        char root[MAX_PATH];
        os_app_root(root, sizeof(root), NULL);
        app_font_set_root(root);
        app_font_register();
    }
    window->ready = 1;
    window_apply_round(window);
    return 1;
}

void
window_destroy(HostWindow *window)
{
    if (window->back_dc && window->old_font) {
        SelectObject(window->back_dc, window->old_font);
    }
    if (window->font) {
        DeleteObject(window->font);
    }
    if (window->back_bitmap) {
        SelectObject(window->back_dc, window->old_bitmap);
        DeleteObject(window->back_bitmap);
    }
    if (window->back_dc) {
        DeleteDC(window->back_dc);
    }
    if (window->hdc && window->hwnd) {
        ReleaseDC(window->hwnd, window->hdc);
    }
    if (window->hwnd) {
        UnregisterHotKey(window->hwnd, 12);
        DestroyWindow(window->hwnd);
    }
    free_cached_fonts();
    g_window = NULL;
}

void
window_pump(HostWindow *window)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            window->running = 0;
            return;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void
window_end_frame_input(HostWindow *window)
{
    window->mouse_pressed = 0;
    window->mouse_released = 0;
    window->mouse_wheel = 0;
    window->text_len = 0;
    window->text[0] = '\0';
    window->key = PLATFORM_KEY_NONE;
}

int
window_minimized(const HostWindow *window)
{
    return window && window->hwnd && IsIconic(window->hwnd);
}

int
window_focused(const HostWindow *window)
{
    HWND fg;
    DWORD pid = 0;

    if (!window || !window->hwnd) {
        return 0;
    }
    fg = GetForegroundWindow();
    if (!fg) {
        return 0;
    }
    if (fg == window->hwnd) {
        return 1;
    }
    /* Our own popups (folder picker, message boxes) count as focused. */
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

int
window_present(HostWindow *window)
{
    if (!window || !window->hwnd || !window->back_dc) {
        return 0;
    }
    if (IsIconic(window->hwnd)) {
        return 0;
    }
    if (!window->shown) {
        ShowWindow(window->hwnd, SW_SHOW);
        window->shown = 1;
    }
    HDC hdc = GetDC(window->hwnd);
    if (hdc) {
        BitBlt(hdc, 0, 0, window->width, window->height, window->back_dc, 0, 0, SRCCOPY);
        ReleaseDC(window->hwnd, hdc);
    }
    return SUCCEEDED(DwmFlush());
}

static int
platform_pick_folder(char *out, int max)
{
    IFileOpenDialog *dlg = NULL;
    IShellItem *item = NULL;
    PWSTR wpath = NULL;
    HRESULT hr;
    HRESULT com;
    DWORD opts = 0;
    int ok = 0;

    if (!out || max < 8) {
        return 0;
    }
    out[0] = '\0';
    force_english_ui();
    com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    hr = CoCreateInstance(
        &CLSID_FileOpenDialog,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IFileOpenDialog,
        (void **)&dlg
    );
    if (FAILED(hr) || !dlg) {
        if (com == S_OK) {
            CoUninitialize();
        }
        return 0;
    }
    if (SUCCEEDED(IFileOpenDialog_GetOptions(dlg, &opts))) {
        IFileOpenDialog_SetOptions(dlg, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    }
    IFileOpenDialog_SetTitle(dlg, L"Choose Dawn folder");
    {
        const char *current = install_job_dir();
        if (current && current[0]) {
            wchar_t start[MAX_PATH];
            IShellItem *folder = NULL;
            os_utf8_to_wide(current, start, MAX_PATH);
            if (SUCCEEDED(SHCreateItemFromParsingName(start, NULL, &IID_IShellItem, (void **)&folder)) && folder) {
                IFileOpenDialog_SetFolder(dlg, folder);
                IShellItem_Release(folder);
            }
        }
    }
    hr = IFileOpenDialog_Show(dlg, g_window ? g_window->hwnd : NULL);
    if (SUCCEEDED(hr) && SUCCEEDED(IFileOpenDialog_GetResult(dlg, &item)) && item) {
        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &wpath)) && wpath) {
            WideCharToMultiByte(CP_UTF8, 0, wpath, -1, out, max, NULL, NULL);
            CoTaskMemFree(wpath);
            ok = out[0] != '\0';
        }
        IShellItem_Release(item);
    }
    IFileOpenDialog_Release(dlg);
    if (com == S_OK) {
        CoUninitialize();
    }
    return ok;
}

void
window_keep_key_focus(HostWindow *window)
{
    if (!window || !window->hwnd) {
        return;
    }
    if (GetForegroundWindow() != window->hwnd) {
        return;
    }
    if (GetFocus() != window->hwnd) {
        SetFocus(window->hwnd);
    }
}

void
window_apply_cursor(HostWindow *window, int text)
{
    (void)window;
    g_text_cursor = text ? 1 : 0;
}

void
window_bind_platform(HostWindow *window, Platform *platform, const char *project_root)
{
    memset(platform, 0, sizeof(*platform));
    platform->width = window->width;
    platform->height = window->height;
    if (window->hwnd) {
        POINT pt;
        if (GetCursorPos(&pt) && ScreenToClient(window->hwnd, &pt)) {
            window->mouse_x = pt.x;
            window->mouse_y = pt.y;
        }
    }
    platform->mouse_x = window->mouse_x;
    platform->mouse_y = window->mouse_y;
    platform->mouse_down = window->mouse_down;
    platform->mouse_pressed = window->mouse_pressed;
    platform->mouse_released = window->mouse_released;
    platform->mouse_wheel = window->mouse_wheel;
    memcpy(platform->text, window->text, sizeof(platform->text));
    platform->text_len = window->text_len;
    platform->key = window->key;
    platform->project_root = project_root;
    platform->hdc = window->back_dc;
    platform->dpi_scale = window->dpi_scale > 0.1f ? window->dpi_scale : 1.0f;
    platform->corner_radius = window->corner_radius > 1.0f ? window->corner_radius : 16.0f;
    platform->pixels = window->pixels;
    platform->clear = platform_clear;
    platform->fill_rect = platform_fill_rect;
    platform->fill_rect_alpha = platform_fill_rect_alpha;
    platform->fill_circle = platform_fill_circle;
    platform->fill_poly = platform_fill_poly;
    platform->fill_polys = platform_fill_polys;
    platform->stroke_poly = platform_stroke_poly;
    platform->draw_line = platform_draw_line;
    platform->draw_text = platform_draw_text;
    platform->draw_label = platform_draw_label;
    platform->measure_label = platform_measure_label;
    platform->minimize = platform_minimize;
    platform->close = platform_close;
    platform->drag = platform_drag;
    platform->log = platform_log;
    platform->media_set_url = media_set_url;
    platform->media_set_playing = media_set_playing;
    platform->media_set_loop = media_set_loop;
    platform->media_set_volume = media_set_volume;
    platform->media_playing = media_is_playing;
    platform->video_set_source = video_bg_set_source;
    platform->video_set_gain = video_bg_set_gain;
    platform->video_lock_frame = video_bg_lock_frame;
    platform->video_unlock_frame = video_bg_unlock_frame;
    platform->video_frame_gen = video_bg_generation;
    platform->video_set_output = video_bg_set_output;
    platform->video_shared_handle = video_bg_shared_handle;
    platform->video_frame_size = video_bg_frame_size;
    platform->media_busy = media_is_busy;
    platform->media_level = media_level;
    platform->embed_set_view = media_embed_set_view;
    platform->install_set_dir = install_job_set_dir;
    platform->install_dir = install_job_dir;
    platform->pick_folder = platform_pick_folder;
    platform->install_set_user = install_job_set_user;
    platform->install_submit_secret = install_job_submit_secret;
    platform->install_start = install_job_start;
    platform->install_cancel = install_job_cancel;
    platform->install_pause = install_job_pause;
    platform->install_paused = install_job_paused;
    platform->install_can_pause = install_job_can_pause;
    platform->install_can_simulate = install_job_can_simulate;
    platform->install_simulate = install_job_simulate;
    platform->install_busy = install_job_busy;
    platform->install_need = install_job_need;
    platform->install_progress = install_job_progress;
    platform->install_status = install_job_status;
    platform->install_ready = install_job_ready;
    platform->install_launch = install_job_launch;
    platform->install_uninstall = install_job_uninstall;
    platform->install_parts = install_job_parts;
    platform->install_uninstall_part = install_job_uninstall_part;
    platform->install_verify = install_job_verify;
    platform->game_state = install_job_game_state;
    platform->game_stop = install_job_game_stop;
    platform->install_set_language = install_job_set_language;
    platform->install_language = install_job_language;
    platform->install_language_label = install_job_language_label;
    platform->steam_sign_in = steam_auth_begin;
    platform->steam_sign_out = steam_auth_sign_out;
    platform->steam_cancel = steam_auth_cancel;
    platform->steam_signed_in = steam_auth_signed_in;
    platform->steam_busy = steam_auth_busy;
    platform->steam_persona = steam_auth_persona;
    platform->steam_id = steam_auth_id;
    platform->steam_avatar_path = steam_auth_avatar_path;
    platform->steam_status = steam_auth_status;
    platform->steam_owns_d2 = steam_auth_owns_d2;
    platform->steam_owns_forsaken = steam_auth_owns_forsaken;
    platform->steam_owns_shadowkeep = steam_auth_owns_shadowkeep;
    platform->update_available = self_update_available;
    platform->update_version = self_update_version;
    platform->update_busy = self_update_busy;
    platform->update_status = self_update_status;
    platform->update_begin = self_update_begin;
    platform->update_cancel = self_update_cancel;
}
