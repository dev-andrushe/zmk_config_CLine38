/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include "ctrl8_kernel_shim.h"
#include "behavior_hold_tap.c"
#include "event_manager.c"
#include "behavior_queue.c"
#undef DT_DRV_COMPAT
#include "behavior_ctrl_layer8.c"
ZMK_EVENT_IMPL(zmk_position_state_changed);
ZMK_EVENT_IMPL(zmk_keycode_state_changed);
ZMK_EVENT_IMPL(zmk_endpoint_changed);

static bool runtime_busy;
bool cline38_ctrl8_runtime_busy(void) { return runtime_busy; }
static struct behavior_hold_tap_config config = {
    .tapping_term_ms = 200, .hold_behavior_dev = "ctrl8_hold", .tap_behavior_dev = "kp",
    .flavor = FLAVOR_HOLD_PREFERRED, .retro_tap = false,
};
static const struct device hold_device = {.config = &config};
static struct zmk_behavior_binding combo_binding = {.behavior_dev = "ctrl8", .param1 = 8, .param2 = 4};
static unsigned tap_a;
static int first_c_mods;

const struct device *zmk_behavior_get_binding(const char *name) { return &hold_device; }

static int hid_event(const zmk_event_t *event) {
    const struct zmk_keycode_state_changed *key = as_zmk_keycode_state_changed(event);
    if (!key) { return 0; }
    if (key->keycode == 0xe0) {
        if (key->state) { zmk_hid_register_mods(1); } else { zmk_hid_unregister_mods(1); }
    } else { down_key = key->state ? (int)key->keycode : 0; }
    implicit_mods = key->state ? key->implicit_modifiers : 0;
    if (key->state && key->keycode == 4) { ++tap_a; }
    if (key->state && key->keycode == 6) { first_c_mods = explicit_mods | implicit_mods; }
    zmk_endpoint_send_report(key->usage_page);
    return 0;
}
ZMK_LISTENER(hid, hid_event);
ZMK_SUBSCRIPTION(hid, zmk_keycode_state_changed);

int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event, bool pressed) {
    if (!strcmp(binding->behavior_dev, "ctrl8_hold")) {
        struct zmk_behavior_binding copy = *binding;
        return pressed ? ctrl8_pressed(&copy, event) : ctrl8_released(&copy, event);
    }
    if (!strcmp(binding->behavior_dev, "macro")) {
        if (pressed) {
            /* Gesture's OWN Ctrl is represented by implicit modifiers, like LC(C).
             * Release is deliberately much later than the root 20ms tap. */
            struct zmk_behavior_binding action = {.behavior_dev = "kp", .param1 = (1u << 24) | 6};
            assert(zmk_behavior_queue_add(&event, action, true, 80) == 0);
            assert(zmk_behavior_queue_add(&event, action, false, 0) == 0);
        }
        return 0;
    }
    if (!strcmp(binding->behavior_dev, "none")) { return 0; }
    return raise_zmk_keycode_state_changed((struct zmk_keycode_state_changed){
        .usage_page = HID_USAGE_KEY, .keycode = binding->param1 & 0xffff,
        .implicit_modifiers = binding->param1 >> 24, .state = pressed, .timestamp = test_now,
    });
}

static int keymap_event(const zmk_event_t *event) {
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(event);
    if (!pos) { return 0; }
    struct zmk_behavior_binding_event ev = {
        .position = pos->position, .source = pos->source, .timestamp = pos->timestamp,
    };
    if (pos->position == 0) {
        return pos->state ? on_hold_tap_binding_pressed(&combo_binding, ev) :
                            on_hold_tap_binding_released(&combo_binding, ev);
    }
    struct zmk_behavior_binding key = {.behavior_dev = "kp", .param1 = pos->position == 1 ? 6 : 25};
    return zmk_behavior_invoke_binding(&key, ev, pos->state);
}
ZMK_LISTENER(keymap, keymap_event);
ZMK_SUBSCRIPTION(keymap, zmk_position_state_changed);

static void key(int position, bool state, int64_t now) {
    advance(now);
    raise_zmk_position_state_changed((struct zmk_position_state_changed){
        .position = position, .source = 255, .state = state, .timestamp = now,
    });
}
static struct zmk_behavior_binding_event gesture_event(void) {
    return (struct zmk_behavior_binding_event){.position = CLINE38_ZOOM_POSITION, .source = 255,
                                               .timestamp = test_now};
}
static void gesture(bool macro, int code) {
    struct zmk_behavior_binding binding = {.behavior_dev = macro ? "macro" : "kp", .param1 = code};
    assert(cline38_ctrl8_begin());
    cline38_ctrl8_invoke(&binding, gesture_event(), true);
    advance(test_now + 20);
    cline38_ctrl8_invoke(&binding, gesture_event(), false);
    cline38_ctrl8_end();
    advance(test_now + 200);
}
static void clean(void) {
    assert(!held && !transaction && !ctrl_owned && !layers[8]);
    assert(mod_counts[0] == 0 && down_key == 0 && tap_a == 0);
    assert(cline38_retro_mutex == 0);
}

