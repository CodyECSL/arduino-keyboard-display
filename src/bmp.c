#include "bmp.h"

#include <string.h>

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

size_t bmp_row_bytes(int width)
{
    return ((size_t)width * 3 + 3) & ~(size_t)3;
}

void bmp_header(uint8_t out[BMP_HEADER_SIZE], int width, int height)
{
    uint32_t image = (uint32_t)(bmp_row_bytes(width) * (size_t)height);
    memset(out, 0, BMP_HEADER_SIZE);
    out[0] = 'B';
    out[1] = 'M';
    put32(out + 2, BMP_HEADER_SIZE + image);
    put32(out + 10, BMP_HEADER_SIZE);
    put32(out + 14, 40);
    put32(out + 18, (uint32_t)width);
    put32(out + 22, (uint32_t)(-height));
    put16(out + 26, 1);
    put16(out + 28, 24);
    put32(out + 34, image);
    put32(out + 38, 2835); /* 72 dpi */
    put32(out + 42, 2835);
}

void bmp_row_from_rgb565(const uint8_t *src, int width, bool byte_swapped, uint8_t *dst)
{
    for (int x = 0; x < width; x++) {
        uint16_t p = byte_swapped ? (uint16_t)((src[2 * x] << 8) | src[2 * x + 1])
                                  : (uint16_t)(src[2 * x] | (src[2 * x + 1] << 8));
        unsigned r = p >> 11;
        unsigned g = (p >> 5) & 0x3F;
        unsigned b = p & 0x1F;
        dst[3 * x] = (uint8_t)((b << 3) | (b >> 2));
        dst[3 * x + 1] = (uint8_t)((g << 2) | (g >> 4));
        dst[3 * x + 2] = (uint8_t)((r << 3) | (r >> 2));
    }
    size_t used = (size_t)width * 3;
    memset(dst + used, 0, bmp_row_bytes(width) - used);
}
