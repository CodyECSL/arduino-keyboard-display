/* Host test for src/saver_logic.c. Run: tests/host/run.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/saver_logic.h"

static int s_fail;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            s_fail++;                                                        \
        }                                                                    \
    } while (0)

static void test_next(void)
{
    CHECK(saver_next_index(0, -1, false, 5) == -1);
    CHECK(saver_next_index(1, 0, true, 5) == 0);
    CHECK(saver_next_index(3, 0, false, 0) == 1);
    CHECK(saver_next_index(3, 2, false, 0) == 0); /* wraps */
    CHECK(saver_next_index(3, -1, false, 9) == 0);
    for (uint32_t r = 0; r < 50; r++) {
        for (int cur = 0; cur < 4; cur++) {
            int n = saver_next_index(4, cur, true, r);
            CHECK(n >= 0 && n < 4 && n != cur); /* random never repeats */
        }
        int n = saver_next_index(4, -1, true, r);
        CHECK(n >= 0 && n < 4);
    }
    /* every other video can be reached */
    int seen[4] = {0};
    for (uint32_t r = 0; r < 3; r++) {
        seen[saver_next_index(4, 1, true, r)] = 1;
    }
    CHECK(seen[0] && !seen[1] && seen[2] && seen[3]);
}

static void test_set(void)
{
    char set[32] = "";
    CHECK(!saver_set_has(set, "a.mjpeg"));
    CHECK(saver_set_toggle(set, sizeof(set), "a.mjpeg", true));
    CHECK(saver_set_toggle(set, sizeof(set), "b.mjpeg", true));
    CHECK(saver_set_toggle(set, sizeof(set), "b.mjpeg", true)); /* twice: no duplicate */
    CHECK(strcmp(set, "a.mjpeg\nb.mjpeg") == 0);
    CHECK(saver_set_has(set, "a.mjpeg") && saver_set_has(set, "b.mjpeg"));
    CHECK(!saver_set_has(set, "a.mjp") && !saver_set_has(set, "mjpeg"));
    CHECK(!saver_set_has(set, ""));
    CHECK(!saver_set_toggle(set, sizeof(set), "ccccccccccccccc.mjpeg", true)); /* does not fit */
    CHECK(strcmp(set, "a.mjpeg\nb.mjpeg") == 0);
    CHECK(saver_set_toggle(set, sizeof(set), "b.mjpeg", false)); /* last */
    CHECK(strcmp(set, "a.mjpeg") == 0);
    CHECK(saver_set_toggle(set, sizeof(set), "c.mjpeg", true));
    CHECK(saver_set_toggle(set, sizeof(set), "a.mjpeg", false)); /* first */
    CHECK(strcmp(set, "c.mjpeg") == 0);
    CHECK(saver_set_toggle(set, sizeof(set), "c.mjpeg", false)); /* only */
    CHECK(strcmp(set, "") == 0);
    CHECK(saver_set_toggle(set, sizeof(set), "x.mjpeg", false)); /* absent: no-op */
    CHECK(saver_set_toggle(set, sizeof(set), "a", true) && saver_set_toggle(set, sizeof(set), "b", true) &&
          saver_set_toggle(set, sizeof(set), "c", true));
    CHECK(saver_set_toggle(set, sizeof(set), "b", false)); /* middle */
    CHECK(strcmp(set, "a\nc") == 0);
}

static uint16_t sw(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static int ch_r(uint16_t v) { return (sw(v) >> 11) & 31; }
static int ch_g(uint16_t v) { return (sw(v) >> 5) & 63; }
static int ch_b(uint16_t v) { return sw(v) & 31; }

static void test_blend(void)
{
    enum { N = 2000 };
    static uint16_t still[N], live[N], work[N];
    srand(1);
    for (int i = 0; i < N; i++) {
        still[i] = (uint16_t)rand();
        live[i] = (uint16_t)rand();
    }
    memcpy(work, live, sizeof(work));
    saver_blend565be(work, still, N, 0);
    CHECK(memcmp(work, still, sizeof(work)) == 0);
    memcpy(work, live, sizeof(work));
    saver_blend565be(work, still, N, SAVER_BLEND_MAX);
    CHECK(memcmp(work, live, sizeof(work)) == 0);
    for (int w = 1; w < SAVER_BLEND_MAX; w++) {
        memcpy(work, live, sizeof(work));
        saver_blend565be(work, still, N, w);
        for (int i = 0; i < N; i++) {
            /* each channel is within 1 step of the straight mix, and between the two inputs */
            double er = ch_r(still[i]) + (ch_r(live[i]) - ch_r(still[i])) * w / 32.0;
            double eg = ch_g(still[i]) + (ch_g(live[i]) - ch_g(still[i])) * w / 32.0;
            double eb = ch_b(still[i]) + (ch_b(live[i]) - ch_b(still[i])) * w / 32.0;
            CHECK(abs(ch_r(work[i]) - (int)er) <= 1 && abs(ch_g(work[i]) - (int)eg) <= 1 &&
                  abs(ch_b(work[i]) - (int)eb) <= 1);
        }
    }
}

int main(void)
{
    test_next();
    test_set();
    test_blend();
    if (s_fail) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("saver_logic: all checks passed\n");
    return 0;
}
