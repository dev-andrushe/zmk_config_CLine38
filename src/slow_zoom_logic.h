/* SPDX-License-Identifier: MIT */
#pragma once
#include "mixed_scroll_logic.h"

struct slow_zoom_config {
    int32_t counts_per_step;
    int64_t min_interval_ms, ctrl_settle_ms;
};
struct slow_zoom_state {
    struct mixed_state direction;
    int32_t frame_x, frame_y, remainder;
    int64_t activated_at, last_emit;
    bool emitted, ctrl_started;
};
struct slow_zoom_result { int zoom; int32_t horizontal; };

static inline void slow_zoom_reset(struct slow_zoom_state *s, int64_t now) {
    *s = (struct slow_zoom_state){.activated_at = now};
}

/* One complete raw X/Y frame. No timers or pending steps: a flick cannot
 * leave a queue of zoom actions that continues after motion stops. */
static inline struct slow_zoom_result
slow_zoom_step(struct slow_zoom_state *s, const struct slow_zoom_config *cfg,
               int32_t dx, int32_t dy, int64_t now) {
    static const struct mixed_config direction_cfg = {8, 100, 24, 250};
    struct slow_zoom_result out = {0};
    if (s->direction.has_motion && now - s->direction.last_motion >= direction_cfg.idle_ms) {
        s->remainder = 0;
        s->ctrl_started = false;
    }
    enum mixed_mode previous = s->direction.mode;
    struct mixed_result direction = mixed_step(&s->direction, &direction_cfg, dx, dy, now);
    if (s->direction.mode == MIXED_VERTICAL && !s->ctrl_started) {
        s->activated_at = now;
        s->ctrl_started = true;
    } else if (s->direction.mode != MIXED_VERTICAL) {
        s->ctrl_started = false;
    }
    if (direction.direction) {
        /* Retain the original horizontal recognizer/bindings/cooldown.
         * Delay X until direction is known, then replay its initial distance. */
        out.horizontal = s->direction.x;
    } else if (s->direction.mode == MIXED_FIRED && previous == MIXED_FIRED) {
        out.horizontal = dx;
    }
    int32_t vertical = direction.scroll;
    if (!vertical) {
        return out;
    }
    if (s->emitted && now - s->last_emit < cfg->min_interval_ms) {
        s->remainder = 0; /* Drop excess movement instead of building a burst. */
        return out;
    }
    if ((s->remainder < 0 && vertical > 0) || (s->remainder > 0 && vertical < 0)) {
        s->remainder = 0;
    }
    s->remainder = mixed_add(s->remainder, vertical);
    if (now - s->activated_at < cfg->ctrl_settle_ms) {
        /* Keep a sub-step at most; a fresh report is needed after Ctrl settles. */
        if (s->remainder >= cfg->counts_per_step) {
            s->remainder = cfg->counts_per_step - 1;
        } else if (s->remainder <= -cfg->counts_per_step) {
            s->remainder = 1 - cfg->counts_per_step;
        }
        return out;
    }
    if (s->remainder >= cfg->counts_per_step || s->remainder <= -cfg->counts_per_step) {
        out.zoom = s->remainder > 0 ? 1 : -1;
        s->remainder = 0; /* One notch maximum, even for a huge sensor delta. */
        s->last_emit = now;
        s->emitted = true;
    }
    return out;
}
