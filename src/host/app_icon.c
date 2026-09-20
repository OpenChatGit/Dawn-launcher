#include "app_icon.h"
#include "shared/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#ifndef IDI_APPICON
#define IDI_APPICON 1
#endif

void
app_icon_apply_win(HWND hwnd, HINSTANCE inst)
{
    int cx;
    int cy;
    HICON big;
    HICON small_icon;

    if (!hwnd || !inst) {
        return;
    }
    cx = GetSystemMetrics(SM_CXICON);
    cy = GetSystemMetrics(SM_CYICON);
    big = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    cx = GetSystemMetrics(SM_CXSMICON);
    cy = GetSystemMetrics(SM_CYSMICON);
    small_icon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    if (big) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
    }
    if (small_icon) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
    }
}

#else

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_STDIO
#include "third_party/stb_image.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

static int
find_app_png(char *out, size_t max)
{
    char root[MAX_PATH];
    char path[MAX_PATH];

    if (os_app_root(root, sizeof(root), NULL) &&
        os_join(path, sizeof(path), root, "assets/Dawn_app.png") &&
        os_file_exists(path)) {
        snprintf(out, max, "%s", path);
        return 1;
    }
    if (os_exe_dir(root, sizeof(root)) &&
        os_join(path, sizeof(path), root, "assets/Dawn_app.png") &&
        os_file_exists(path)) {
        snprintf(out, max, "%s", path);
        return 1;
    }
    return 0;
}

static unsigned char *
load_file(const char *path, int *out_n)
{
    FILE *file;
    long size;
    unsigned char *buf;

    *out_n = 0;
    file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size <= 0 || size > 16 * 1024 * 1024) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    buf = (unsigned char *)malloc((size_t)size);
    if (!buf) {
        fclose(file);
        return NULL;
    }
    if (fread(buf, 1, (size_t)size, file) != (size_t)size) {
        free(buf);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_n = (int)size;
    return buf;
}

void
app_icon_apply_x11(void *display, unsigned long window)
{
    Display *dpy = (Display *)display;
    Window win = (Window)window;
    char path[MAX_PATH];
    unsigned char *file = NULL;
    unsigned char *rgba = NULL;
    unsigned long *prop = NULL;
    int file_n = 0;
    int w = 0;
    int h = 0;
    int n = 0;
    int i;
    Atom net_wm_icon;
    XClassHint hint;

    if (!dpy || !win) {
        return;
    }
    memset(&hint, 0, sizeof(hint));
    hint.res_name = "dawn";
    hint.res_class = "Dawn";
    XSetClassHint(dpy, win, &hint);

    if (!find_app_png(path, sizeof(path))) {
        return;
    }
    file = load_file(path, &file_n);
    if (!file) {
        return;
    }
    rgba = stbi_load_from_memory(file, file_n, &w, &h, &n, 4);
    free(file);
    if (!rgba || w <= 0 || h <= 0 || (long)w * (long)h > 1024 * 1024) {
        stbi_image_free(rgba);
        return;
    }
    prop = (unsigned long *)malloc(((size_t)w * (size_t)h + 2) * sizeof(unsigned long));
    if (!prop) {
        stbi_image_free(rgba);
        return;
    }
    prop[0] = (unsigned long)w;
    prop[1] = (unsigned long)h;
    for (i = 0; i < w * h; i++) {
        unsigned r = rgba[i * 4 + 0];
        unsigned g = rgba[i * 4 + 1];
        unsigned b = rgba[i * 4 + 2];
        unsigned a = rgba[i * 4 + 3];
        prop[2 + i] = ((unsigned long)a << 24) | ((unsigned long)r << 16) |
            ((unsigned long)g << 8) | (unsigned long)b;
    }
    stbi_image_free(rgba);
    net_wm_icon = XInternAtom(dpy, "_NET_WM_ICON", False);
    XChangeProperty(
        dpy,
        win,
        net_wm_icon,
        XA_CARDINAL,
        32,
        PropModeReplace,
        (unsigned char *)prop,
        w * h + 2
    );
    free(prop);
}

#endif
