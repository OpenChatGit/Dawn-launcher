#include "shared/svg_rast.h"

#include <lunasvg.h>

#include <memory>
#include <stdlib.h>
#include <string.h>

int
svg_rast_file(const char *path, int dim, unsigned char **out_rgba, int *out_dim)
{
    std::unique_ptr<lunasvg::Document> doc;
    lunasvg::Bitmap bmp;
    unsigned char *rgba;
    int y;
    int width;
    int height;
    int stride;

    if (!path || !path[0] || !out_rgba || dim < 8) {
        return 0;
    }
    *out_rgba = NULL;
    if (out_dim) {
        *out_dim = 0;
    }
    doc = lunasvg::Document::loadFromFile(path);
    if (!doc) {
        return 0;
    }
    bmp = doc->renderToBitmap(dim, dim, 0x00000000u);
    if (bmp.isNull() || !bmp.data()) {
        return 0;
    }
    bmp.convertToRGBA();
    width = bmp.width();
    height = bmp.height();
    stride = bmp.stride();
    if (width < 1 || height < 1 || stride < width * 4) {
        return 0;
    }
    rgba = (unsigned char *)calloc((size_t)width * (size_t)height * 4u, 1);
    if (!rgba) {
        return 0;
    }
    for (y = 0; y < height; y++) {
        memcpy(rgba + (size_t)y * (size_t)width * 4u, bmp.data() + (size_t)y * (size_t)stride, (size_t)width * 4u);
    }
    *out_rgba = rgba;
    if (out_dim) {
        *out_dim = width < height ? width : height;
    }
    return 1;
}

void
svg_rast_free(unsigned char *rgba)
{
    free(rgba);
}
