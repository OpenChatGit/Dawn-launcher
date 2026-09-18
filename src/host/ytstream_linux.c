#include "ytstream.h"

void ytstream_init(const char *project_root)
{
    (void)project_root;
}
void ytstream_shutdown(void) {}
void ytstream_poll(void) {}
void ytstream_set_loop(int loop)
{
    (void)loop;
}
void ytstream_set_volume(float volume)
{
    (void)volume;
}
void ytstream_play(const char *url)
{
    (void)url;
}
void ytstream_stop(void) {}
int ytstream_playing(void)
{
    return 0;
}
int ytstream_busy(void)
{
    return 0;
}
float ytstream_level(void)
{
    return 0.0f;
}
int ytstream_handles_url(const char *url)
{
    (void)url;
    return 0;
}
