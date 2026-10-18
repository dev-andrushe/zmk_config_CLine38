/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "../src/reference_scroll_logic.h"

int main(void) {
    struct reference_scroll_state s = {0};
    /* Exactly one application of LT3's initial 1/8, including small reports. */
    for (int i = 0; i < 7; ++i) {
        assert(reference_scroll_step(&s, 1, 1, 8, 1, 8, false) == 0);
    }
    assert(reference_scroll_step(&s, 1, 1, 8, 1, 8, false) == 1);
    assert(reference_scroll_step(&s, -16, 1, 8, 1, 8, false) == -2);

    /* Honor slower saved scroll values; cap fast or invalid saved values. */
    assert(reference_scroll_step(&s, 64, 1, 64, 1, 8, false) == 1);
    assert(reference_scroll_step(&s, 64, 1, 1, 1, 8, false) == 8);
    assert(reference_scroll_step(&s, 64, 0, 8, 1, 8, false) == 8);
    assert(reference_scroll_step(&s, 64, 1, 0, 1, 8, false) == 8);
    assert(reference_scroll_step(&s, 64, 2, 32, 1, 8, false) == 4);

    /* Changing the scale or inversion cannot reuse an old fractional step. */
    s = (struct reference_scroll_state){0};
    assert(reference_scroll_step(&s, 7, 1, 8, 1, 8, false) == 0);
    assert(reference_scroll_step(&s, 1, 1, 64, 1, 8, false) == 0);
    assert(s.remainder == 1);
    assert(reference_scroll_step(&s, 8, 1, 8, 1, 8, true) == -1);
    assert(reference_scroll_step(&s, -8, 1, 8, 1, 8, true) == 1);

    /* Sensor and stored-setting extremes stay defined and bounded. */
    assert(reference_scroll_step(&s, INT32_MIN, UINT32_MAX, UINT32_MAX,
                                 1, 8, false) == -4095);
    s = (struct reference_scroll_state){0};
    assert(reference_scroll_step(&s, INT32_MAX, UINT32_MAX, UINT32_MAX,
                                 UINT32_MAX, UINT32_MAX, false) == 32767);
    assert(reference_scroll_step(&s, INT32_MIN, UINT32_MAX, UINT32_MAX,
                                 UINT32_MAX, UINT32_MAX, true) == 32767);
    puts("reference scroll: shared scale, speed cap, fractions and extremes passed");
}
