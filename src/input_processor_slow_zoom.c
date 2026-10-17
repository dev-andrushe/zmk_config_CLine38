/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_input_processor_slow_zoom

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/input_processor.h>
#include <dt-bindings/zmk/modifiers.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#if IS_ENABLED(CONFIG_CLINE38_RETRO_POINTER)
#include "retro_pointer.h"
#endif
#include "slow_zoom_logic.h"

LOG_MODULE_REGISTER(cline38_slow_zoom, CONFIG_ZMK_LOG_LEVEL);
K_MUTEX_DEFINE(slow_zoom_mutex);

struct slow_zoom_device_config {
    struct slow_zoom_config rate;
    uint8_t layer;
};
struct slow_zoom_device_data {
    struct slow_zoom_state motion;
    bool active, ctrl_owned, ctrl_ready;
};

static void zoom_lock(void) {
#if IS_ENABLED(CONFIG_CLINE38_RETRO_POINTER)
    cline38_retro_lock();
#endif
    k_mutex_lock(&slow_zoom_mutex, K_FOREVER);
}
static void zoom_unlock(void) {
    k_mutex_unlock(&slow_zoom_mutex);
#if IS_ENABLED(CONFIG_CLINE38_RETRO_POINTER)
    cline38_retro_unlock();
#endif
}

static void zoom_release_ctrl(struct slow_zoom_device_data *data) {
    if (data->ctrl_owned) {
        zmk_hid_unregister_mods(MOD_LCTL);
        data->ctrl_owned = false;
        int err = zmk_endpoint_send_report(HID_USAGE_KEY);
        if (err < 0) {
            LOG_WRN("Zoom Ctrl release report failed: %d", err);
        }
    }
    data->ctrl_ready = false;
}

static void zoom_sync_layer(const struct device *dev) {
    const struct slow_zoom_device_config *cfg = dev->config;
    struct slow_zoom_device_data *data = dev->data;
    bool active = zmk_keymap_layer_active(cfg->layer);
    if (data->active == active) {
        return;
    }
    data->active = active;
    slow_zoom_reset(&data->motion, k_uptime_get());
    zoom_release_ctrl(data);
}

static int zoom_layer_changed(const zmk_event_t *event) {
    ARG_UNUSED(event);
    zoom_lock();
    zoom_sync_layer(DEVICE_DT_INST_GET(0));
    zoom_unlock();
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(cline38_slow_zoom, zoom_layer_changed);
ZMK_SUBSCRIPTION(cline38_slow_zoom, zmk_layer_state_changed);

static int zoom_endpoint_changed(const zmk_event_t *event) {
    ARG_UNUSED(event);
    zoom_lock();
    /* endpoints.c cleared all HID counts before this event. Forget ownership
     * without unregistering a count that no longer exists on the new endpoint. */
    struct slow_zoom_device_data *data = DEVICE_DT_INST_GET(0)->data;
    data->ctrl_owned = data->ctrl_ready = false;
    slow_zoom_reset(&data->motion, k_uptime_get());
    zoom_unlock();
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(cline38_zoom_endpoint, zoom_endpoint_changed);
ZMK_SUBSCRIPTION(cline38_zoom_endpoint, zmk_endpoint_changed);

static int zoom_handle_event(const struct device *dev, struct input_event *event,
                              uint32_t param1, uint32_t param2,
                              struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);
    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }
    zoom_lock();
    zoom_sync_layer(dev);
    const struct slow_zoom_device_config *cfg = dev->config;
    struct slow_zoom_device_data *data = dev->data;
    if (!data->active) {
        zoom_unlock();
        return ZMK_INPUT_PROC_CONTINUE;
    }
    if (event->code == INPUT_REL_X) {
        data->motion.frame_x = mixed_add(data->motion.frame_x, event->value);
    } else {
        data->motion.frame_y = mixed_add(data->motion.frame_y, event->value);
    }
    event->code = INPUT_REL_WHEEL;
    event->value = 0; /* Preserve sync without leaking XY or old up/down actions. */
    if (event->sync) {
        int32_t x = data->motion.frame_x, y = data->motion.frame_y;
        data->motion.frame_x = data->motion.frame_y = 0;
        struct slow_zoom_result result = slow_zoom_step(&data->motion, &cfg->rate,
                                                       x, -y, k_uptime_get());
        if (data->motion.direction.mode != MIXED_VERTICAL) {
            zoom_release_ctrl(data); /* Horizontal bindings keep their own modifiers. */
        } else if (!data->ctrl_owned) {
            /* Explicit counts preserve a physical Ctrl; do not overwrite
             * the global implicit-modifier state used by shortcut bindings. */
            zmk_hid_register_mods(MOD_LCTL);
            data->ctrl_owned = true;
            data->ctrl_ready = zmk_endpoint_send_report(HID_USAGE_KEY) >= 0;
        } else if (!data->ctrl_ready) {
            data->ctrl_ready = zmk_endpoint_send_report(HID_USAGE_KEY) >= 0;
        }
        if (result.zoom && data->ctrl_ready) {
            zmk_hid_mouse_movement_set(0, 0);
            zmk_hid_mouse_scroll_set(0, result.zoom);
            int err = zmk_endpoint_send_mouse_report();
            zmk_hid_mouse_scroll_set(0, 0);
            if (err < 0) {
                LOG_WRN("Zoom wheel report failed: %d", err);
            }
        }
        if (result.horizontal) {
            event->code = INPUT_REL_X;
            event->value = result.horizontal;
        }
    }
    zoom_unlock();
    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api zoom_api = {.handle_event = zoom_handle_event};

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1);
#define ZOOM_INST(n)                                                                               \
    BUILD_ASSERT(DT_INST_PROP(n, counts_per_step) > 0 && DT_INST_PROP(n, counts_per_step) < 32767);  \
    BUILD_ASSERT(DT_INST_PROP(n, min_interval_ms) > 0);                                            \
    BUILD_ASSERT(DT_INST_PROP(n, ctrl_settle_ms) >= 0);                                            \
    static struct slow_zoom_device_data zoom_data_##n;                                             \
    static const struct slow_zoom_device_config zoom_config_##n = {                                \
        .layer = DT_INST_PROP(n, layer),                                                           \
        .rate = {.counts_per_step = DT_INST_PROP(n, counts_per_step),                              \
                 .min_interval_ms = DT_INST_PROP(n, min_interval_ms),                              \
                 .ctrl_settle_ms = DT_INST_PROP(n, ctrl_settle_ms)},                               \
    };                                                                                            \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &zoom_data_##n, &zoom_config_##n, POST_KERNEL,              \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &zoom_api);
DT_INST_FOREACH_STATUS_OKAY(ZOOM_INST)
