/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_input_processor_mixed_scroll

#include <limits.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <drivers/input_processor.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/input_processor_layer_router.h>
#include <zmk/keymap.h>
#include <zmk/pointing/input_processor_runtime.h>

#include "mixed_scroll_logic.h"
#include "reference_scroll_logic.h"

LOG_MODULE_REGISTER(cline38_mixed_scroll, CONFIG_ZMK_LOG_LEVEL);

struct mixed_device_config {
    struct mixed_config motion;
    uint8_t route_index;
    struct zmk_behavior_binding left, right;
    const struct device *scroll_reference;
    uint32_t default_multiplier, default_divisor;
};

struct mixed_device_data {
    const struct device *dev;
    struct mixed_state motion;
    struct reference_scroll_state scale;
    int32_t frame_x, frame_y;
    uint32_t route_mask, active_mask;
    atomic_val_t epoch;
    struct k_work_delayable tap_work;
    struct zmk_behavior_binding tap_binding;
    atomic_t tap_busy;
    atomic_val_t tap_epoch;
    bool tap_pressed;
};

/* Layer events may run on a different thread. Only the input thread touches
 * motion state; the epoch invalidates even an off/on cycle between reports. */
static atomic_t layer_epoch;

static int layer_changed(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    atomic_inc(&layer_epoch);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(cline38_mixed_scroll, layer_changed);
ZMK_SUBSCRIPTION(cline38_mixed_scroll, zmk_layer_state_changed);

static uint32_t active_positions(uint32_t mask) {
    uint32_t active = 0;
    for (uint8_t i = 0; i < 32; i++) {
        if (!(mask & BIT(i))) {
            continue;
        }
        zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(i);
        if (id != ZMK_KEYMAP_LAYER_ID_INVAL && zmk_keymap_layer_active(id)) {
            active |= BIT(i);
        }
    }
    return active;
}

/* A paired press/release is owned by one work item. Do not queue two separate
 * behavior-queue items: if the second enqueue failed, a modifier could stick. */
static void tap_handler(struct k_work *work) {
    struct mixed_device_data *data =
        CONTAINER_OF(k_work_delayable_from_work(work), struct mixed_device_data, tap_work);
    struct zmk_behavior_binding_event ev = {
        .position = INT32_MAX - 1,
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };
    if (!data->tap_pressed) {
        const struct mixed_device_config *cfg = data->dev->config;
        if (data->tap_epoch != atomic_get(&layer_epoch) ||
            !active_positions(zmk_layer_router_get_layers(cfg->route_index))) {
            atomic_clear(&data->tap_busy);
            return; // A queued navigation must not fire after the mode was released.
        }
        zmk_behavior_invoke_binding(&data->tap_binding, ev, true);
        data->tap_pressed = true;
        k_work_schedule(&data->tap_work, K_MSEC(20));
    } else {
        zmk_behavior_invoke_binding(&data->tap_binding, ev, false);
        data->tap_pressed = false;
        atomic_clear(&data->tap_busy);
    }
}

static void fire_navigation(struct mixed_device_data *data,
                            const struct mixed_device_config *cfg, int direction) {
    if (!atomic_cas(&data->tap_busy, 0, 1)) {
        return;
    }
    data->tap_binding = direction < 0 ? cfg->left : cfg->right;
    data->tap_epoch = data->epoch;
    k_work_schedule(&data->tap_work, K_NO_WAIT);
}

static int mixed_handle_event(const struct device *dev, struct input_event *event,
                              uint32_t param1, uint32_t param2,
                              struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);
    const struct mixed_device_config *cfg = dev->config;
    struct mixed_device_data *data = dev->data;
    uint32_t mask = zmk_layer_router_get_layers(cfg->route_index);
    uint32_t active = active_positions(mask);
    atomic_val_t epoch = atomic_get(&layer_epoch);
    if (!active || data->epoch != epoch || data->route_mask != mask ||
        data->active_mask != active) {
        mixed_reset(&data->motion);
        data->scale = (struct reference_scroll_state){0};
        data->frame_x = data->frame_y = 0;
        data->epoch = epoch;
        data->route_mask = mask;
        data->active_mask = active;
    }
    if (!active) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    bool xy = event->type == INPUT_EV_REL &&
              (event->code == INPUT_REL_X || event->code == INPUT_REL_Y);
    if (xy) {
        if (event->code == INPUT_REL_X) {
            data->frame_x = mixed_add(data->frame_x, event->value);
        } else {
            data->frame_y = mixed_add(data->frame_y, event->value);
        }
    }
    if (event->sync) {
        /* Match CLine38's existing X inversion and scroll Y inversion.
         * Direction is chosen after the complete X/Y frame, avoiding X-first bias. */
        struct mixed_result result = mixed_step(&data->motion, &cfg->motion,
                                                -data->frame_x, -data->frame_y,
                                                k_uptime_get());
        data->frame_x = data->frame_y = 0;
        if (result.scroll != 0) {
            struct zmk_input_processor_runtime_config reference = {
                .scale_multiplier = cfg->default_multiplier,
                .scale_divisor = cfg->default_divisor,
            };
            zmk_input_processor_runtime_get_config(cfg->scroll_reference, NULL, &reference);
            result.scroll = reference_scroll_step(&data->scale, result.scroll,
                reference.scale_multiplier, reference.scale_divisor,
                cfg->default_multiplier, cfg->default_divisor, reference.y_invert);
        }
        if (result.scroll != 0) {
            /* Separate device/listener: mouse scaling and gesture recognition
             * cannot rescale or consume this wheel output. Layer 3's scale has
             * already been applied exactly once. Never block the input
             * thread trying to enqueue to its own queue. No inertia is attached. */
            int err = input_report_rel(dev, INPUT_REL_WHEEL, result.scroll, true, K_NO_WAIT);
            if (err < 0) {
                LOG_WRN("Mixed scroll report dropped: %d", err);
            }
        }
        if (result.direction) {
            fire_navigation(data, cfg, result.direction);
        }
    }
    if (xy) {
        /* Preserve sync so the original listener can flush prior data. A zero
         * WHEEL event bypasses both the mouse scaler and all XY recognizers. */
        event->code = INPUT_REL_WHEEL;
        event->value = 0;
    }
    return ZMK_INPUT_PROC_CONTINUE;
}

