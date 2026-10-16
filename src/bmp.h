#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BMP_HEADER_SIZE 54

/** Bytes of one 24 bit BMP row: width * 3, padded up to a multiple of 4. */
size_t bmp_row_bytes(int width);

/** File header + DIB header for a 24 bit, top-down image (negative height), no compression. */
void bmp_header(uint8_t out[BMP_HEADER_SIZE], int width, int height);

/** Convert `width` RGB565 pixels (2 bytes each) to one BMP row (B, G, R, zero padding).
 * `byte_swapped`: each pixel is stored high byte first (LVGL with LV_COLOR_16_SWAP 1). */
void bmp_row_from_rgb565(const uint8_t *src, int width, bool byte_swapped, uint8_t *dst);
