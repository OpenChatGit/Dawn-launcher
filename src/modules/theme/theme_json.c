#include "theme_desc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *
skip_ws(const char *s)
{
    while (s && *s && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

static const char *
find_root_key(const char *json, const char *end, const char *key)
{
    const char *s = skip_ws(json);
    if (!s || s >= end || *s != '{') {
        return NULL;
    }
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    size_t n = strlen(needle);
    int depth = 0;
    int in_str = 0;
    for (; s < end && *s; ++s) {
        if (in_str) {
            if (*s == '\\' && s[1]) {
                s++;
                continue;
            }
            if (*s == '"') {
                in_str = 0;
            }
            continue;
        }
        if (*s == '"') {
            if (depth == 1 && (size_t)(end - s) >= n && strncmp(s, needle, n) == 0) {
                const char *after = skip_ws(s + n);
                if (*after == ':') {
                    return skip_ws(after + 1);
                }
            }
            in_str = 1;
            continue;
        }
        if (*s == '{' || *s == '[') {
            depth++;
        } else if (*s == '}' || *s == ']') {
            depth--;
            if (depth <= 0) {
                break;
            }
        }
    }
    return NULL;
}

static const char *
find_key(const char *json, const char *end, const char *key)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = json;
    size_t n = strlen(needle);
    while (p && p < end) {
        const char *hit = strstr(p, needle);
        if (!hit || hit >= end) {
            return NULL;
        }
        const char *after = skip_ws(hit + n);
        if (*after == ':') {
            return skip_ws(after + 1);
        }
        p = hit + n;
    }
    return NULL;
}

static const char *
object_end(const char *start)
{
    if (!start || *start != '{') {
        return start;
    }
    int depth = 0;
    int in_str = 0;
    const char *s = start;
    for (; *s; ++s) {
        if (in_str) {
            if (*s == '\\' && s[1]) {
                s++;
                continue;
            }
            if (*s == '"') {
                in_str = 0;
            }
            continue;
        }
        if (*s == '"') {
            in_str = 1;
            continue;
        }
        if (*s == '{') {
            depth++;
        } else if (*s == '}') {
            depth--;
            if (depth == 0) {
                return s + 1;
            }
        }
    }
    return s;
}

static int
read_string(const char *value, char *out, int max)
{
    value = skip_ws(value);
    if (*value != '"') {
        return 0;
    }
    value++;
    int i = 0;
    while (*value && *value != '"' && i < max - 1) {
        if (*value == '\\' && value[1]) {
            value++;
        }
        out[i++] = *value++;
    }
    out[i] = '\0';
    return i > 0;
}

static int
read_number(const char *value, float *out)
{
    value = skip_ws(value);
    if (!*value) {
        return 0;
    }
    char *end = NULL;
    float n = strtof(value, &end);
    if (end == value) {
        return 0;
    }
    *out = n;
    return 1;
}

static int
read_bool(const char *value, int *out)
{
    value = skip_ws(value);
    if (strncmp(value, "true", 4) == 0) {
        *out = 1;
        return 1;
    }
    if (strncmp(value, "false", 5) == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

static int
parse_hex_color(const char *text, uint32_t *rgb)
{
    if (!text || text[0] != '#') {
        return 0;
    }
    size_t n = strlen(text + 1);
    unsigned int value = 0;
    if (n == 3) {
        unsigned int r = 0, g = 0, b = 0;
        if (sscanf(text + 1, "%1x%1x%1x", &r, &g, &b) != 3) {
            return 0;
        }
        *rgb = ((r * 17u) << 16) | ((g * 17u) << 8) | (b * 17u);
        return 1;
    }
    if (n >= 8) {
        if (sscanf(text + 1, "%08x", &value) != 1) {
            return 0;
        }
        *rgb = (value >> 8) & 0xffffffu;
        return 1;
    }
    if (n >= 6) {
        if (sscanf(text + 1, "%06x", &value) != 1) {
            return 0;
        }
        *rgb = value & 0xffffffu;
        return 1;
    }
    return 0;
}

static const char *
array_end(const char *start)
{
    if (!start || *start != '[') {
        return start;
    }
    int depth = 0;
    int in_str = 0;
    const char *s = start;
    for (; *s; ++s) {
        if (in_str) {
            if (*s == '\\' && s[1]) {
                s++;
                continue;
            }
            if (*s == '"') {
                in_str = 0;
            }
            continue;
        }
        if (*s == '"') {
            in_str = 1;
            continue;
        }
        if (*s == '[' || *s == '{') {
            depth++;
        } else if (*s == ']' || *s == '}') {
            depth--;
            if (depth == 0) {
                return s + 1;
            }
        }
    }
    return s;
}

static const char *
next_object(const char **cursor, const char *end)
{
    const char *p = skip_ws(*cursor);
    while (p < end && *p && *p != '{' && *p != ']') {
        p++;
    }
    if (p >= end || *p != '{') {
        return NULL;
    }
    const char *obj = p;
    *cursor = object_end(p);
    return obj;
}

static int
key_string(const char *json, const char *end, const char *key, char *out, int max)
{
    const char *v = find_key(json, end, key);
    return v ? read_string(v, out, max) : 0;
}

static int
key_number(const char *json, const char *end, const char *key, float *out)
{
    const char *v = find_key(json, end, key);
    return v ? read_number(v, out) : 0;
}

static int
key_bool(const char *json, const char *end, const char *key, int *out)
{
    const char *v = find_key(json, end, key);
    return v ? read_bool(v, out) : 0;
}

static int
key_color(const char *json, const char *end, const char *key, uint32_t *rgb)
{
    char text[32];
    if (!key_string(json, end, key, text, sizeof(text))) {
        return 0;
    }
    return parse_hex_color(text, rgb);
}

static void
layer_defaults(ThemeLayer *layer, ThemeLayerKind kind)
{
    memset(layer, 0, sizeof(*layer));
    layer->kind = kind;
    layer->enabled = 1;
    layer->x = 0.5f;
    layer->y = 0.5f;
    layer->w = 0.95f;
    layer->h = 0.85f;
    layer->opacity = 1.0f;
    layer->inner = 0x051a44;
    layer->mid = 0x03102d;
    layer->outer = 0x010613;
    layer->glow = 0x0859f2;
    layer->core = 0x93c5fd;
    layer->cap_inner = 0x000206;
    layer->cap_outer = 0x000814;
    layer->color = 0x0859f2;
    layer->mid_stop = 0.42f;
    layer->start_deg = 180.0f;
    layer->end_deg = 90.0f;
    layer->radius_ratio = 0.37f;
    layer->scale = 1.32f;
    layer->narrow_scale = 1.55f;
    layer->segments = 24;
    layer->count = 12;
    layer->flow_amp = 16.0f;
    layer->flow_speed = 0.3f;
    layer->phase = 0.0f;
    layer->animate = 1;
    layer->audio = 0;
    layer->cap = 1;
    layer->stroke_width = 6.0f;
    layer->stroke_alpha = 0.8f;
    layer->blur_inner = 5.0f;
    layer->blur_outer = 13.0f;
    layer->core_width = 1.8f;
    layer->size = 230.0f;
    layer->view = 1028.0f;
    layer->cache = 512;
    snprintf(layer->file, sizeof(layer->file), "emblem.svg");
    if (kind == THEME_LAYER_RING) {
        layer->glow = 0x6e6e6e;
        layer->core = 0x7a7a7a;
        layer->color = 0x7a7a7a;
        layer->stroke_width = 2.2f;
        layer->stroke_alpha = 0.45f;
        layer->blur_inner = 0.0f;
        layer->blur_outer = 0.0f;
        layer->core_width = 2.0f;
        layer->size = 210.0f;
        layer->scale = 0.16f;
        layer->flow_speed = 0.4f;
        layer->animate = 0;
        layer->audio = 1;
        layer->opacity = 0.88f;
    }
}

static ThemeLayer *
add_layer(ThemeDesc *desc, ThemeLayerKind kind)
{
    if (desc->layer_count >= THEME_MAX_LAYERS) {
        return NULL;
    }
    ThemeLayer *layer = &desc->layers[desc->layer_count++];
    layer_defaults(layer, kind);
    return layer;
}

static void
resolve_layer_path(const char *folder, ThemeLayer *layer)
{
    if (!layer->file[0]) {
        layer->path[0] = '\0';
        return;
    }
    snprintf(layer->path, sizeof(layer->path), "%s/%s", folder, layer->file);
}

static ThemeLayerKind
kind_from_name(const char *name)
{
    if (strcmp(name, "radial") == 0) {
        return THEME_LAYER_RADIAL;
    }
    if (strcmp(name, "arc") == 0) {
        return THEME_LAYER_ARC;
    }
    if (strcmp(name, "emblem") == 0) {
        return THEME_LAYER_EMBLEM;
    }
    if (strcmp(name, "vignette") == 0) {
        return THEME_LAYER_VIGNETTE;
    }
    if (strcmp(name, "ring") == 0) {
        return THEME_LAYER_RING;
    }
    if (strcmp(name, "drift") == 0) {
        return THEME_LAYER_DRIFT;
    }
    return THEME_LAYER_NONE;
}

static void
parse_layer_fields(const char *json, const char *end, const char *folder, ThemeLayer *layer)
{
    key_bool(json, end, "enabled", &layer->enabled);
    key_number(json, end, "x", &layer->x);
    key_number(json, end, "y", &layer->y);
    key_number(json, end, "width", &layer->w);
    key_number(json, end, "height", &layer->h);
    key_number(json, end, "opacity", &layer->opacity);
    key_color(json, end, "inner", &layer->inner);
    key_color(json, end, "mid", &layer->mid);
    key_color(json, end, "outer", &layer->outer);
    key_color(json, end, "glow", &layer->glow);
    key_color(json, end, "core", &layer->core);
    key_color(json, end, "capInner", &layer->cap_inner);
    key_color(json, end, "capOuter", &layer->cap_outer);
    key_color(json, end, "color", &layer->color);
    key_number(json, end, "midStop", &layer->mid_stop);
    key_number(json, end, "start", &layer->start_deg);
    key_number(json, end, "end", &layer->end_deg);
    key_number(json, end, "radiusRatio", &layer->radius_ratio);
    key_number(json, end, "scale", &layer->scale);
    key_number(json, end, "narrowScale", &layer->narrow_scale);
    float segs = (float)layer->segments;
    if (key_number(json, end, "segments", &segs)) {
        layer->segments = (int)segs;
    }
    float count = (float)layer->count;
    if (key_number(json, end, "count", &count)) {
        layer->count = (int)count;
    }
    if (layer->count < 1) {
        layer->count = 1;
    }
    if (layer->count > 24) {
        layer->count = 24;
    }
    key_number(json, end, "flowAmp", &layer->flow_amp);
    key_number(json, end, "flowSpeed", &layer->flow_speed);
    key_number(json, end, "phase", &layer->phase);
    key_bool(json, end, "animate", &layer->animate);
    key_bool(json, end, "audio", &layer->audio);
    key_bool(json, end, "cap", &layer->cap);
    key_number(json, end, "strokeWidth", &layer->stroke_width);
    key_number(json, end, "strokeAlpha", &layer->stroke_alpha);
    key_number(json, end, "blurInner", &layer->blur_inner);
    key_number(json, end, "blurOuter", &layer->blur_outer);
    key_number(json, end, "coreWidth", &layer->core_width);
    key_number(json, end, "size", &layer->size);
    key_number(json, end, "viewBox", &layer->view);
    float cache = (float)layer->cache;
    if (key_number(json, end, "cacheSize", &cache)) {
        layer->cache = (int)cache;
    }
    key_string(json, end, "file", layer->file, sizeof(layer->file));
    if (layer->segments < 8) {
        layer->segments = 8;
    }
    if (layer->segments > 96) {
        layer->segments = 96;
    }
    if (layer->cache < 128) {
        layer->cache = 128;
    }
    if (layer->cache > 1024) {
        layer->cache = 1024;
    }
    if (layer->opacity < 0.0f) {
        layer->opacity = 0.0f;
    }
    if (layer->opacity > 1.0f) {
        layer->opacity = 1.0f;
    }
    resolve_layer_path(folder, layer);
}

static void
theme_defaults(ThemeDesc *desc)
{
    memset(desc, 0, sizeof(*desc));
    desc->clear = 0x010613;
    snprintf(desc->chrome.title, sizeof(desc->chrome.title), "APP");
    desc->chrome.title_color = 0xe6e6e6;
    desc->chrome.muted = 0xa8aeb6;
    desc->chrome.hover = 0xffffff;
    desc->chrome.close = 0xe81123;
    desc->audio.enabled = 0;
    desc->audio.loop = 1;
    desc->audio.sensitivity = 1.0f;
    desc->audio.attack = 0.12f;
    desc->audio.release = 0.28f;
    desc->audio.idle = 0.0f;
    desc->audio.volume = 0.5f;
    desc->background.enabled = 0;
    desc->background.loop = 1;
    desc->background.muted = 1;
    desc->background.opacity = 1.0f;
    desc->background.volume = 1.0f;
}

static void
ui_defaults(ThemeUiWidget *widget)
{
    memset(widget, 0, sizeof(*widget));
    widget->enabled = 1;
    widget->x = 0.5f;
    widget->y = 0.8f;
    widget->w = 0.0f;
    widget->h = 0.0f;
    widget->size = 48.0f;
    widget->radius = -1.0f;
    widget->opacity = 1.0f;
    widget->color = 0x8a8a8a;
    widget->hover = 0xf2f2f2;
    widget->fill = 0x2a2a2a;
    widget->fill_hover = 0x363636;
    snprintf(widget->icon, sizeof(widget->icon), "play");
}

static ThemeUiAction
action_from_name(const char *name)
{
    if (strcmp(name, "togglePlay") == 0) {
        return THEME_UI_ACTION_TOGGLE_PLAY;
    }
    if (strcmp(name, "play") == 0) {
        return THEME_UI_ACTION_PLAY;
    }
    if (strcmp(name, "pause") == 0) {
        return THEME_UI_ACTION_PAUSE;
    }
    return THEME_UI_ACTION_NONE;
}

static void
parse_audio_fields(const char *json, const char *end, ThemeAudio *audio)
{
    key_bool(json, end, "enabled", &audio->enabled);
    key_number(json, end, "sensitivity", &audio->sensitivity);
    key_number(json, end, "attack", &audio->attack);
    key_number(json, end, "release", &audio->release);
    key_number(json, end, "idle", &audio->idle);
    key_bool(json, end, "loop", &audio->loop);
    key_number(json, end, "volume", &audio->volume);
    if (!key_string(json, end, "url", audio->url, sizeof(audio->url))) {
        key_string(json, end, "source", audio->url, sizeof(audio->url));
    }
    if (audio->sensitivity < 0.0f) {
        audio->sensitivity = 0.0f;
    }
    if (audio->sensitivity > 8.0f) {
        audio->sensitivity = 8.0f;
    }
    if (audio->attack < 0.001f) {
        audio->attack = 0.001f;
    }
    if (audio->release < 0.001f) {
        audio->release = 0.001f;
    }
    if (audio->idle < 0.0f) {
        audio->idle = 0.0f;
    }
    if (audio->idle > 1.0f) {
        audio->idle = 1.0f;
    }
    if (audio->volume < 0.0f) {
        audio->volume = 0.0f;
    }
    if (audio->volume > 1.0f) {
        audio->volume = 1.0f;
    }
}

static void
parse_ui_fields(const char *json, const char *end, ThemeUiWidget *widget)
{
    char type[32];
    char action[32];
    type[0] = '\0';
    action[0] = '\0';
    key_string(json, end, "type", type, sizeof(type));
    key_string(json, end, "id", widget->id, sizeof(widget->id));
    key_bool(json, end, "enabled", &widget->enabled);
    key_number(json, end, "x", &widget->x);
    key_number(json, end, "y", &widget->y);
    key_number(json, end, "width", &widget->w);
    key_number(json, end, "height", &widget->h);
    key_number(json, end, "size", &widget->size);
    key_number(json, end, "radius", &widget->radius);
    key_string(json, end, "text", widget->text, sizeof(widget->text));
    key_number(json, end, "opacity", &widget->opacity);
    key_color(json, end, "color", &widget->color);
    key_color(json, end, "hover", &widget->hover);
    key_color(json, end, "fill", &widget->fill);
    key_color(json, end, "fillHover", &widget->fill_hover);
    key_string(json, end, "icon", widget->icon, sizeof(widget->icon));
    key_string(json, end, "iconOn", widget->icon_on, sizeof(widget->icon_on));
    key_string(json, end, "url", widget->url, sizeof(widget->url));
    key_string(json, end, "action", action, sizeof(action));

    if (strcmp(type, "play") == 0) {
        widget->kind = THEME_UI_BUTTON;
        widget->action = THEME_UI_ACTION_TOGGLE_PLAY;
        if (!widget->id[0]) {
            snprintf(widget->id, sizeof(widget->id), "play");
        }
        if (!widget->icon[0] || strcmp(widget->icon, "play") == 0) {
            snprintf(widget->icon, sizeof(widget->icon), "play");
        }
        if (!widget->icon_on[0]) {
            snprintf(widget->icon_on, sizeof(widget->icon_on), "pause");
        }
        if (!widget->text[0]) {
            snprintf(widget->text, sizeof(widget->text), "PLAY");
        }
    } else if (strcmp(type, "button") == 0) {
        widget->kind = THEME_UI_BUTTON;
    } else if (strcmp(type, "label") == 0 || strcmp(type, "text") == 0) {
        widget->kind = THEME_UI_LABEL;
    } else if (strcmp(type, "embed") == 0 || strcmp(type, "youtube") == 0 || strcmp(type, "webview") == 0) {
        widget->kind = THEME_UI_EMBED;
        if (widget->w <= 0.0f) {
            widget->w = 0.22f;
        }
        if (widget->h <= 0.0f) {
            widget->h = 0.22f;
        }
    } else {
        widget->kind = THEME_UI_NONE;
        return;
    }

    if (action[0]) {
        widget->action = action_from_name(action);
    }
    if (widget->size < 1.0f) {
        widget->size = 1.0f;
    }
    if (widget->opacity < 0.0f) {
        widget->opacity = 0.0f;
    }
    if (widget->opacity > 1.0f) {
        widget->opacity = 1.0f;
    }
    if (!widget->id[0]) {
        snprintf(widget->id, sizeof(widget->id), "ui%d", 0);
    }
}

int
theme_json_load(const char *folder, ThemeDesc *desc, char *error, int error_max)
{
    theme_defaults(desc);
    snprintf(desc->info.path, sizeof(desc->info.path), "%s", folder);

    char path[THEME_PATH_MAX];
    snprintf(path, sizeof(path), "%s/theme.json", folder);
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (error) {
            snprintf(error, error_max, "missing theme.json in %s", folder);
        }
        return 0;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0 || size > 256000) {
        fclose(file);
        if (error) {
            snprintf(error, error_max, "theme.json too large");
        }
        return 0;
    }

    char *json = (char *)malloc((size_t)size + 1);
    if (!json) {
        fclose(file);
        return 0;
    }
    size_t n = fread(json, 1, (size_t)size, file);
    fclose(file);
    json[n] = '\0';

    const char *end = json + n;
    char text[THEME_NAME_MAX];
    const char *root_id = find_root_key(json, end, "id");
    if (root_id && read_string(root_id, text, sizeof(text))) {
        snprintf(desc->info.id, sizeof(desc->info.id), "%s", text);
    }
    const char *root_name = find_root_key(json, end, "name");
    if (root_name && read_string(root_name, text, sizeof(text))) {
        snprintf(desc->info.name, sizeof(desc->info.name), "%s", text);
    }
    const char *root_standard = find_root_key(json, end, "standard");
    if (root_standard) {
        read_bool(root_standard, &desc->info.standard);
    }
    const char *root_clear = find_root_key(json, end, "clear");
    if (root_clear) {
        char color[32];
        if (read_string(root_clear, color, sizeof(color))) {
            parse_hex_color(color, &desc->clear);
        }
    }

    const char *chrome = find_root_key(json, end, "chrome");
    if (chrome && *chrome == '{') {
        const char *ce = object_end(chrome);
        key_string(chrome, ce, "title", desc->chrome.title, sizeof(desc->chrome.title));
        key_color(chrome, ce, "titleColor", &desc->chrome.title_color);
        key_color(chrome, ce, "muted", &desc->chrome.muted);
        key_color(chrome, ce, "hover", &desc->chrome.hover);
        key_color(chrome, ce, "close", &desc->chrome.close);
    }

    const char *audio = find_root_key(json, end, "audio");
    if (audio && *audio == '{') {
        parse_audio_fields(audio, object_end(audio), &desc->audio);
    }

    const char *background = find_root_key(json, end, "background");
    if (!background) {
        background = find_root_key(json, end, "video");
    }
    if (background) {
        if (*background == '{') {
            const char *be = object_end(background);
            desc->background.enabled = 1;
            key_bool(background, be, "enabled", &desc->background.enabled);
            key_bool(background, be, "loop", &desc->background.loop);
            key_bool(background, be, "muted", &desc->background.muted);
            key_number(background, be, "opacity", &desc->background.opacity);
            key_number(background, be, "volume", &desc->background.volume);
            if (!key_string(background, be, "url", desc->background.url, sizeof(desc->background.url))) {
                key_string(background, be, "file", desc->background.url, sizeof(desc->background.url));
            }
            if (desc->background.opacity < 0.0f) {
                desc->background.opacity = 0.0f;
            }
            if (desc->background.opacity > 1.0f) {
                desc->background.opacity = 1.0f;
            }
            if (desc->background.volume < 0.0f) {
                desc->background.volume = 0.0f;
            }
            if (desc->background.volume > 1.0f) {
                desc->background.volume = 1.0f;
            }
        } else if (read_string(background, desc->background.url, sizeof(desc->background.url))) {
            desc->background.enabled = 1;
        }
        if (!desc->background.url[0]) {
            desc->background.enabled = 0;
        }
    }

    const char *ui = find_root_key(json, end, "ui");
    if (ui && *ui == '[') {
        const char *ue = array_end(ui);
        const char *cursor = ui + 1;
        while (desc->ui_count < THEME_MAX_UI) {
            const char *obj = next_object(&cursor, ue);
            if (!obj) {
                break;
            }
            ThemeUiWidget *widget = &desc->ui[desc->ui_count];
            ui_defaults(widget);
            parse_ui_fields(obj, object_end(obj), widget);
            if (widget->kind == THEME_UI_NONE) {
                continue;
            }
            if (!widget->id[0] || strcmp(widget->id, "ui0") == 0) {
                snprintf(widget->id, sizeof(widget->id), "ui%d", desc->ui_count);
            }
            desc->ui_count++;
        }
    }

    const char *layers = find_root_key(json, end, "layers");
    if (layers && *layers == '[') {
        const char *le = array_end(layers);
        const char *cursor = layers + 1;
        while (desc->layer_count < THEME_MAX_LAYERS) {
            const char *obj = next_object(&cursor, le);
            if (!obj) {
                break;
            }
            const char *oe = object_end(obj);
            char type[32];
            type[0] = '\0';
            key_string(obj, oe, "type", type, sizeof(type));
            ThemeLayerKind kind = kind_from_name(type);
            if (kind == THEME_LAYER_NONE) {
                continue;
            }
            ThemeLayer *layer = add_layer(desc, kind);
            if (!layer) {
                break;
            }
            parse_layer_fields(obj, oe, folder, layer);
        }
    }

    if (desc->layer_count == 0) {
        const char *radial = find_key(json, end, "radial");
        if (radial && *radial == '{') {
            ThemeLayer *layer = add_layer(desc, THEME_LAYER_RADIAL);
            if (layer) {
                parse_layer_fields(radial, object_end(radial), folder, layer);
            }
        }
        const char *arcs = find_key(json, end, "arcs");
        if (arcs && *arcs == '{') {
            const char *ae = object_end(arcs);
            int enabled = 1;
            key_bool(arcs, ae, "enabled", &enabled);
            if (enabled) {
                ThemeLayer *tr = add_layer(desc, THEME_LAYER_ARC);
                ThemeLayer *bl = add_layer(desc, THEME_LAYER_ARC);
                if (tr) {
                    parse_layer_fields(arcs, ae, folder, tr);
                    tr->x = 1.0f;
                    tr->y = 0.0f;
                    tr->start_deg = 180.0f;
                    tr->end_deg = 90.0f;
                    tr->phase = 0.0f;
                }
                if (bl) {
                    parse_layer_fields(arcs, ae, folder, bl);
                    bl->x = 0.0f;
                    bl->y = 1.0f;
                    bl->start_deg = -90.0f;
                    bl->end_deg = 0.0f;
                    bl->phase = 2.3f;
                }
            }
        }
        const char *emblem = find_key(json, end, "emblem");
        if (emblem && *emblem == '{') {
            ThemeLayer *layer = add_layer(desc, THEME_LAYER_EMBLEM);
            if (layer) {
                parse_layer_fields(emblem, object_end(emblem), folder, layer);
                layer->x = 0.5f;
                layer->y = 0.5f;
            }
        }
    }

    if (!desc->info.id[0]) {
        const char *slash = strrchr(folder, '/');
        const char *bslash = strrchr(folder, '\\');
        const char *base = folder;
        if (slash && slash + 1 > base) {
            base = slash + 1;
        }
        if (bslash && bslash + 1 > base) {
            base = bslash + 1;
        }
        snprintf(desc->info.id, sizeof(desc->info.id), "%s", base);
    }
    if (!desc->info.name[0]) {
        snprintf(desc->info.name, sizeof(desc->info.name), "%s", desc->info.id);
    }

    if (!desc->chrome.title[0]) {
        snprintf(desc->chrome.title, sizeof(desc->chrome.title), "%s", desc->info.name);
    }

    free(json);
    if (error) {
        error[0] = '\0';
    }
    return 1;
}
