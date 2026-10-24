/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum retro_motion_action { RETRO_DROP, RETRO_BUFFER, RETRO_FORWARD, RETRO_COMMIT };
struct retro_motion_state {
    uint32_t epoch;
    int32_t x, y;
    int64_t last_motion;
    bool buffered;
};
struct retro_motion_result {
    enum retro_motion_action action;
    int32_t x, y;
};
static inline int32_t retro_motion_add(int32_t a, int32_t b) {
    int64_t sum = (int64_t)a + b;
    return sum > 32767 ? 32767 : sum < -32767 ? -32767 : (int32_t)sum;
}
static inline struct retro_motion_result
retro_motion_step(struct retro_motion_state *s, uint32_t epoch, uint32_t frame_epoch,
                  bool pending, int32_t dx, int32_t dy, int64_t now,
                  int32_t threshold, int64_t idle_ms) {
    if (s->epoch != epoch || !pending || frame_epoch != epoch ||
        (s->buffered && now - s->last_motion >= idle_ms)) {
        *s = (struct retro_motion_state){.epoch = epoch};
    }
    if (frame_epoch != epoch) {
        return (struct retro_motion_result){.action = RETRO_DROP};
    }
    if (!pending) {
        return (struct retro_motion_result){RETRO_FORWARD, dx, dy};
    }
    if (dx || dy) {
        s->x = retro_motion_add(s->x, dx);
        s->y = retro_motion_add(s->y, dy);
        s->last_motion = now;
        s->buffered = true;
    }
    if (s->x < threshold && s->x > -threshold && s->y < threshold && s->y > -threshold) {
        return (struct retro_motion_result){.action = RETRO_BUFFER};
    }
    struct retro_motion_result out = {RETRO_COMMIT, s->x, s->y};
    *s = (struct retro_motion_state){.epoch = epoch};
    return out;
}
