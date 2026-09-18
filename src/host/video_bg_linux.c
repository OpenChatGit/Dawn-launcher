#include "video_bg.h"

void video_bg_init(void) {}
void video_bg_shutdown(void) {}
void video_bg_tick(void) {}
void video_bg_set_source(const char *url, int loop)
{
    (void)url;
    (void)loop;
}
void video_bg_set_gain(int muted, float volume)
{
    (void)muted;
    (void)volume;
}
void video_bg_set_output(int w, int h)
{
    (void)w;
    (void)h;
}
void *video_bg_shared_handle(void)
{
    return 0;
}
int video_bg_frame_size(int *w, int *h)
{
    if (w) {
        *w = 0;
    }
    if (h) {
        *h = 0;
    }
    return 0;
}
int video_bg_lock_frame(const unsigned char **bgra, int *w, int *h, int *stride)
{
    (void)bgra;
    (void)w;
    (void)h;
    (void)stride;
    return 0;
}
void video_bg_unlock_frame(void) {}
unsigned video_bg_generation(void)
{
    return 0;
}
