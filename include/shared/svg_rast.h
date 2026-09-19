#ifndef SHARED_SVG_RAST_H
#define SHARED_SVG_RAST_H

#ifdef __cplusplus
extern "C" {
#endif

int svg_rast_file(const char *path, int dim, unsigned char **out_rgba, int *out_dim);
void svg_rast_free(unsigned char *rgba);

#ifdef __cplusplus
}
#endif

#endif
