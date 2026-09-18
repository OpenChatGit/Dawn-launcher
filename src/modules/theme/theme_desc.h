#ifndef THEME_DESC_H
#define THEME_DESC_H

#include "shared/theme.h"

#define THEME_MAX_LAYERS 32
#define THEME_MAX_EMBLEMS 8
#define THEME_MAX_UI 32
#define THEME_UI_ID_MAX 32
#define THEME_UI_ICON_MAX 32
#define THEME_UI_TEXT_MAX 160

typedef enum ThemeUiKind {
    THEME_UI_NONE = 0,
    THEME_UI_BUTTON,
    THEME_UI_LABEL,
    THEME_UI_EMBED
} ThemeUiKind;

typedef enum ThemeUiAction {
    THEME_UI_ACTION_NONE = 0,
    THEME_UI_ACTION_TOGGLE_PLAY,
    THEME_UI_ACTION_PLAY,
    THEME_UI_ACTION_PAUSE
} ThemeUiAction;

typedef struct ThemeUiWidget {
    char id[THEME_UI_ID_MAX];
    ThemeUiKind kind;
    ThemeUiAction action;
    int enabled;
    float x;
    float y;
    float w;
    float h;
    float size;
    float radius;
    float opacity;
    char text[THEME_UI_TEXT_MAX];
    uint32_t color;
    uint32_t hover;
    uint32_t fill;
    uint32_t fill_hover;
    char icon[THEME_UI_ICON_MAX];
    char icon_on[THEME_UI_ICON_MAX];
    char url[THEME_URL_MAX];
} ThemeUiWidget;

typedef enum ThemeLayerKind {
    THEME_LAYER_NONE = 0,
    THEME_LAYER_RADIAL,
    THEME_LAYER_ARC,
    THEME_LAYER_EMBLEM,
    THEME_LAYER_VIGNETTE,
    THEME_LAYER_RING,
    THEME_LAYER_DRIFT
} ThemeLayerKind;

typedef struct ThemeLayer {
    ThemeLayerKind kind;
    int enabled;
    float x;
    float y;
    float w;
    float h;
    float opacity;
    uint32_t inner;
    uint32_t mid;
    uint32_t outer;
    uint32_t glow;
    uint32_t core;
    uint32_t cap_inner;
    uint32_t cap_outer;
    uint32_t color;
    float mid_stop;
    float start_deg;
    float end_deg;
    float radius_ratio;
    float scale;
    float narrow_scale;
    int segments;
    int count;
    float flow_amp;
    float flow_speed;
    float phase;
    int animate;
    int audio;
    int cap;
    float stroke_width;
    float stroke_alpha;
    float blur_inner;
    float blur_outer;
    float core_width;
    float size;
    float view;
    int cache;
    char file[260];
    char path[THEME_PATH_MAX];
} ThemeLayer;

typedef struct ThemeDesc {
    ThemeInfo info;
    uint32_t clear;
    ThemeChrome chrome;
    ThemeAudio audio;
    ThemeBackground background;
    int layer_count;
    ThemeLayer layers[THEME_MAX_LAYERS];
    int ui_count;
    ThemeUiWidget ui[THEME_MAX_UI];
} ThemeDesc;

const ThemeDesc *theme_desc_current(void);
void theme_desc_invalidate_cache(void);

int theme_json_load(const char *folder, ThemeDesc *desc, char *error, int error_max);

#ifdef __cplusplus
extern "C" {
#endif

void theme_ui_reset(void);

void theme_render_init(void);
void theme_render_shutdown(void);
void theme_render_reset(void);
void theme_render_set_audio(float level);
float theme_render_audio(void);
void theme_render_draw(Platform *platform, const ThemeDesc *desc, float time);

#ifdef __cplusplus
}
#endif

#endif
