#include "media.h"
#include "video_bg.h"
#include "ytstream.h"

void media_init(void *hwnd, const char *project_root)
{
    (void)hwnd;
    ytstream_init(project_root);
    video_bg_init();
}

void media_shutdown(void)
{
    ytstream_shutdown();
    video_bg_shutdown();
}

void media_poll(void)
{
    ytstream_poll();
    video_bg_tick();
}

void media_set_url(const char *url)
{
    if (ytstream_handles_url(url)) {
        ytstream_play(url);
    }
}

void media_set_playing(int playing)
{
    if (!playing) {
        ytstream_stop();
    }
}

void media_set_loop(int loop)
{
    ytstream_set_loop(loop);
}

void media_set_volume(float volume)
{
    ytstream_set_volume(volume);
}

int media_is_playing(void)
{
    return ytstream_playing();
}

int media_is_busy(void)
{
    return ytstream_busy();
}

float media_level(void)
{
    return ytstream_level();
}

void media_embed_set_view(int x, int y, int w, int h, int visible)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)visible;
}
