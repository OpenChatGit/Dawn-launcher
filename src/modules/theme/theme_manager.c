#include "shared/theme.h"
#include "theme_desc.h"
#include "shared/icons.h"
#include "shared/os.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static ThemeInfo g_list[THEME_MAX_COUNT];
static int g_count;
static ThemeDesc g_current;
static int g_has_current;
static char g_root[THEME_PATH_MAX];
static char g_error[512];
static uint64_t g_json_time;
static uint64_t g_emblem_time;
static uint32_t g_last_file_check;
static char g_standard_id[THEME_ID_MAX];
static int g_ready;
static float g_audio_raw;
static float g_audio_level;
static float g_last_time;
static int g_playing;
static int g_armed;
static int g_saw_busy;
static Platform *g_platform;
static char g_click_url[THEME_URL_MAX];
static char g_resolved_url[THEME_URL_MAX];
static char g_video_url[THEME_URL_MAX];
static int g_audio_boot;

static int
url_is_remote_or_abs(const char *url)
{
    if (!url || !url[0]) {
        return 0;
    }
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        return 1;
    }
    if (strncmp(url, "file:", 5) == 0) {
        return 1;
    }
    if (url[0] == '/' || url[0] == '\\') {
        return 1;
    }
    if (((url[0] >= 'A' && url[0] <= 'Z') || (url[0] >= 'a' && url[0] <= 'z')) && url[1] == ':') {
        return 1;
    }
    return 0;
}

static void
resolve_into(char *out, int max, const char *url)
{
    if (!out || max <= 0) {
        return;
    }
    if (!url || !url[0]) {
        out[0] = '\0';
        return;
    }
    if (url_is_remote_or_abs(url)) {
        snprintf(out, (size_t)max, "%s", url);
        return;
    }
    if (g_has_current && g_current.info.path[0]) {
        snprintf(out, (size_t)max, "%s/%s", g_current.info.path, url);
        return;
    }
    snprintf(out, (size_t)max, "%s", url);
}

static const char *
resolve_audio_url(const char *url)
{
    if (!url || !url[0]) {
        return "";
    }
    if (url_is_remote_or_abs(url)) {
        return url;
    }
    resolve_into(g_resolved_url, (int)sizeof(g_resolved_url), url);
    return g_resolved_url;
}

static int
is_video_url(const char *url)
{
    if (!url || !url[0]) {
        return 0;
    }
    char buf[THEME_URL_MAX];
    snprintf(buf, sizeof(buf), "%s", url);
    char *q = strchr(buf, '?');
    if (q) {
        *q = '\0';
    }
    const char *dot = strrchr(buf, '.');
    if (!dot) {
        return 0;
    }
    return os_stricmp(dot, ".mp4") == 0 ||
        os_stricmp(dot, ".m4v") == 0 ||
        os_stricmp(dot, ".webm") == 0 ||
        os_stricmp(dot, ".mov") == 0 ||
        os_stricmp(dot, ".mkv") == 0 ||
        os_stricmp(dot, ".avi") == 0 ||
        os_stricmp(dot, ".wmv") == 0;
}

static void
sync_video_source(Platform *platform)
{
    if (!platform || !platform->video_set_source) {
        return;
    }
    if (platform->video_set_output) {
        platform->video_set_output(platform->width, platform->height);
    }
    char url[THEME_URL_MAX];
    url[0] = '\0';
    int loop = 1;
    if (g_has_current && g_current.background.enabled && g_current.background.url[0]) {
        resolve_into(url, (int)sizeof(url), g_current.background.url);
        loop = g_current.background.loop;
    } else if (g_playing) {
        const char *audio = theme_audio_url();
        if (is_video_url(audio)) {
            snprintf(url, sizeof(url), "%s", audio);
            loop = g_has_current ? g_current.audio.loop : 1;
        }
    }
    if (strcmp(g_video_url, url) == 0) {
        platform->video_set_source(url[0] ? url : NULL, loop);
        return;
    }
    snprintf(g_video_url, sizeof(g_video_url), "%s", url);
    platform->video_set_source(url[0] ? url : NULL, loop);
}

static void
sync_video_gain(Platform *platform)
{
    if (!platform || !platform->video_set_gain || !g_has_current) {
        return;
    }
    int muted = g_current.background.muted;
    float volume = g_current.background.volume;
    if (g_playing) {
        muted = 1;
    }
    platform->video_set_gain(muted, volume);
}

static void
sync_audio_gain(Platform *platform)
{
    if (!platform || !platform->media_set_volume || !g_has_current) {
        return;
    }
    platform->media_set_volume(g_current.audio.volume);
}

const char *
theme_audio_url(void)
{
    if (g_click_url[0]) {
        return resolve_audio_url(g_click_url);
    }
    if (g_has_current && g_current.audio.url[0]) {
        return resolve_audio_url(g_current.audio.url);
    }
    return "";
}

static void
set_error(const char *text)
{
    snprintf(g_error, sizeof(g_error), "%s", text ? text : "");
}

