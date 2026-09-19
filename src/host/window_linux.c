#include "window.h"
#include "debug_console.h"
#include "install_job.h"
#include "media.h"
#include "self_update.h"
#include "steam_auth.h"
#include "video_bg.h"
#include "shared/draw.h"
#include "shared/soft_font.h"

static void platform_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, int alpha);
static void platform_draw_label(int x, int y, const char *text, uint32_t color, int px, int weight, int tracking);

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shared/os.h"

static HostWindow *g_window;

static void
put_pixel(int x, int y, uint32_t rgb, int alpha)
{
    if (!g_window || !g_window->pixels || alpha <= 0) {
        return;
    }
    if (x < 0 || y < 0 || x >= g_window->width || y >= g_window->height) {
        return;
    }
    uint32_t *dest = &g_window->pixels[y * g_window->width + x];
    if (alpha >= 255) {
        *dest = 0xff000000u | (rgb & 0x00ffffffu);
        return;
    }
    uint32_t d = *dest;
    int dr = (int)((d >> 16) & 0xff);
    int dg = (int)((d >> 8) & 0xff);
    int db = (int)(d & 0xff);
    int sr = (int)((rgb >> 16) & 0xff);
    int sg = (int)((rgb >> 8) & 0xff);
    int sb = (int)(rgb & 0xff);
    int inv = 255 - alpha;
    *dest = 0xff000000u | ((uint32_t)((sr * alpha + dr * inv) / 255) << 16) |
        ((uint32_t)((sg * alpha + dg * inv) / 255) << 8) |
        (uint32_t)((sb * alpha + db * inv) / 255);
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
    platform_fill_rect_alpha(x, y, w, h, color, 255);
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
    if (!g_window) {
        return;
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
            float edge = r2 - d2;
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
platform_fill_poly(const int *xy, int points, uint32_t color)
{
    (void)xy;
    (void)points;
    (void)color;
}

static void
platform_fill_polys(const int *xy, const int *counts, int poly_count, uint32_t color)
{
    (void)xy;
    (void)counts;
    (void)poly_count;
    (void)color;
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
        float len = sqrtf(dx * dx + dy * dy);
        int steps = (int)(len / 1.4f) + 1;
        for (int s = 0; s <= steps; ++s) {
            float t = (float)s / (float)steps;
            platform_fill_circle((int)(x0 + dx * t + 0.5f), (int)(y0 + dy * t + 0.5f), radius, color, alpha);
        }
    }
}

static void
platform_draw_line(int x0, int y0, int x1, int y1, uint32_t color, int width)
{
    float xy[4] = {(float)x0, (float)y0, (float)x1, (float)y1};
    platform_stroke_poly(xy, 2, color, (float)(width < 1 ? 1 : width), 255);
}

static void
platform_draw_text(int x, int y, const char *text, uint32_t color)
{
    platform_draw_label(x, y, text, color, 13, 400, 0);
}

static void
platform_measure_label(const char *text, int px, int weight, int *w, int *h, int *ascent)
{
    (void)weight;
    int n = text ? (int)strlen(text) : 0;
    if (w) {
        *w = n * (px * SOFT_FONT_ADVANCE / SOFT_FONT_ROWS);
    }
    if (h) {
        *h = px + 4;
    }
    if (ascent) {
        *ascent = px;
    }
}

static void
draw_glyph(int x, int y, int scale, char ch, uint32_t color)
{
    int row;
    int col;

    if (scale < 1) {
        scale = 1;
    }
    for (row = 0; row < SOFT_FONT_ROWS; ++row) {
        for (col = 0; col < SOFT_FONT_COLS; ++col) {
            if (!soft_font_pixel((int)(unsigned char)ch, col, row)) {
                continue;
            }
            {
                int sy;
                int sx;
                for (sy = 0; sy < scale; ++sy) {
                    for (sx = 0; sx < scale; ++sx) {
                        put_pixel(x + col * scale + sx, y + row * scale + sy, color, 255);
                    }
                }
            }
        }
    }
}

static void
platform_draw_label(int x, int y, const char *text, uint32_t color, int px, int weight, int tracking)
{
    (void)weight;
    if (!text) {
        return;
    }
    int scale = px / 8;
    if (scale < 1) {
        scale = 1;
    }
    int advance = SOFT_FONT_ADVANCE * scale + (tracking > 0 ? tracking / 20 : 0);
    for (int i = 0; text[i]; ++i) {
        draw_glyph(x + i * advance, y, scale, text[i], color);
    }
}

static void
platform_minimize(void)
{
    if (!g_window || !g_window->display) {
        return;
    }
    XIconifyWindow((Display *)g_window->display, (Window)g_window->xwindow, DefaultScreen((Display *)g_window->display));
}

static void
platform_close(void)
{
    if (g_window) {
        g_window->running = 0;
    }
}

static void
platform_drag(void)
{
}

static void
platform_log(const char *msg)
{
    debug_log("%s", msg ? msg : "");
}

int
window_create(HostWindow *window, const char *title, int width, int height)
{
    memset(window, 0, sizeof(*window));
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        return 0;
    }
    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    unsigned long black = BlackPixel(dpy, screen);
    Window win = XCreateSimpleWindow(dpy, root, 80, 80, (unsigned)width, (unsigned)height, 0, black, black);
    XStoreName(dpy, win, title ? title : "DAWN");
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask);
    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);
    XMapWindow(dpy, win);

    window->display = dpy;
    window->xwindow = (unsigned long)win;
    window->width = width;
    window->height = height;
    window->dpi_scale = 1.0f;
    window->corner_radius = 16.0f;
    window->running = 1;
    window->ready = 1;
    window->pixels = (uint32_t *)calloc((size_t)width * (size_t)height, sizeof(uint32_t));
    if (!window->pixels) {
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        return 0;
    }
    window->soft.pixels = window->pixels;
    window->soft.width = width;
    window->soft.height = height;
    XImage *image = XCreateImage(
        dpy,
        DefaultVisual(dpy, screen),
        (unsigned)DefaultDepth(dpy, screen),
        ZPixmap,
        0,
        (char *)window->pixels,
        (unsigned)width,
        (unsigned)height,
        32,
        0
    );
    window->ximage = image;
    window->gc = XCreateGC(dpy, win, 0, NULL);
    g_window = window;
    return 1;
}

