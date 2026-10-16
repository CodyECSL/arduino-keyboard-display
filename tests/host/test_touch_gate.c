/* Host test for src/touch_gate.c. Run: tests/host/run.sh */
#include <stdio.h>

#include "../../src/touch_gate.h"

static int s_fail;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            s_fail++;                                                        \
        }                                                                    \
    } while (0)

static bool s_asleep;
static int s_wake_calls;

static bool wake(void)
{
    s_wake_calls++;
    bool was = s_asleep;
    s_asleep = false;
    return was;
}

static void setup(bool asleep)
{
    s_asleep = asleep;
    s_wake_calls = 0;
}

static void test_awake_touch_passes(void)
{
    touch_gate_t g = {0};
    setup(false);
    CHECK(touch_gate_pressed(&g, true, 0, wake));
    CHECK(touch_gate_pressed(&g, true, 30, wake));
    CHECK(s_wake_calls == 1); /* asked once, at the start of the touch */
    CHECK(!touch_gate_pressed(&g, false, 60, wake));
}

static void test_waking_touch_is_hidden_until_release(void)
{
    touch_gate_t g = {0};
    setup(true);
    CHECK(!touch_gate_pressed(&g, true, 0, wake)); /* this touch only wakes */
    CHECK(!touch_gate_pressed(&g, true, 30, wake));
    CHECK(!touch_gate_pressed(&g, true, 600, wake)); /* however long the finger stays */
    CHECK(s_wake_calls == 1);
    CHECK(!touch_gate_pressed(&g, false, 700, wake));
}

static void test_next_touch_after_a_real_release_passes(void)
{
    touch_gate_t g = {0};
    setup(true);
    touch_gate_pressed(&g, true, 0, wake);
    touch_gate_pressed(&g, false, 100, wake);
    CHECK(!touch_gate_pressed(&g, false, 100 + TOUCH_GATE_RELEASE_MS, wake)); /* the finger has been up long enough */
    CHECK(touch_gate_pressed(&g, true, 400, wake));                           /* a new touch works */
}

static void test_brief_gap_does_not_end_the_ignoring(void)
{
    touch_gate_t g = {0};
    setup(true);
    touch_gate_pressed(&g, true, 0, wake);
    CHECK(!touch_gate_pressed(&g, false, 30, wake)); /* a missed sample while the finger is still down */
    CHECK(!touch_gate_pressed(&g, true, 60, wake));  /* still the same touch */
    CHECK(s_wake_calls == 1);
}

static void test_press_after_long_gap_without_release_sample(void)
{
    touch_gate_t g = {0};
    setup(true);
    touch_gate_pressed(&g, true, 0, wake);
    touch_gate_pressed(&g, false, 50, wake);
    CHECK(touch_gate_pressed(&g, true, 50 + TOUCH_GATE_RELEASE_MS + 1, wake)); /* no release sample in between */
}

static void test_second_wake_is_hidden_too(void)
{
    touch_gate_t g = {0};
    setup(true);
    touch_gate_pressed(&g, true, 0, wake);
    touch_gate_pressed(&g, false, 100, wake);
    touch_gate_pressed(&g, false, 300, wake);
    s_asleep = true; /* the screensaver started again */
    CHECK(!touch_gate_pressed(&g, true, 5000, wake));
    CHECK(s_wake_calls == 2);
}

static void test_no_wake_function(void)
{
    touch_gate_t g = {0};
    CHECK(touch_gate_pressed(&g, true, 0, NULL));
    CHECK(!touch_gate_pressed(&g, false, 30, NULL));
}

int main(void)
{
    test_awake_touch_passes();
    test_waking_touch_is_hidden_until_release();
    test_next_touch_after_a_real_release_passes();
    test_brief_gap_does_not_end_the_ignoring();
    test_press_after_long_gap_without_release_sample();
    test_second_wake_is_hidden_too();
    test_no_wake_function();
    if (s_fail) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
