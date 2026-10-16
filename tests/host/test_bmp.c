/* Host test for src/bmp.c. Run: tests/host/run.sh */
#include <stdio.h>
#include <string.h>

#include "../../src/bmp.h"

static int s_fail;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            s_fail++;                                                        \
        }                                                                    \
    } while (0)

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static void test_row_bytes(void)
{
    CHECK(bmp_row_bytes(480) == 1440);
    CHECK(bmp_row_bytes(3) == 12); /* 9 bytes padded to 12 */
    CHECK(bmp_row_bytes(1) == 4);
}

static void test_header(void)
{
    uint8_t h[BMP_HEADER_SIZE];
    bmp_header(h, 480, 320);
    CHECK(h[0] == 'B' && h[1] == 'M');
    CHECK(le32(h + 2) == 54 + 1440u * 320u); /* file size */
    CHECK(le32(h + 10) == 54);               /* pixel data offset */
    CHECK(le32(h + 14) == 40);               /* DIB header size */
    CHECK(le32(h + 18) == 480);
    CHECK((int32_t)le32(h + 22) == -320);    /* negative: rows run top to bottom */
    CHECK(le16(h + 26) == 1);
    CHECK(le16(h + 28) == 24);
    CHECK(le32(h + 30) == 0);                /* no compression */
    CHECK(le32(h + 34) == 1440u * 320u);
}

static void test_row_native_order(void)
{
    /* red 0xF800, green 0x07E0, blue 0x001F, white 0xFFFF, little endian in memory */
    const uint8_t src[] = {0x00, 0xF8, 0xE0, 0x07, 0x1F, 0x00, 0xFF, 0xFF};
    uint8_t dst[bmp_row_bytes(4)];
    bmp_row_from_rgb565(src, 4, false, dst);
    const uint8_t want[] = {0, 0, 255, /* red   as B,G,R */
                            0, 255, 0, /* green */
                            255, 0, 0, /* blue  */
                            255, 255, 255};
    CHECK(memcmp(dst, want, sizeof(want)) == 0);
}

static void test_row_swapped_order(void)
{
    /* the same colours, high byte first */
    const uint8_t src[] = {0xF8, 0x00, 0x07, 0xE0, 0x00, 0x1F, 0xFF, 0xFF};
    uint8_t dst[bmp_row_bytes(4)];
    bmp_row_from_rgb565(src, 4, true, dst);
    const uint8_t want[] = {0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255};
    CHECK(memcmp(dst, want, sizeof(want)) == 0);
}

static void test_row_padding_is_zero(void)
{
    const uint8_t src[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t dst[12];
    memset(dst, 0xAA, sizeof(dst));
    bmp_row_from_rgb565(src, 3, false, dst);
    CHECK(dst[9] == 0 && dst[10] == 0 && dst[11] == 0);
}

int main(void)
{
    test_row_bytes();
    test_header();
    test_row_native_order();
    test_row_swapped_order();
    test_row_padding_is_zero();
    if (s_fail) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
