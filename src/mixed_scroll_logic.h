/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum mixed_mode { MIXED_UNDECIDED, MIXED_VERTICAL, MIXED_HORIZONTAL, MIXED_FIRED };

struct mixed_config {
    int32_t direction_threshold;
    int32_t gesture_threshold;
    int32_t diagonal_threshold;
    int64_t idle_ms;
};

struct mixed_state {
    enum mixed_mode mode;
    int32_t x, y;
    int64_t last_motion;
    bool has_motion;
};

struct mixed_result {
    int32_t scroll;
    int direction; /* -1 left, +1 right, 0 none */
};

/* Limit accumulated undecided motion and malformed input without signed overflow. */
static inline int32_t mixed_add(int32_t a, int32_t b) {
    int64_t sum = (int64_t)a + b;
    return sum > 32767 ? 32767 : sum < -32767 ? -32767 : (int32_t)sum;
}

static inline void mixed_reset(struct mixed_state *state) {
    *state = (struct mixed_state){0};
}

/* Called once per complete sensor report, never separately for X and Y.
 * A vertical stroke stays vertical even if a later sample has large X jitter.
 * A horizontal stroke emits at most once until a real idle gap or layer reset.
 */
static inline struct mixed_result mixed_step(struct mixed_state *s,
                                             const struct mixed_config *cfg,
                                             int32_t dx, int32_t dy, int64_t now) {
    struct mixed_result out = {0};
    if (s->has_motion && now - s->last_motion >= cfg->idle_ms) {
        mixed_reset(s);
    }
    if (dx == 0 && dy == 0) {
        return out; /* Zero reports must not keep a gesture alive. */
    }
    s->last_motion = now;
    s->has_motion = true;

    if (s->mode == MIXED_FIRED) {
        return out;
    }
    if (s->mode == MIXED_VERTICAL) {
        out.scroll = dy;
        return out;
    }

    s->x = mixed_add(s->x, dx);
    s->y = mixed_add(s->y, dy);
    int32_t ax = s->x < 0 ? -s->x : s->x;
    int32_t ay = s->y < 0 ? -s->y : s->y;

    if (s->mode == MIXED_UNDECIDED) {
        if (ay >= cfg->direction_threshold && ay * 2 >= ax * 3) {
            s->mode = MIXED_VERTICAL;
        } else if (ax >= cfg->gesture_threshold && ax * 2 >= ay * 3) {
            s->mode = MIXED_HORIZONTAL;
        } else if (ay >= cfg->diagonal_threshold && ax * 2 < ay * 3) {
            /* Ambiguous diagonals favor scrolling, not browser navigation.
             * Small initial sideways drift must not lock out vertical input. */
            s->mode = MIXED_VERTICAL;
        }
        if (s->mode == MIXED_VERTICAL) {
            out.scroll = s->y; /* Retain motion buffered during direction selection. */
            s->x = s->y = 0;
            return out;
        }
    }

    if (s->mode == MIXED_HORIZONTAL && ax >= cfg->gesture_threshold) {
        out.direction = s->x < 0 ? -1 : 1;
        s->mode = MIXED_FIRED;
    }
    return out;
}
