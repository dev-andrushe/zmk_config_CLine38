/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT cline38_behavior_ctrl_layer8
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>
#include <dt-bindings/zmk/modifiers.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zmk/endpoints.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include "retro_pointer.h"
#include "ctrl_layer8.h"

LOG_MODULE_REGISTER(cline38_ctrl8, CONFIG_ZMK_LOG_LEVEL);

/* All entry points run under the existing recursive Retro Tap mutex. Only
 * this behavior owns this explicit Ctrl count; never mask the global Ctrl bit. */
static bool held, physical_down, ctrl_owned, layer_owned, transaction, ending;
static uint32_t hold_position;
static uint8_t hold_source;
static k_tid_t gesture_thread;
static unsigned gesture_depth;

/* Reserve room for releases. Under pressure drop a new press AND its release,
 * rather than losing a release of an already accepted key. */
#define DEFERRED_COUNT 256
#define RELEASE_RESERVE 64
struct deferred_event {
    bool keycode;
    union {
        struct zmk_position_state_changed position;
        struct zmk_keycode_state_changed key;
    } data;
};
static struct deferred_event deferred[DEFERRED_COUNT];
static size_t deferred_len;
struct dropped_press { uint32_t position; uint8_t source; bool used; };
static struct dropped_press dropped[RELEASE_RESERVE];

#if IS_ENABLED(CONFIG_ZMK_RUNTIME_MACRO)
bool cline38_ctrl8_runtime_busy(void);
#endif

static bool queued_actions(void) {
    return cline38_ctrl8_queue_busy()
#if IS_ENABLED(CONFIG_ZMK_RUNTIME_MACRO)
           || cline38_ctrl8_runtime_busy()
#endif
        ;
}

static int send_ctrl(bool down) {
    if (ctrl_owned != down) {
        if (down) { zmk_hid_register_mods(MOD_LCTL); }
        else { zmk_hid_unregister_mods(MOD_LCTL); }
        ctrl_owned = down;
    }
    return zmk_endpoint_send_report(HID_USAGE_KEY);
}

bool cline38_ctrl8_hold_behavior(const char *name) {
    return name && !strcmp(name, DEVICE_DT_NAME(DT_DRV_INST(0)));
}

static bool ordinary_key_down(void) {
    /* Do not lift Ctrl under a shortcut key already held by the PC, which
     * could otherwise auto-repeat as plain text during the gesture. */
    const struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
    for (size_t i = 0; i < sizeof(report->body.keys); ++i) {
        if (report->body.keys[i]) { return true; }
    }
    return false;
}

bool cline38_ctrl8_transaction_active(void) { return transaction; }

/* Called before the ordinary event dispatcher, so neither Hold-Tap, combos
 * nor keymap/HID can consume another key halfway through a gesture. */
bool cline38_ctrl8_capture(const zmk_event_t *event) {
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(event);
    const struct zmk_keycode_state_changed *key = as_zmk_keycode_state_changed(event);
    if (pos) {
        for (size_t i = 0; i < ARRAY_SIZE(dropped); ++i) {
            if (dropped[i].used && dropped[i].position == pos->position &&
                dropped[i].source == pos->source) {
                if (!pos->state) { dropped[i].used = false; }
                return true;
            }
        }
    }
    if (!transaction || (gesture_depth && gesture_thread == k_current_get())) { return false; }
    if (!pos && !key) { return false; }
    if (pos) {
        if (held && pos->position == hold_position && pos->source == hold_source) {
            physical_down = pos->state;
        }
        if (pos->state && deferred_len >= DEFERRED_COUNT - RELEASE_RESERVE) {
            for (size_t i = 0; i < ARRAY_SIZE(dropped); ++i) {
                if (!dropped[i].used) {
                    dropped[i] = (struct dropped_press){pos->position, pos->source, true};
                    LOG_WRN("Gesture input buffer busy: ignored one new key tap");
                    return true;
                }
            }
        }
    }
    if (deferred_len == DEFERRED_COUNT) {
        /* Only synthetic, unbounded event producers can exhaust the release
         * reserve; do not silently corrupt memory. Such producers are outside
         * the physical-key transaction contract. */
        LOG_ERR("Gesture deferred event buffer exhausted");
        return true;
    }
    struct deferred_event *out = &deferred[deferred_len++];
    out->keycode = key != NULL;
    if (key) { out->data.key = *key; }
    else { out->data.position = *pos; }
    return true;
}

