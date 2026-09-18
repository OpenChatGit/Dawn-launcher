#ifndef HOST_VIDEO_BG_H
#define HOST_VIDEO_BG_H

#ifdef __cplusplus
extern "C" {
#endif

void video_bg_init(void);
void video_bg_shutdown(void);
void video_bg_tick(void);
void video_bg_set_source(const char *url, int loop);
void video_bg_set_gain(int muted, float volume);
void video_bg_set_output(int w, int h);
void *video_bg_shared_handle(void);
int video_bg_frame_size(int *w, int *h);
int video_bg_lock_frame(const unsigned char **bgra, int *w, int *h, int *stride);
void video_bg_unlock_frame(void);
unsigned video_bg_generation(void);

#ifdef __cplusplus
}
#endif

#endif