static int mixed_init(const struct device *dev) {
    struct mixed_device_data *data = dev->data;
    data->dev = dev;
    k_work_init_delayable(&data->tap_work, tap_handler);
    return 0;
}

static const struct zmk_input_processor_driver_api mixed_api = {
    .handle_event = mixed_handle_event,
};

#define MIXED_INST(n)                                                                              \
    BUILD_ASSERT(DT_PROP(DT_INST_PHANDLE(n, scroll_reference), scale_multiplier) > 0);            \
    BUILD_ASSERT(DT_PROP(DT_INST_PHANDLE(n, scroll_reference), scale_divisor) > 0);               \
    BUILD_ASSERT(DT_INST_PROP(n, direction_threshold) > 0);                                       \
    BUILD_ASSERT(DT_INST_PROP(n, gesture_threshold) >= DT_INST_PROP(n, direction_threshold));     \
    BUILD_ASSERT(DT_INST_PROP(n, diagonal_threshold) >= DT_INST_PROP(n, direction_threshold));    \
    BUILD_ASSERT(DT_INST_PROP(n, idle_ms) > 20);                                                   \
    static struct mixed_device_data mixed_data_##n;                                               \
    static const struct mixed_device_config mixed_config_##n = {                                  \
        .motion = {.direction_threshold = DT_INST_PROP(n, direction_threshold),                  \
                   .gesture_threshold = DT_INST_PROP(n, gesture_threshold),                      \
                   .diagonal_threshold = DT_INST_PROP(n, diagonal_threshold),                    \
                   .idle_ms = DT_INST_PROP(n, idle_ms)},                                          \
        .route_index = DT_INST_PROP(n, route_index),                                              \
        .left = ZMK_KEYMAP_EXTRACT_BINDING(0, DT_DRV_INST(n)),                                    \
        .right = ZMK_KEYMAP_EXTRACT_BINDING(1, DT_DRV_INST(n)),                                   \
        .scroll_reference = DEVICE_DT_GET(DT_INST_PHANDLE(n, scroll_reference)),                  \
        .default_multiplier = DT_PROP(DT_INST_PHANDLE(n, scroll_reference), scale_multiplier),    \
        .default_divisor = DT_PROP(DT_INST_PHANDLE(n, scroll_reference), scale_divisor),            \
    };                                                                                            \
    DEVICE_DT_INST_DEFINE(n, mixed_init, NULL, &mixed_data_##n, &mixed_config_##n, POST_KERNEL,     \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &mixed_api);

DT_INST_FOREACH_STATUS_OKAY(MIXED_INST)
