/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

struct reference_scroll_state {
    uint32_t multiplier, divisor;
    bool invert;
    int64_t remainder;
};

/* Use layer 3's persistent scale, capped at its DT default. Old "mixed"
 * settings cannot override this, and no active-layer filter can skip scaling. */
static inline int32_t reference_scroll_step(struct reference_scroll_state *s, int32_t input,
                                           uint32_t mul, uint32_t div,
                                           uint32_t default_mul, uint32_t default_div,
                                           bool invert) {
    if (!mul || !div || (uint64_t)mul * default_div > (uint64_t)default_mul * div) {
        mul = default_mul;
        div = default_div;
    }
    if (s->multiplier != mul || s->divisor != div || s->invert != invert) {
        *s = (struct reference_scroll_state){.multiplier = mul, .divisor = div, .invert = invert};
    }
    int32_t bounded = input > 32767 ? 32767 : input < -32767 ? -32767 : input;
    int64_t value = (int64_t)(invert ? -bounded : bounded) * mul + s->remainder;
    int64_t scaled = value / div;
    s->remainder = value - scaled * div;
    return scaled > 32767 ? 32767 : scaled < -32767 ? -32767 : (int32_t)scaled;
}
