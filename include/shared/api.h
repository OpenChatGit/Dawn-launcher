#ifndef SHARED_API_H
#define SHARED_API_H

#include "shared/types.h"

#define APP_API_VERSION 25
#ifndef INSTALL_PART_DEPOTS
#define INSTALL_PART_DEPOTS 1
#define INSTALL_PART_DAWN 2
#define INSTALL_PART_SUNRISE 4
#endif
#define APP_STATE_BYTES APP_MEGABYTES(4)

#define PLATFORM_KEY_NONE 0
#define PLATFORM_KEY_BACKSPACE 1
#define PLATFORM_KEY_TAB 2
#define PLATFORM_KEY_ENTER 3
#define PLATFORM_KEY_ESCAPE 4

#ifdef _WIN32
#ifdef APP_EXPORTS
#define APP_EXPORT __declspec(dllexport)
#else
#define APP_EXPORT
#endif
#else
#define APP_EXPORT __attribute__((visibility("default")))
#endif

typedef struct Platform {
    int width;
    int height;
    int mouse_x;
    int mouse_y;
    int mouse_down;
    int mouse_pressed;
    int mouse_released;
    int mouse_wheel;
    char text[128];
    int text_len;
    int key;
    int want_text_cursor;
    const char *project_root;
    void *hdc;
    float dpi_scale;
    float corner_radius;
    uint32_t *pixels;
    void (*clear)(uint32_t color);
    void (*fill_rect)(int x, int y, int w, int h, uint32_t color);
    void (*fill_rect_alpha)(int x, int y, int w, int h, uint32_t color, int alpha);
    void (*fill_circle)(int cx, int cy, float radius, uint32_t color, int alpha);
    void (*fill_poly)(const int *xy, int points, uint32_t color);
    void (*fill_polys)(const int *xy, const int *counts, int poly_count, uint32_t color);
    void (*stroke_poly)(const float *xy, int points, uint32_t color, float width, int alpha);
    void (*draw_line)(int x0, int y0, int x1, int y1, uint32_t color, int width);
    void (*draw_text)(int x, int y, const char *text, uint32_t color);
    void (*draw_label)(int x, int y, const char *text, uint32_t color, int px, int weight, int tracking);
    void (*measure_label)(const char *text, int px, int weight, int *w, int *h, int *ascent);
    void (*minimize)(void);
    void (*close)(void);
    void (*drag)(void);
    void (*log)(const char *msg);
    void (*media_set_url)(const char *url);
    void (*media_set_playing)(int playing);
    void (*media_set_loop)(int loop);
    void (*media_set_volume)(float volume);
    int (*media_playing)(void);
    int (*media_busy)(void);
    float (*media_level)(void);
    void (*video_set_source)(const char *url, int loop);
    void (*video_set_gain)(int muted, float volume);
    int (*video_lock_frame)(const unsigned char **bgra, int *w, int *h, int *stride);
    void (*video_unlock_frame)(void);
    unsigned (*video_frame_gen)(void);
    void (*video_set_output)(int w, int h);
    void *(*video_shared_handle)(void);
    int (*video_frame_size)(int *w, int *h);
    void (*embed_set_view)(int x, int y, int w, int h, int visible);
    void (*install_set_dir)(const char *dir);
    const char *(*install_dir)(void);
    int (*pick_folder)(char *out, int max);
    void (*install_set_user)(const char *username);
    void (*install_submit_secret)(const char *text);
    int (*install_start)(void);
    void (*install_cancel)(void);
    int (*install_busy)(void);
    int (*install_need)(void);
    float (*install_progress)(void);
    const char *(*install_status)(void);
    int (*install_ready)(void);
    int (*install_launch)(void);
    int (*install_uninstall)(void);
    int (*install_parts)(void);
    int (*install_uninstall_part)(int part);
    int (*install_verify)(void);
    int (*game_state)(void);
    void (*game_stop)(void);
    void (*install_set_language)(const char *steam);
    const char *(*install_language)(void);
    const char *(*install_language_label)(void);
    int (*steam_sign_in)(void);
    void (*steam_sign_out)(void);
    void (*steam_cancel)(void);
    int (*steam_signed_in)(void);
    int (*steam_busy)(void);
    const char *(*steam_persona)(void);
    const char *(*steam_id)(void);
    const char *(*steam_avatar_path)(void);
    const char *(*steam_status)(void);
    int (*steam_owns_d2)(void);
    int (*steam_owns_forsaken)(void);
    int (*steam_owns_shadowkeep)(void);
} Platform;

typedef struct AppMemory {
    uint32_t api_version;
    int initialized;
    uint32_t reload_count;
    char last_error[512];
    uint8_t data[APP_STATE_BYTES];
} AppMemory;

typedef struct AppApi {
    uint32_t version;
    void (*init)(AppMemory *memory);
    void (*reload)(AppMemory *memory);
    void (*tick)(AppMemory *memory, Platform *platform, float dt);
    void (*shutdown)(AppMemory *memory);
} AppApi;

typedef const AppApi *(*app_get_api_fn)(void);

#ifdef APP_EXPORTS
APP_EXPORT const AppApi *app_get_api(void);
#endif

#endif