static void
remember_times(const ThemeDesc *desc)
{
    char json_path[THEME_PATH_MAX];
    snprintf(json_path, sizeof(json_path), "%s/theme.json", desc->info.path);
    g_json_time = os_file_mtime(json_path);
    g_emblem_time = 0;
    for (int i = 0; i < desc->layer_count; ++i) {
        if (desc->layers[i].path[0]) {
            uint64_t time = os_file_mtime(desc->layers[i].path);
            if (time > g_emblem_time) {
                g_emblem_time = time;
            }
        }
    }
}

static int
files_changed(const ThemeDesc *desc)
{
    char json_path[THEME_PATH_MAX];
    snprintf(json_path, sizeof(json_path), "%s/theme.json", desc->info.path);
    uint64_t json_time = os_file_mtime(json_path);
    if (json_time && json_time != g_json_time) {
        return 1;
    }
    uint64_t newest = 0;
    for (int i = 0; i < desc->layer_count; ++i) {
        if (desc->layers[i].path[0]) {
            uint64_t emblem_time = os_file_mtime(desc->layers[i].path);
            if (emblem_time > newest) {
                newest = emblem_time;
            }
        }
    }
    if (newest && newest != g_emblem_time) {
        return 1;
    }
    return 0;
}

static int
scan_theme_folder(const char *name, int is_dir, void *user)
{
    (void)user;
    if (!is_dir || g_count >= THEME_MAX_COUNT) {
        return g_count < THEME_MAX_COUNT;
    }
    char folder[THEME_PATH_MAX];
    snprintf(folder, sizeof(folder), "%s/themes/%s", g_root, name);
    ThemeDesc desc;
    char error[256];
    if (!theme_json_load(folder, &desc, error, sizeof(error))) {
        return 1;
    }
    g_list[g_count] = desc.info;
    if (!g_list[g_count].id[0]) {
        snprintf(g_list[g_count].id, sizeof(g_list[g_count].id), "%s", name);
    }
    if (g_list[g_count].standard && !g_standard_id[0]) {
        snprintf(g_standard_id, sizeof(g_standard_id), "%s", g_list[g_count].id);
    }
    g_count += 1;
    return 1;
}

static void
scan_themes(void)
{
    g_count = 0;
    g_standard_id[0] = '\0';
    char folder[THEME_PATH_MAX];
    snprintf(folder, sizeof(folder), "%s/themes", g_root);
    os_list_dir(folder, scan_theme_folder, NULL);
}

int
theme_manager_init(const char *project_root)
{
    memset(g_list, 0, sizeof(g_list));
    g_count = 0;
    g_has_current = 0;
    g_error[0] = '\0';
    snprintf(g_root, sizeof(g_root), "%s", project_root ? project_root : ".");
    icon_set_root(g_root);
    theme_render_init();
    scan_themes();
    g_ready = 1;
    if (g_count == 0) {
        set_error("no themes found in themes/");
        return 0;
    }
    return 1;
}

void
theme_manager_shutdown(void)
{
    theme_render_shutdown();
    g_ready = 0;
    g_has_current = 0;
    g_count = 0;
}

void
theme_manager_refresh(void)
{
    if (!g_ready) {
        return;
    }
    char keep[THEME_ID_MAX];
    snprintf(keep, sizeof(keep), "%s", g_has_current ? g_current.info.id : theme_standard_id());
    scan_themes();
    if (keep[0]) {
        theme_set(keep);
    }
}

int
theme_count(void)
{
    return g_count;
}

int
theme_list(ThemeInfo *out, int max_count)
{
    if (!out || max_count <= 0) {
        return 0;
    }
    int n = g_count < max_count ? g_count : max_count;
    memcpy(out, g_list, sizeof(ThemeInfo) * (size_t)n);
    return n;
}

const ThemeInfo *
theme_get(int index)
{
    if (index < 0 || index >= g_count) {
        return NULL;
    }
    return &g_list[index];
}

const ThemeInfo *
theme_find(const char *id)
{
    if (!id) {
        return NULL;
    }
    for (int i = 0; i < g_count; ++i) {
        if (strcmp(g_list[i].id, id) == 0) {
            return &g_list[i];
        }
    }
    return NULL;
}

const ThemeInfo *
theme_current(void)
{
    return g_has_current ? &g_current.info : NULL;
}

const char *
theme_current_id(void)
{
    return g_has_current ? g_current.info.id : "";
}

const char *
theme_standard_id(void)
{
    if (g_standard_id[0]) {
        return g_standard_id;
    }
    if (g_count > 0) {
        return g_list[0].id;
    }
    return "";
}

const ThemeDesc *
theme_desc_current(void)
{
    return g_has_current ? &g_current : NULL;
}

void
theme_desc_invalidate_cache(void)
{
    theme_render_reset();
}

