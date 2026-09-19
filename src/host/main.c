#include "debug_console.h"
#include "builder.h"
#include "config.h"
#include "hot_reload.h"
#include "install_job.h"
#include "self_update.h"
#include "steam_auth.h"
#include "watcher.h"
#include "media.h"
#include "window.h"
#include "shared/os.h"
#include "shared/user_id.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static char g_app_root[MAX_PATH];

static void
host_log(const char *text)
{
    debug_log("%s", text ? text : "");
}

static void
resolve_app_root(void)
{
    os_app_root(g_app_root, sizeof(g_app_root), APP_PROJECT_ROOT);
}

#ifdef _WIN32
static float
elapsed_seconds(LARGE_INTEGER *last, LARGE_INTEGER freq)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = (float)(now.QuadPart - last->QuadPart) / (float)freq.QuadPart;
    *last = now;
    return dt;
}

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)instance;
    (void)prev;
    (void)show;

    if (cmd && strstr(cmd, "--add-openid-host")) {
        return steam_auth_install_openid_host() ? 0 : 1;
    }

    window_enable_dpi_awareness();
    FreeConsole();
    debug_console_init();
    debug_log("Dawn %s", APP_VERSION);
    builder_prepare_path();

    HostWindow window;
    if (!window_create(&window, "DAWN", 1200, 620)) {
        MessageBoxA(NULL, "Window create failed", "Dawn", MB_ICONERROR);
        return 1;
    }
    resolve_app_root();
    media_init(window.hwnd, g_app_root);
#else
#include <time.h>

static float
elapsed_seconds(struct timespec *last)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    float dt = (float)(now.tv_sec - last->tv_sec) + (float)(now.tv_nsec - last->tv_nsec) / 1000000000.0f;
    *last = now;
    return dt;
}

int
main(void)
{
    debug_console_init();
    debug_log("Dawn %s", APP_VERSION);
    builder_prepare_path();

    HostWindow window;
    if (!window_create(&window, "DAWN", 1200, 620)) {
        fprintf(stderr, "Window create failed\n");
        return 1;
    }
    resolve_app_root();
    media_init(NULL, g_app_root);
#endif
    install_job_init(g_app_root);
    self_update_init();
    steam_auth_init(g_app_root);
    user_id_get();

    static AppMemory memory;
    if (!hot_reload_init(&memory)) {
        host_log(hot_reload_error());
#ifdef _WIN32
        MessageBoxA(NULL, hot_reload_error(), "Dawn", MB_ICONERROR);
#else
        fprintf(stderr, "%s\n", hot_reload_error());
#endif
        return 1;
    } else if (hot_reload_api() && hot_reload_api()->init) {
        hot_reload_api()->init(&memory);
    }

    if (!watcher_init(g_app_root)) {
        host_log("file watcher failed");
    }

#ifdef _WIN32
    LARGE_INTEGER freq;
    LARGE_INTEGER last;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
#else
    struct timespec last;
    clock_gettime(CLOCK_MONOTONIC, &last);
#endif

    int rebuild_pending = 0;
    uint32_t debounce_until = 0;

    while (window.running) {
        window_pump(&window);
        media_poll();
        install_job_poll();
        self_update_poll();
        if (self_update_should_quit()) {
            window.running = 0;
            continue;
        }

#ifdef _WIN32
        float dt = elapsed_seconds(&last, freq);
#else
        float dt = elapsed_seconds(&last);
#endif
        if (dt > 0.1f) {
            dt = 0.1f;
        }

        if (watcher_poll()) {
            rebuild_pending = 1;
            debounce_until = os_tick_ms() + 250;
            host_log("change detected");
        }

        builder_poll();

        if (rebuild_pending && builder_status() != BUILD_RUNNING && os_tick_ms() >= debounce_until) {
            rebuild_pending = 0;
            host_log("rebuilding app_logic");
            if (!builder_start()) {
                host_log(builder_log());
            }
        }

        if (builder_status() == BUILD_OK) {
            builder_reset();
            if (hot_reload_swap()) {
                memory.reload_count += 1;
                if (hot_reload_api() && hot_reload_api()->reload) {
                    hot_reload_api()->reload(&memory);
                }
                char status[128];
                snprintf(status, sizeof(status), "hot reload #%u ok", memory.reload_count);
                host_log(status);
            } else {
                host_log(hot_reload_error());
            }
        } else if (builder_status() == BUILD_FAILED) {
            builder_reset();
            host_log(builder_log());
        }

        Platform platform;
        window_bind_platform(&window, &platform, g_app_root);
        if (install_job_need() >= 1 && install_job_need() <= 3) {
            window_keep_key_focus(&window);
        }

        int vsync = 0;
        int focused = window_focused(&window);
        if (!window_minimized(&window)) {
            const AppApi *api = hot_reload_api();
            if (api && api->tick) {
                api->tick(&memory, &platform, dt);
            } else {
                platform.clear(APP_RGB(1, 6, 19));
            }
            window_apply_cursor(&window, platform.want_text_cursor);
            vsync = window_present(&window);
        }
        window_end_frame_input(&window);
        if (window_minimized(&window)) {
            os_sleep_ms(50);
        } else if (!vsync || !focused) {
            /* Full 60 Hz only while we are the active window. In the
             * background (or behind the running game) the animated theme
             * does not need to burn a CPU core: 30 fps, 10 fps while D2 runs. */
            double target = 1.0 / 60.0;
            if (!focused) {
                target = install_job_game_state() == 2 ? 1.0 / 10.0 : 1.0 / 30.0;
            }
#ifdef _WIN32
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            double used = (double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart;
#else
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double used = (double)(now.tv_sec - last.tv_sec) +
                (double)(now.tv_nsec - last.tv_nsec) / 1000000000.0;
#endif
            double remain = target - used;
            if (remain > 0.001) {
                os_sleep_ms((unsigned)(remain * 1000.0));
            }
        }
    }

    if (hot_reload_api() && hot_reload_api()->shutdown) {
        hot_reload_api()->shutdown(&memory);
    }

    watcher_shutdown();
    hot_reload_shutdown();
    steam_auth_shutdown();
    self_update_shutdown();
    install_job_shutdown();
    media_shutdown();
    debug_console_shutdown();
    window_destroy(&window);
    return 0;
}
