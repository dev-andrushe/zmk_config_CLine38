/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "../src/slow_zoom_logic.h"

static const struct slow_zoom_config cfg = {64, 200, 40};

int main(void) {
    struct slow_zoom_state s;
    struct slow_zoom_result r;
    slow_zoom_reset(&s, 0);
    assert(slow_zoom_step(&s, &cfg, 0, 8, 0).zoom == 0);
    assert(s.ctrl_started && s.direction.mode == MIXED_VERTICAL);
    assert(slow_zoom_step(&s, &cfg, 0, 55, 40).zoom == 0); /* 63 counts. */
    r = slow_zoom_step(&s, &cfg, 0, 1, 48);
    assert(r.zoom == 1 && r.horizontal == 0); /* Exactly 64 counts. */
    for (int64_t now = 56; now < 248; now += 8) {
        assert(slow_zoom_step(&s, &cfg, 0, 20000, now).zoom == 0);
    }
    assert(s.remainder == 0); /* Fast flicks never leave a backlog. */
    assert(slow_zoom_step(&s, &cfg, 0, 64, 248).zoom == 1);
    assert(slow_zoom_step(&s, &cfg, 0, -64, 448).zoom == -1);
    assert(slow_zoom_step(&s, &cfg, 0, -32767, 648).zoom == -1); /* At most one notch. */
    for (int64_t now = 656; now < 1200; now += 8) {
        assert(slow_zoom_step(&s, &cfg, 0, 0, now).zoom == 0); /* No inertia/timer output. */
    }

    /* Control must settle before the first wheel report; a fresh motion report
     * is required afterwards, so no zoom is scheduled after a short flick. */
    slow_zoom_reset(&s, 0);
    assert(slow_zoom_step(&s, &cfg, 0, 1000, 1000).zoom == 0);
    assert(slow_zoom_step(&s, &cfg, 0, 0, 1040).zoom == 0);
    assert(slow_zoom_step(&s, &cfg, 0, 1, 1048).zoom == 1);

    /* Preserve full horizontal distance for the existing recognizer, which
     * retains its own binding and repeat cooldown. Ignore Y in that stroke. */
    slow_zoom_reset(&s, 0);
    assert(slow_zoom_step(&s, &cfg, 60, 0, 0).horizontal == 0);
    r = slow_zoom_step(&s, &cfg, 40, 0, 8);
    assert(r.horizontal == 100 && r.zoom == 0 && !s.ctrl_started);
    r = slow_zoom_step(&s, &cfg, 120, 0, 16);
    assert(r.horizontal == 120 && r.zoom == 0);
    r = slow_zoom_step(&s, &cfg, 0, 1000, 24);
    assert(r.horizontal == 0 && r.zoom == 0);
    r = slow_zoom_step(&s, &cfg, 0, 32, 274);
    assert(r.horizontal == 0 && r.zoom == 0 && s.ctrl_started);
    assert(slow_zoom_step(&s, &cfg, 0, 32, 314).zoom == 1);
    r = slow_zoom_step(&s, &cfg, -500, 64, 514);
    assert(r.zoom == 1 && r.horizontal == 0); /* X jitter cannot switch desktops. */

    /* Equal diagonals choose vertical, not a horizontal gesture. */
    slow_zoom_reset(&s, 0);
    r = slow_zoom_step(&s, &cfg, -100, -100, 0);
    assert(r.horizontal == 0 && r.zoom == 0);
    r = slow_zoom_step(&s, &cfg, -100, -1, 40);
    assert(r.horizontal == 0 && r.zoom == -1);

    /* A pause or leaving/re-entering L8 discards fractional steps. */
    slow_zoom_reset(&s, 0);
    assert(slow_zoom_step(&s, &cfg, 0, 60, 0).zoom == 0);
    assert(slow_zoom_step(&s, &cfg, 0, 8, 250).zoom == 0);
    assert(s.remainder == 8);
    slow_zoom_reset(&s, 300);
    assert(slow_zoom_step(&s, &cfg, 0, -8, 300).zoom == 0);
    assert(slow_zoom_step(&s, &cfg, 0, -56, 340).zoom == -1);

    /* Direction reversal clears old signed remainder instead of resisting it. */
    slow_zoom_reset(&s, 0);
    assert(slow_zoom_step(&s, &cfg, 0, 32, 0).zoom == 0);
    assert(slow_zoom_step(&s, &cfg, 0, -32, 40).zoom == 0);
    assert(slow_zoom_step(&s, &cfg, 0, -32, 48).zoom == -1);
    puts("slow zoom: fine steps, rate limit, direction lock and reset tests passed");
}