int
theme_set(const char *id)
{
    if (!g_ready) {
        set_error("theme manager not initialized");
        return 0;
    }
    if (!id || !id[0]) {
        set_error("theme id empty");
        return 0;
    }

    const ThemeInfo *info = theme_find(id);
    char folder[THEME_PATH_MAX];
    if (info) {
        snprintf(folder, sizeof(folder), "%s", info->path);
    } else {
        snprintf(folder, sizeof(folder), "%s/themes/%s", g_root, id);
    }

    ThemeDesc desc;
    if (!theme_json_load(folder, &desc, g_error, sizeof(g_error))) {
        return 0;
    }

    g_current = desc;
    g_has_current = 1;
    g_video_url[0] = '\0';
    g_audio_boot = 0;
    remember_times(&g_current);
    theme_render_reset();
    theme_ui_reset();
    set_error("");
    return 1;
}

void
theme_set_audio(float level)
{
    if (level < 0.0f) {
        level = 0.0f;
    }
    if (level > 1.0f) {
        level = 1.0f;
    }
    g_audio_raw = level;
}

float
theme_audio(void)
{
    return g_audio_level;
}

const ThemeAudio *
theme_audio_config(void)
{
    static ThemeAudio fallback = {0, 1, 1.0f, 0.12f, 0.28f, 0.0f, 0.5f, ""};
    return g_has_current ? &g_current.audio : &fallback;
}

int
theme_playing(void)
{
    return g_playing;
}

void
theme_set_playing(int playing)
{
    g_playing = playing ? 1 : 0;
    if (!playing) {
        g_armed = 0;
        g_click_url[0] = '\0';
    }
    if (!g_platform) {
        return;
    }
    if (playing && g_platform->media_set_url) {
        g_platform->media_set_url(theme_audio_url());
    }
    if (g_platform->media_set_playing) {
        g_platform->media_set_playing(playing);
    }
}

void
theme_play_url(const char *url)
{
    if (url && url[0]) {
        snprintf(g_click_url, sizeof(g_click_url), "%s", url);
    } else {
        g_click_url[0] = '\0';
    }
    theme_set_playing(1);
}

static void
update_audio_envelope(float time)
{
    float dt = time - g_last_time;
    g_last_time = time;
    if (dt < 0.0f || dt > 0.08f) {
        dt = 0.016f;
    }

    const ThemeAudio *audio = theme_audio_config();
    if (g_platform && g_platform->media_level && audio->enabled) {
        g_audio_raw = g_platform->media_level();
    }

    if (g_platform) {
        int busy = g_platform->media_busy ? g_platform->media_busy() : 0;
        int live = g_platform->media_playing ? g_platform->media_playing() : 0;
        if (g_playing) {
            if (busy) {
                g_armed = 0;
                g_saw_busy = 1;
            } else if (live) {
                g_armed = 1;
                g_saw_busy = 0;
            } else if (g_armed && !audio->loop) {
                g_playing = 0;
                g_armed = 0;
                g_saw_busy = 0;
                g_click_url[0] = '\0';
            } else if (g_saw_busy && !audio->loop) {
                g_playing = 0;
                g_saw_busy = 0;
                g_click_url[0] = '\0';
            }
        } else {
            g_armed = 0;
            g_saw_busy = 0;
        }
    }

    float target = 0.0f;
    if (audio->enabled) {
        target = g_audio_raw * audio->sensitivity;
        if (target > 1.0f) {
            target = 1.0f;
        }
        if (target < audio->idle) {
            target = audio->idle;
        }
    }
    float tau = target > g_audio_level ? audio->attack : audio->release;
    if (tau < 0.001f) {
        tau = 0.001f;
    }
    g_audio_level += (target - g_audio_level) * (1.0f - expf(-dt / tau));
    theme_render_set_audio(g_audio_level);
}

int
theme_set_index(int index)
{
    const ThemeInfo *info = theme_get(index);
    if (!info) {
        set_error("theme index out of range");
        return 0;
    }
    return theme_set(info->id);
}

const ThemeChrome *
theme_chrome(void)
{
    static ThemeChrome fallback = {
        "APP",
        0xe6e6e6,
        0xa8aeb6,
        0xffffff,
        0xe81123
    };
    return g_has_current ? &g_current.chrome : &fallback;
}

void
theme_draw(Platform *platform, float time)
{
    if (!platform) {
        return;
    }
    if (!g_ready) {
        theme_manager_init(platform->project_root);
    }
    if (!g_has_current) {
        const char *standard = theme_standard_id();
        if ((!standard[0] || !theme_set(standard)) && g_count > 0) {
            theme_set(g_list[0].id);
        }
    }
    uint32_t now = os_tick_ms();
    if (g_has_current && (now - g_last_file_check) >= 750) {
        g_last_file_check = now;
        if (files_changed(&g_current)) {
            theme_set(g_current.info.id);
        }
    }
    if (!g_has_current) {
        platform->clear(APP_RGB(1, 6, 19));
        return;
    }
    g_platform = platform;
    if (platform->media_set_loop) {
        platform->media_set_loop(g_current.audio.loop);
    }
    sync_audio_gain(platform);
    if (!g_audio_boot && g_current.audio.enabled && g_current.audio.url[0]) {
        g_audio_boot = 1;
        theme_set_playing(1);
    }
    sync_video_gain(platform);
    sync_video_source(platform);
    update_audio_envelope(time);
    theme_render_draw(platform, &g_current, time);
}

const char *
theme_last_error(void)
{
    return g_error;
}
