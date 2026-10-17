/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "../src/mixed_scroll_logic.h"

static const struct mixed_config cfg = {8, 100, 24, 250};

int main(void) {
    struct mixed_state s = {0};
    struct mixed_result r;

    /* Small reports accumulate without losing the initial vertical distance. */
    assert(mixed_step(&s, &cfg, 1, 3, 0).scroll == 0);
    assert(mixed_step(&s, &cfg, 0, 3, 8).scroll == 0);
    r = mixed_step(&s, &cfg, 1, 3, 16);
    assert(r.scroll == 9 && r.direction == 0);
    r = mixed_step(&s, &cfg, 200, 6, 24);
    assert(r.scroll == 6 && r.direction == 0); /* X jitter cannot navigate. */
    assert(mixed_step(&s, &cfg, 0, -7, 32).scroll == -7); /* Scroll reversals. */

    /* Deliberate left, exactly one action even across reversals in the stroke. */
    mixed_reset(&s);
    r = mixed_step(&s, &cfg, -60, 2, 0);
    assert(r.scroll == 0 && r.direction == 0);
    r = mixed_step(&s, &cfg, -40, 0, 8);
    assert(r.direction == -1 && r.scroll == 0);
    assert(mixed_step(&s, &cfg, -1000, 50, 16).direction == 0);
    assert(mixed_step(&s, &cfg, 1000, 50, 24).direction == 0);

    /* Zero-valued sensor reports do not delay rearming after a pause. */
    for (int i = 32; i < 274; i += 8) {
        assert(mixed_step(&s, &cfg, 0, 0, i).direction == 0);
    }
    assert(mixed_step(&s, &cfg, 100, 0, 274).direction == 1);

    /* A single batched diagonal X/Y frame must not choose horizontal because
     * the sensor happened to send X first. Equal diagonals favor scrolling. */
    mixed_reset(&s);
    r = mixed_step(&s, &cfg, 30, 30, 0);
    assert(r.scroll == 30 && r.direction == 0);
    assert(mixed_step(&s, &cfg, 150, 1, 8).direction == 0);

    /* Starting with a little sideways drift must not disable scrolling. */
    mixed_reset(&s);
    assert(mixed_step(&s, &cfg, 12, 0, 0).direction == 0);
    r = mixed_step(&s, &cfg, 0, 20, 8);
    assert(r.scroll == 20 && r.direction == 0);

    /* Layer exit/re-entry discards old partial gestures and axis locks. */
    mixed_reset(&s);
    assert(mixed_step(&s, &cfg, 70, 0, 0).direction == 0);
    mixed_reset(&s);
    assert(mixed_step(&s, &cfg, 40, 0, 8).direction == 0);
    mixed_reset(&s);
    assert(mixed_step(&s, &cfg, 0, -16, 16).scroll == -16);

    /* Noise oscillation must not accumulate into navigation. */
    mixed_reset(&s);
    for (int i = 0; i < 1000; ++i) {
        r = mixed_step(&s, &cfg, i % 2 ? -2 : 2, 0, i * 8);
        assert(r.direction == 0 && r.scroll == 0);
    }
    /* Saturating accumulation is defined even for malformed huge input. */
    assert(mixed_add(100, INT32_MAX) == 32767);
    assert(mixed_add(-100, INT32_MIN) == -32767);
    puts("mixed scroll: all motion tests passed");
    return 0;
}
