#ifndef HOST_YTSTREAM_H
#define HOST_YTSTREAM_H

#ifdef __cplusplus
extern "C" {
#endif

void ytstream_init(const char *project_root);
void ytstream_shutdown(void);
void ytstream_poll(void);
void ytstream_set_loop(int loop);
void ytstream_set_volume(float volume);
void ytstream_play(const char *url);
void ytstream_stop(void);
int ytstream_playing(void);
int ytstream_busy(void);
float ytstream_level(void);
int ytstream_handles_url(const char *url);

#ifdef __cplusplus
}
#endif

#endif