void
window_destroy(HostWindow *window)
{
    if (!window) {
        return;
    }
    if (window->gc && window->display) {
        XFreeGC((Display *)window->display, (GC)window->gc);
    }
    if (window->ximage) {
        XImage *image = (XImage *)window->ximage;
        image->data = NULL;
        XDestroyImage(image);
    }
    if (window->display) {
        if (window->xwindow) {
            XDestroyWindow((Display *)window->display, (Window)window->xwindow);
        }
        XCloseDisplay((Display *)window->display);
    }
    free(window->pixels);
    memset(window, 0, sizeof(*window));
    if (g_window == window) {
        g_window = NULL;
    }
}

void
window_pump(HostWindow *window)
{
    if (!window || !window->display) {
        return;
    }
    Display *dpy = (Display *)window->display;
    XEvent ev;
    while (XPending(dpy)) {
        XNextEvent(dpy, &ev);
        if (ev.type == ClientMessage) {
            window->running = 0;
        } else if (ev.type == MotionNotify) {
            window->mouse_x = ev.xmotion.x;
            window->mouse_y = ev.xmotion.y;
        } else if (ev.type == ButtonPress) {
            window->mouse_x = ev.xbutton.x;
            window->mouse_y = ev.xbutton.y;
            if (ev.xbutton.button == 4) {
                window->mouse_wheel += 1;
            } else if (ev.xbutton.button == 5) {
                window->mouse_wheel -= 1;
            } else if (ev.xbutton.button == 1) {
                window->mouse_down = 1;
                window->mouse_pressed = 1;
            }
        } else if (ev.type == ButtonRelease) {
            if (ev.xbutton.button == 1) {
                window->mouse_down = 0;
                window->mouse_released = 1;
            }
        } else if (ev.type == KeyPress) {
            KeySym key = NoSymbol;
            char buf[32];
            int n = XLookupString(&ev.xkey, buf, (int)sizeof(buf) - 1, &key, NULL);
            if (key == XK_Escape) {
                window->key = PLATFORM_KEY_ESCAPE;
            } else if (key == XK_Return || key == XK_KP_Enter) {
                window->key = PLATFORM_KEY_ENTER;
            } else if (key == XK_BackSpace) {
                window->key = PLATFORM_KEY_BACKSPACE;
            } else if (key == XK_Tab) {
                window->key = PLATFORM_KEY_TAB;
            } else if (n > 0) {
                int i;
                for (i = 0; i < n && window->text_len < (int)sizeof(window->text) - 1; i++) {
                    if ((unsigned char)buf[i] < 32) {
                        continue;
                    }
                    window->text[window->text_len++] = buf[i];
                }
                window->text[window->text_len] = '\0';
            }
        } else if (ev.type == ConfigureNotify) {
            if (ev.xconfigure.width != window->width || ev.xconfigure.height != window->height) {
                int w = ev.xconfigure.width;
                int h = ev.xconfigure.height;
                if (w < 1) {
                    w = 1;
                }
                if (h < 1) {
                    h = 1;
                }
                uint32_t *next = (uint32_t *)realloc(window->pixels, (size_t)w * (size_t)h * sizeof(uint32_t));
                if (next) {
                    window->pixels = next;
                    window->width = w;
                    window->height = h;
                    window->soft.pixels = next;
                    window->soft.width = w;
                    window->soft.height = h;
                    if (window->ximage) {
                        XImage *image = (XImage *)window->ximage;
                        image->data = NULL;
                        XDestroyImage(image);
                    }
                    window->ximage = XCreateImage(
                        dpy,
                        DefaultVisual(dpy, DefaultScreen(dpy)),
                        (unsigned)DefaultDepth(dpy, DefaultScreen(dpy)),
                        ZPixmap,
                        0,
                        (char *)window->pixels,
                        (unsigned)w,
                        (unsigned)h,
                        32,
                        0
                    );
                }
            }
        }
    }
}

