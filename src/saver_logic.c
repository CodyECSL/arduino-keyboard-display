#include "saver_logic.h"

#include <string.h>

int saver_next_index(int count, int cur, bool random_order, uint32_t rnd)
{
    if (count <= 0) {
        return -1;
    }
    if (count == 1) {
        return 0;
    }
    if (random_order) {
        if (cur < 0 || cur >= count) {
            return (int)(rnd % (uint32_t)count);
        }
        int r = (int)(rnd % (uint32_t)(count - 1)); /* one of the others */
        return r >= cur ? r + 1 : r;
    }
    return (cur < 0 || cur >= count) ? 0 : (cur + 1) % count;
}

/* Position of the line equal to `name` in `set`, or NULL. */
static const char *find_line(const char *set, const char *name, size_t *line_len)
{
    size_t n = strlen(name);
    const char *p = set;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && memcmp(p, name, n) == 0) {
            *line_len = len;
            return p;
        }
        p += len + (end ? 1 : 0);
    }
    return NULL;
}

bool saver_set_has(const char *set, const char *name)
{
    size_t len;
    return name[0] != '\0' && find_line(set, name, &len) != NULL;
}

bool saver_set_toggle(char *set, size_t cap, const char *name, bool on)
{
    size_t len;
    const char *at = find_line(set, name, &len);
    if (name[0] == '\0' || strchr(name, '\n')) {
        return false;
    }
    if (on) {
        if (at) {
            return true;
        }
        size_t have = strlen(set);
        size_t need = have + (have ? 1 : 0) + strlen(name) + 1;
        if (need > cap) {
            return false;
        }
        if (have) {
            set[have++] = '\n';
        }
        strcpy(set + have, name);
        return true;
    }
    if (at) {
        char *line = set + (at - set);
        size_t drop = len + (line[len] == '\n' ? 1 : 0);
        if (drop == len && line > set) { /* last line: also drop the newline before it */
            line--;
            drop++;
        }
        memmove(line, line + drop, strlen(line + drop) + 1);
    }
    return true;
}

static inline uint16_t swap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

void saver_blend565be(uint16_t *live, const uint16_t *still, size_t pixels, int weight)
{
    if (weight <= 0) {
        memcpy(live, still, pixels * sizeof(uint16_t));
        return;
    }
    if (weight >= SAVER_BLEND_MAX) {
        return;
    }
    const uint32_t a = (uint32_t)weight;
    for (size_t i = 0; i < pixels; i++) {
        /* Spread G to the top, so one multiply mixes all three channels. */
        uint32_t bg = swap16(still[i]);
        uint32_t fg = swap16(live[i]);
        bg = (bg | (bg << 16)) & 0x07E0F81FU;
        fg = (fg | (fg << 16)) & 0x07E0F81FU;
        uint32_t m = ((((fg - bg) * a) >> 5) + bg) & 0x07E0F81FU;
        live[i] = swap16((uint16_t)(m | (m >> 16)));
    }
}
