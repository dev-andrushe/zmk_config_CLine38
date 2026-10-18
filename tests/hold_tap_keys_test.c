/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include "hold_tap_shim.h"

static int keymap_event(const zmk_event_t *event);
static int combo_event(const zmk_event_t *event) { return ZMK_EV_EVENT_BUBBLE; }
/* Negative control: reproduce the old CMake order without changing Hold-Tap. */
#ifdef BROKEN_SUBSCRIPTION_ORDER
ZMK_LISTENER(keymap, keymap_event);
ZMK_SUBSCRIPTION(keymap, zmk_position_state_changed);
#endif
#include "behavior_hold_tap.c"
ZMK_LISTENER(combo, combo_event);
ZMK_SUBSCRIPTION(combo, zmk_position_state_changed);
#ifndef BROKEN_SUBSCRIPTION_ORDER
ZMK_LISTENER(keymap, keymap_event);
ZMK_SUBSCRIPTION(keymap, zmk_position_state_changed);
#endif
#include "upstream_event_manager.c"
ZMK_EVENT_IMPL(zmk_position_state_changed);
ZMK_EVENT_IMPL(zmk_keycode_state_changed);

static struct behavior_hold_tap_config config = {
    .tapping_term_ms = 200, .hold_behavior_dev = "mo", .tap_behavior_dev = "kp",
    .flavor = FLAVOR_HOLD_PREFERRED, .retro_tap = true,
};
static const struct device hold_device = {.config = &config};
static struct zmk_behavior_binding lt = {.behavior_dev = "lt", .param1 = 3, .param2 = 44};
static int active_layer, held_mod, tap_down, tap_up, other_down, other_up;
static int seen_layer, seen_mod;

const struct device *zmk_behavior_get_binding(const char *name) { return &hold_device; }
int zmk_behavior_invoke_binding(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event, bool pressed) {
    if (!strcmp(binding->behavior_dev, "mo")) {
        active_layer = pressed ? binding->param1 : 0;
    } else if (!strcmp(binding->behavior_dev, "mod")) {
        held_mod = pressed ? binding->param1 : 0;
    } else {
        if (pressed) { ++tap_down; } else { ++tap_up; }
    }
    return 0;
}

/* Minimal keymap/HID endpoint: record the layer/modifier at the exact moment
 * the other key is dispatched, not just the final Hold-Tap status. */
static int keymap_event(const zmk_event_t *event) {
    struct zmk_position_state_changed *ev = as_zmk_position_state_changed(event);
    if (ev->position == 0) {
        struct zmk_behavior_binding_event binding_event = {
            .source = ev->source, .position = ev->position, .timestamp = ev->timestamp,
        };
        if (ev->state) { on_hold_tap_binding_pressed(&lt, binding_event); }
        else { on_hold_tap_binding_released(&lt, binding_event); }
    } else if (ev->state) {
        ++other_down;
        seen_layer = active_layer;
        seen_mod = held_mod;
    } else {
        ++other_up;
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static void key(int position, bool down, int64_t now, uint8_t source) {
    test_now = now;
    assert(raise_zmk_position_state_changed((struct zmk_position_state_changed){
        .source = source, .position = position, .state = down, .timestamp = now,
    }) == 0);
}
static void timer(int64_t now) {
    test_now = now;
    struct active_hold_tap *hold = find_hold_tap(0);
    assert(hold != NULL);
    hold->work.work.handler(&hold->work.work);
}
static void reset(void) {
    assert(cline38_retro_mutex == 0);
    for (int i = 0; i < ZMK_BHV_HOLD_TAP_MAX_HELD; ++i) { clear_hold_tap(&active_hold_taps[i]); }
    memset(captured_events, 0, sizeof(captured_events));
    undecided_hold_tap = NULL;
    last_tapped = (struct last_tapped){INT32_MIN, INT32_MIN};
    config.retro_tap = true;
    config.hold_behavior_dev = "mo";
    active_layer = held_mod = tap_down = tap_up = other_down = other_up = 0;
    seen_layer = seen_mod = -1;
}

int main(void) {
    behavior_hold_tap_init(&hold_device);
    /* Before/after timeout, local/remote keys, and several hold layers. */
    const int layers[] = {1, 2, 3, 7, 8};
    for (unsigned i = 0; i < sizeof(layers) / sizeof(layers[0]); ++i) {
        for (int expired = 0; expired <= 1; ++expired) {
            for (uint8_t source = 0; source <= 1; ++source) {
                reset();
                lt.param1 = layers[i];
                key(0, true, 1000, 0);
                if (expired) { timer(1200); assert(active_layer == 0); }
                key(1, true, expired ? 1300 : 1050, source);
                assert(other_down == 1 && seen_layer == layers[i]);
                assert(tap_down == 0);
                key(1, false, 1400, source);
                key(0, false, 1500, 0);
                assert(other_up == 1 && active_layer == 0 && tap_down == 0 && tap_up == 0);
            }
        }
    }
    /* Ctrl and Shift must affect the first other key, not the second. */
    for (int mod = 1; mod <= 2; ++mod) {
        for (int expired = 0; expired <= 1; ++expired) {
            reset(); config.hold_behavior_dev = "mod"; lt.param1 = mod;
            key(0, true, 2000, 0);
            if (expired) { timer(2200); }
            key(1, true, expired ? 2300 : 2050, 1);
            assert(other_down == 1 && seen_mod == mod && tap_down == 0);
            key(0, false, 2400, 0); /* Release LT/MT before the other key too. */
            key(1, false, 2500, 1);
            assert(held_mod == 0 && tap_down == 0 && other_up == 1);
        }
    }
    /* Short tap and long, un-interrupted Retro Tap still produce one space. */
    for (int expired = 0; expired <= 1; ++expired) {
        reset(); key(0, true, 3000, 0);
        if (expired) { timer(3200); }
        key(0, false, expired ? 3500 : 3050, 0);
        assert(tap_down == 1 && tap_up == 1 && active_layer == 0);
    }
    /* The dedicated no-Retro LT1 keeps its time-based hold. */
    reset(); config.retro_tap = false; lt.param1 = 1;
    key(0, true, 4000, 0); timer(4200);
    assert(active_layer == 1);
    key(0, false, 4300, 0);
    assert(active_layer == 0 && tap_down == 0);
    /* Pointer commits before/after the timer still pair hold press/release. */
    for (int expired = 0; expired <= 1; ++expired) {
        reset(); lt.param1 = 8; key(0, true, 5000, 0);
        if (expired) { timer(5200); }
        cline38_retro_lock();
        assert(cline38_retro_commit(expired ? 5300 : 5050));
        assert(!cline38_retro_commit(5400));
        cline38_retro_unlock();
        assert(active_layer == 8);
        key(0, false, 5500, 0);
        assert(active_layer == 0 && tap_down == 0);
    }
    puts("Pinned Hold-Tap + event manager: LT1/2/3/7/8, Ctrl/Shift, first key, taps and pointer passed");
}
