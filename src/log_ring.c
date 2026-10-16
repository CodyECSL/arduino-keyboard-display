#include "log_ring.h"

#include <string.h>

void log_ring_init(log_ring_t *r, char *storage, size_t cap)
{
    r->buf = storage;
    r->cap = cap;
    r->total = 0;
}

void log_ring_write(log_ring_t *r, const char *data, size_t len)
{
    if (len > r->cap) {
        /* only the last `cap` bytes can stay; count the others as written and lost */
        r->total += len - r->cap;
        data += len - r->cap;
        len = r->cap;
    }
    size_t off = (size_t)(r->total % r->cap);
    size_t first = r->cap - off;
    if (first > len) {
        first = len;
    }
    memcpy(r->buf + off, data, first);
    memcpy(r->buf, data + first, len - first);
    r->total += len;
}

size_t log_ring_read(const log_ring_t *r, uint64_t *pos, char *out, size_t max, size_t *skipped)
{
    size_t lost = 0;
    uint64_t oldest = r->total > r->cap ? r->total - r->cap : 0;
    if (*pos < oldest) {
        lost = (size_t)(oldest - *pos);
        *pos = oldest;
    }
    if (skipped) {
        *skipped = lost;
    }
    if (*pos >= r->total) {
        *pos = r->total; /* also repairs a position from before a reset */
        return 0;
    }
    uint64_t avail = r->total - *pos;
    size_t n = avail < max ? (size_t)avail : max;
    size_t off = (size_t)(*pos % r->cap);
    size_t first = r->cap - off;
    if (first > n) {
        first = n;
    }
    memcpy(out, r->buf + off, first);
    memcpy(out + first, r->buf, n - first);
    *pos += n;
    return n;
}