void
window_end_frame_input(HostWindow *window)
{
    window->mouse_pressed = 0;
    window->mouse_released = 0;
    window->mouse_wheel = 0;
    window->text[0] = '\0';
    window->text_len = 0;
    window->key = PLATFORM_KEY_NONE;
}

int
window_minimized(const HostWindow *window)
{
    (void)window;
    return 0;
}

int
window_focused(const HostWindow *window)
{
    Window focus = 0;
    int revert = 0;

    if (!window || !window->display) {
        return 1;
    }
    XGetInputFocus((Display *)window->display, &focus, &revert);
    return focus == (Window)window->xwindow;
}

int
window_present(HostWindow *window)
{
    if (!window || !window->display || !window->ximage) {
        return 0;
    }
    XPutImage(
        (Display *)window->display,
        (Window)window->xwindow,
        (GC)window->gc,
        (XImage *)window->ximage,
        0,
        0,
        0,
        0,
        (unsigned)window->width,
        (unsigned)window->height
    );
    XFlush((Display *)window->display);
    return 0;
}

static int
read_picker_line(const char *cmd, char *out, int max)
{
    FILE *pipe;
    size_t n;

    if (!cmd || !out || max < 8) {
        return 0;
    }
    out[0] = '\0';
    pipe = popen(cmd, "r");
    if (!pipe) {
        return 0;
    }
    if (!fgets(out, max, pipe)) {
        pclose(pipe);
        out[0] = '\0';
        return 0;
    }
    if (pclose(pipe) != 0) {
        out[0] = '\0';
        return 0;
    }
    n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) {
        out[--n] = '\0';
    }
    return out[0] == '/';
}

static int
platform_pick_folder(char *out, int max)
{
    char cmd[MAX_PATH * 2];
    const char *current = install_job_dir();
    int quoted = current && current[0] && !strchr(current, '\'');

    if (!out || max < 8) {
        return 0;
    }
    out[0] = '\0';
    if (quoted) {
        snprintf(
            cmd,
            sizeof(cmd),
            "LANG=C.UTF-8 LC_ALL=C.UTF-8 zenity --file-selection --directory --title='Choose Dawn folder' --filename='%s/' 2>/dev/null",
            current
        );
    } else {
        snprintf(cmd, sizeof(cmd), "LANG=C.UTF-8 LC_ALL=C.UTF-8 zenity --file-selection --directory --title='Choose Dawn folder' 2>/dev/null");
    }
    if (read_picker_line(cmd, out, max)) {
        return 1;
    }
    if (quoted) {
        snprintf(
            cmd,
            sizeof(cmd),
            "LANG=C.UTF-8 LC_ALL=C.UTF-8 kdialog --getexistingdirectory '%s' 'Choose Dawn folder' 2>/dev/null",
            current
        );
    } else {
        snprintf(cmd, sizeof(cmd), "LANG=C.UTF-8 LC_ALL=C.UTF-8 kdialog --getexistingdirectory \"$HOME\" 'Choose Dawn folder' 2>/dev/null");
    }
    if (read_picker_line(cmd, out, max)) {
        return 1;
    }
    snprintf(cmd, sizeof(cmd), "LANG=C.UTF-8 LC_ALL=C.UTF-8 yad --file-selection --directory --title='Choose Dawn folder' 2>/dev/null");
    return read_picker_line(cmd, out, max);
}

void
window_keep_key_focus(HostWindow *window)
{
    (void)window;
}

void
window_apply_cursor(HostWindow *window, int text)
{
    (void)window;
    (void)text;
}

void
window_bind_platform(HostWindow *window, Platform *platform, const char *project_root)
{
    memset(platform, 0, sizeof(*platform));
    platform->width = window->width;
    platform->height = window->height;
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
    platform->hdc = &window->soft;
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
