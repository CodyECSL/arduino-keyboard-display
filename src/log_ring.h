#pragma once

#include <stddef.h>
#include <stdint.h>

/* A fixed-size ring of log text. Readers keep their own absolute position, so the writer never waits for them.
 * Not thread-safe: the caller serializes write and read. */
typedef struct {
    char *buf;
    size_t cap;
    uint64_t total; /* bytes ever written; byte k is at buf[k % cap] */
} log_ring_t;

void log_ring_init(log_ring_t *r, char *storage, size_t cap);
void log_ring_write(log_ring_t *r, const char *data, size_t len);

/** Copy up to `max` bytes from absolute position *pos and advance *pos. If *pos is older than the oldest kept
 * byte, *pos jumps to the oldest byte and *skipped (may be NULL) is the number of bytes lost. Returns bytes copied. */
size_t log_ring_read(const log_ring_t *r, uint64_t *pos, char *out, size_t max, size_t *skipped);
