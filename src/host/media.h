#ifndef HOST_MEDIA_H
#define HOST_MEDIA_H

#ifdef __cplusplus
extern "C" {
#endif

void media_init(void *hwnd, const char *project_root);
void media_shutdown(void);
void media_poll(void);
void media_set_url(const char *url);
void media_set_playing(int playing);
void media_set_loop(int loop);
void media_set_volume(float volume);
int media_is_playing(void);
int media_is_busy(void);
float media_level(void);
void media_embed_set_view(int x, int y, int w, int h, int visible);

#ifdef __cplusplus
}
#endif

#endif