int main(void) {
    behavior_hold_tap_init(&hold_device);
    key(0, true, 1000); key(0, false, 1050);
    assert(tap_a == 1 && !layers[8] && explicit_mods == 0);
    tap_a = 0;
    /* Hold-preferred: first C before 200ms already sees Ctrl, layer8 transparent. */
    key(0, true, 2000); key(1, true, 2050);
    assert(layers[8] && first_c_mods == 1);
    key(1, false, 2060); key(2, true, 2070);
    assert(down_key == 25 && (explicit_mods & 1));
    key(2, false, 2080); key(0, false, 2090); clean();
    /* Timer-only hold has no RetroTap, even when no other key was pressed. */
    key(0, true, 3000); advance(3200);
    assert(layers[8] && mod_counts[0] == 1);
    key(0, false, 3250); clean();
    /* Pointer resolves the SAME dedicated no-retro hold before timeout. */
    key(0, true, 4000);
    assert(cline38_retro_pending());
    cline38_retro_lock(); assert(cline38_retro_commit(4010)); cline38_retro_unlock();
    assert(layers[8] && mod_counts[0] == 1);
    for (int code = 0x91; code >= 0x90; --code) { /* LANG2 / LANG1 */
        unsigned before = report_count;
        gesture(false, code);
        assert(reports[before].mods == 0 && reports[before].key == 0);
        assert(reports[before + 1].mods == 0 && reports[before + 1].key == code);
        assert(reports[before + 2].mods == 0 && reports[before + 2].key == 0);
        assert(reports[before + 3].mods == 1 && reports[before + 3].key == 0);
    }
    key(1, true, 4500); assert(first_c_mods == 1); key(1, false, 4510);
    key(0, false, 4520); clean();
    /* Physical Ctrl, Shift, Alt and GUI remain. Gesture's own Ctrl also remains. */
    key(0, true, 5000); advance(5200);
    zmk_hid_register_mods(1 | 2 | 4 | 8);
    unsigned before = report_count; gesture(false, 0x91);
    assert(reports[before + 1].mods == 15 && mod_counts[0] == 2);
    zmk_hid_unregister_mods(1 | 2 | 4 | 8);
    before = report_count; gesture(true, 0);
    assert(reports[before].mods == 0);
    assert(reports[before + 1].mods == 1 && reports[before + 1].key == 6);
    assert(reports[before + 2].mods == 0 && reports[before + 2].key == 0);
    assert(reports[before + 3].mods == 1);
    key(0, false, 5700); clean();
    /* Release owner DURING a gesture. Root release never restores its Ctrl.
     * A concurrent ordinary key waits until the complete gesture key-up. */
    key(0, true, 6000); advance(6200); assert(cline38_ctrl8_begin());
    struct zmk_behavior_binding lang = {.behavior_dev = "kp", .param1 = 0x90};
    cline38_ctrl8_invoke(&lang, gesture_event(), true);
    key(0, false, 6205); key(1, true, 6206); key(1, false, 6207);
    assert(down_key == 0x90 && deferred_len == 3);
    cline38_ctrl8_invoke(&lang, gesture_event(), false);
    cline38_ctrl8_end(); advance(6230);
    assert(first_c_mods == 0); clean();
    /* Deferred DYA macro startup: do not restore before its work and queue finish. */
    key(0, true, 7000); advance(7200); assert(cline38_ctrl8_begin());
    runtime_busy = true; cline38_ctrl8_end(); advance(7240);
    assert(transaction && mod_counts[0] == 0);
    runtime_busy = false; advance(7250);
    assert(!transaction && mod_counts[0] == 1);
    key(0, false, 7260); clean();
    /* Endpoint switch and a failed Ctrl-up report must never leak our count. */
    key(0, true, 8000); advance(8200);
    raise_zmk_endpoint_changed((struct zmk_endpoint_changed){0});
    assert(mod_counts[0] == 0);
    key(0, false, 8210); clean();
    key(0, true, 9000); advance(9200); report_error = -1;
    assert(!cline38_ctrl8_begin() && !transaction && mod_counts[0] == 1);
    report_error = 0; key(0, false, 9210); clean();
    /* Never lift Ctrl under an already held shortcut key; replay a key that
     * arrived during the gesture with Ctrl restored before its first report. */
    key(0, true, 10000); key(1, true, 10020);
    assert(!cline38_ctrl8_begin() && mod_counts[0] == 1);
    key(1, false, 10030);
    assert(cline38_ctrl8_begin());
    key(1, true, 10031); key(1, false, 10032);
    cline38_ctrl8_end(); advance(10040);
    assert(first_c_mods == 1 && !transaction);
    key(0, false, 10050); clean();
    /* An existing MO8 and another active layer survive ownership release. */
    layers[3] = layers[8] = true;
    key(0, true, 11000); advance(11200); key(0, false, 11210);
    assert(layers[3] && layers[8] && mod_counts[0] == 0);
    layers[3] = layers[8] = false;
    clean();
    puts("Ctrl8: A/C/V, no-retro timer, immediate pointer, LANG2/LANG1 report order, independent modifiers,");
    puts("queued macro completion, owner release, deferred keys, endpoint change and transport failure passed");
}
