#ifndef SHARED_THEME_H
#define SHARED_THEME_H

#include "shared/api.h"

#define THEME_MAX_COUNT 32
#define THEME_ID_MAX 64
#define THEME_NAME_MAX 128
#define THEME_PATH_MAX 512
#define THEME_URL_MAX 1024

/*
 * Plug-and-play themes live in <project>/themes/<id>/theme.json.
 * A theme is chrome, layers[], optional background {}, audio {}, and ui[] widgets.
 * Layer types: radial, arc, emblem, vignette, ring, drift.
 * UI types: play, button, label, embed / youtube. Place them anywhere.
 * background.url accepts a local or https mp4/webm/mov and loops by default.
 * audio.url accepts a local file, https audio, YouTube, SoundCloud, or similar.
 * audio.loop defaults to true so the track restarts at the end.
 * Themes decide the layout. The host does not force a player chrome.
 * Drop a new folder and call theme_manager_refresh() + theme_set(id).
 */

typedef struct ThemeAudio {
    int enabled;
    int loop;
    float sensitivity;
    float attack;
    float release;
    float idle;
    float volume;
    char url[THEME_URL_MAX];
} ThemeAudio;

typedef struct ThemeBackground {
    int enabled;
    int loop;
    int muted;
    float opacity;
    float volume;
    char url[THEME_URL_MAX];
} ThemeBackground;

typedef struct ThemeInfo {
    char id[THEME_ID_MAX];
    char name[THEME_NAME_MAX];
    char path[THEME_PATH_MAX];
    int standard;
} ThemeInfo;

typedef struct ThemeChrome {
    char title[64];
    uint32_t title_color;
    uint32_t muted;
    uint32_t hover;
    uint32_t close;
} ThemeChrome;

int theme_manager_init(const char *project_root);
void theme_manager_shutdown(void);
void theme_manager_refresh(void);

int theme_count(void);
int theme_list(ThemeInfo *out, int max_count);
const ThemeInfo *theme_get(int index);
const ThemeInfo *theme_find(const char *id);
const ThemeInfo *theme_current(void);
const char *theme_current_id(void);
const char *theme_standard_id(void);

int theme_set(const char *id);
int theme_set_index(int index);

/* 0..1 raw analyser sample. Envelope comes from theme.json audio {}. */
void theme_set_audio(float level);
float theme_audio(void);
const ThemeAudio *theme_audio_config(void);

int theme_playing(void);
void theme_set_playing(int playing);
void theme_play_url(const char *url);
const char *theme_audio_url(void);

void theme_draw(Platform *platform, float time);
void theme_ui_tick(Platform *platform, float dt);
const ThemeChrome *theme_chrome(void);
const char *theme_last_error(void);

#endif
