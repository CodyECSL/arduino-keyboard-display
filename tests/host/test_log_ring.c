/* Host test for src/log_ring.c. Run: tests/host/run.sh */
#include <stdio.h>
#include <string.h>

#include "../../src/log_ring.h"

static int s_fail;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            s_fail++;                                                        \
        }                                                                    \
    } while (0)

static char s_store[8];
static log_ring_t s_r;

static void fresh(void)
{
    memset(s_store, 0, sizeof(s_store));
    log_ring_init(&s_r, s_store, sizeof(s_store));
}

static void test_empty(void)
{
    fresh();
    char out[8];
    uint64_t pos = 0;
    size_t skipped = 99;
    CHECK(log_ring_read(&s_r, &pos, out, sizeof(out), &skipped) == 0);
    CHECK(skipped == 0);
    CHECK(pos == 0);
}

static void test_roundtrip(void)
{
    fresh();
    log_ring_write(&s_r, "abc", 3);
    char out[8];
    uint64_t pos = 0;
    size_t skipped;
    CHECK(log_ring_read(&s_r, &pos, out, sizeof(out), &skipped) == 3);
    CHECK(memcmp(out, "abc", 3) == 0);
    CHECK(pos == 3);
    CHECK(log_ring_read(&s_r, &pos, out, sizeof(out), &skipped) == 0); /* nothing new */
    log_ring_write(&s_r, "de", 2);
    CHECK(log_ring_read(&s_r, &pos, out, sizeof(out), &skipped) == 2);
    CHECK(memcmp(out, "de", 2) == 0);
}

static void test_wrap_without_loss(void)
{
    fresh();
    char out[8];
    uint64_t pos = 0;
    size_t skipped;
    log_ring_write(&s_r, "123456", 6);
    CHECK(log_ring_read(&s_r, &pos, out, sizeof(out), &skipped) == 6);
    log_ring_write(&s_r, "abcd", 4); /* wraps around the end of the storage */
    CHECK(log_ring_read(&s_r, &pos, out, sizeof(out), &skipped) == 4);
    CHECK(skipped == 0);
    CHECK(memcmp(out, "abcd", 4) == 0);
}

static void test_overflow_reports_loss(void)
{
    fresh();
    char out[8];
    uint64_t pos = 0;
    size_t skipped;
    log_ring_write(&s_r, "0123456789", 10); /* 10 bytes into 8: "01" lost */
    CHECK(log_ring_read(&s_r, &pos, out, sizeof(out), &skipped) == 8);
    CHECK(skipped == 2);
    CHECK(memcmp(out, "23456789", 8) == 0);
    CHECK(pos == 10);
}

static void test_many_small_writes_then_read_from_zero(void)
{
    fresh();
    for (int i = 0; i < 5; i++) {
        log_ring_write(&s_r, "xy", 2); /* 10 bytes in total */
    }
    char out[8];
    uint64_t pos = 0;
    size_t skipped;
    CHECK(log_ring_read(&s_r, &pos, out, sizeof(out), &skipped) == 8);
    CHECK(skipped == 2);
    CHECK(memcmp(out, "xyxyxyxy", 8) == 0);
}

static void test_partial_reads(void)
{
    fresh();
    log_ring_write(&s_r, "abcdef", 6);
    char out[8];
    uint64_t pos = 0;
    size_t skipped;
    CHECK(log_ring_read(&s_r, &pos, out, 4, &skipped) == 4);
    CHECK(memcmp(out, "abcd", 4) == 0);
    CHECK(log_ring_read(&s_r, &pos, out, 4, &skipped) == 2);
    CHECK(memcmp(out, "ef", 2) == 0);
}

static void test_write_larger_than_capacity(void)
{
    fresh();
    char out[8];
    uint64_t pos = 0;
    size_t skipped;
    log_ring_write(&s_r, "ABCDEFGHIJKLMNOP", 16); /* 16 bytes: only the last 8 stay */
    CHECK(log_ring_read(&s_r, &pos, out, sizeof(out), &skipped) == 8);
    CHECK(skipped == 8);
    CHECK(memcmp(out, "IJKLMNOP", 8) == 0);
}

static void test_position_ahead_of_ring(void)
{
    fresh();
    log_ring_write(&s_r, "abc", 3);
    char out[8];
    uint64_t pos = 100; /* stale position, for example after a reset */
    size_t skipped;
    CHECK(log_ring_read(&s_r, &pos, out, sizeof(out), &skipped) == 0);
    CHECK(pos == 3);
}

int main(void)
{
    test_empty();
    test_roundtrip();
    test_wrap_without_loss();
    test_overflow_reports_loss();
    test_many_small_writes_then_read_from_zero();
    test_partial_reads();
    test_write_larger_than_capacity();
    test_position_ahead_of_ring();
    if (s_fail) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