static void finish_work(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(finish, finish_work);

bool cline38_ctrl8_begin(void) {
    if (transaction || queued_actions() || ordinary_key_down()) { return false; }
    transaction = true;
    ending = false;
    /* Enqueue Ctrl-up before any gesture key report (same keyboard FIFO on
     * BLE); an independent physical Ctrl count is deliberately retained. */
    if (send_ctrl(false) < 0) {
        transaction = false;
        send_ctrl(held && physical_down);
        return false;
    }
    return true;
}

int cline38_ctrl8_invoke(const struct zmk_behavior_binding *binding,
                         struct zmk_behavior_binding_event event, bool pressed) {
    cline38_retro_lock();
    bool gesture = transaction && event.position == CLINE38_ZOOM_POSITION;
    if (gesture) {
        gesture_thread = k_current_get();
        ++gesture_depth;
    }
    int ret = zmk_behavior_invoke_binding(binding, event, pressed);
    if (gesture) { --gesture_depth; }
    cline38_retro_unlock();
    return ret;
}

void cline38_ctrl8_end(void) {
    ending = true;
    k_work_reschedule(&finish, K_NO_WAIT);
}

static void finish_work(struct k_work *work) {
    ARG_UNUSED(work);
    cline38_retro_lock();
    if (!transaction || !ending) { cline38_retro_unlock(); return; }
    if (queued_actions()) {
        k_work_reschedule(&finish, K_MSEC(5));
        cline38_retro_unlock();
        return;
    }
    /* Root release and every queued macro release have completed. The owner
     * may have been physically released while its event was deferred. */
    send_ctrl(held && physical_down);
    transaction = ending = false;
    size_t count = deferred_len;
    deferred_len = 0;
    for (size_t i = 0; i < count; ++i) {
        if (deferred[i].keycode) { raise_zmk_keycode_state_changed(deferred[i].data.key); }
        else { raise_zmk_position_state_changed(deferred[i].data.position); }
    }
    cline38_retro_unlock();
}

static int ctrl8_pressed(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    if (binding->param1 != 8) { return -EINVAL; }
    cline38_retro_lock();
    if (held) { cline38_retro_unlock(); return -EBUSY; }
    held = physical_down = true;
    hold_position = event.position;
    hold_source = event.source;
    layer_owned = !zmk_keymap_layer_active(8);
    /* Activating the layer cancels old scroll inertia before Ctrl goes down. */
    zmk_keymap_layer_activate(8, false);
    if (!transaction) { send_ctrl(true); }
    cline38_retro_unlock();
    return ZMK_BEHAVIOR_OPAQUE;
}

static int ctrl8_released(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    cline38_retro_lock();
    if (held && event.position == hold_position && event.source == hold_source) {
        held = physical_down = false;
        if (layer_owned) { zmk_keymap_layer_deactivate(8, false); }
        layer_owned = false;
        send_ctrl(false);
    }
    cline38_retro_unlock();
    return ZMK_BEHAVIOR_OPAQUE;
}

static int ctrl8_endpoint_changed(const zmk_event_t *event) {
    ARG_UNUSED(event);
    cline38_retro_lock();
    /* Pinned hid.c clears report bytes but NOT explicit modifier counts.
     * Remove exactly our count; never resurrect the hold on a new endpoint. */
    physical_down = false;
    send_ctrl(false);
    cline38_retro_unlock();
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(cline38_ctrl8_endpoint, ctrl8_endpoint_changed);
ZMK_SUBSCRIPTION(cline38_ctrl8_endpoint, zmk_endpoint_changed);

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata layer_value[] = {
    {.display_name = "Layer 8", .value = 8, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
};
static const struct behavior_parameter_metadata_set param_set[] = {
    {.param1_values = layer_value, .param1_values_len = ARRAY_SIZE(layer_value)},
};
static const struct behavior_parameter_metadata metadata = {
    .sets = param_set, .sets_len = ARRAY_SIZE(param_set),
};
#endif
static const struct behavior_driver_api ctrl8_api = {
    .binding_pressed = ctrl8_pressed, .binding_released = ctrl8_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1);
BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &ctrl8_api);
