/* SPDX-License-Identifier: MIT
 * Host-only kernel/device/HID shim. Hold-Tap and event dispatch stay upstream. */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>

#define CONFIG_ZMK_BEHAVIOR_HOLD_TAP_MAX_HELD 10
#define CONFIG_ZMK_BEHAVIOR_HOLD_TAP_MAX_CAPTURED_EVENTS 40
#define CONFIG_ZMK_BEHAVIOR_METADATA 0
#ifndef CONFIG_CLINE38_CTRL_LAYER8
#define CONFIG_CLINE38_CTRL_LAYER8 0
#endif
#define CONFIG_ZMK_SPLIT 1
#define CONFIG_ZMK_SPLIT_RELAY_EVENT 0
#define IS_ENABLED(x) (x)
#define DT_HAS_COMPAT_STATUS_OKAY(x) 1
#define DT_INST_FOREACH_STATUS_OKAY(x)
#define LOG_MODULE_DECLARE(...)
#define LOG_DBG(...)
#define LOG_WRN(...)
#define LOG_ERR(...)
#define K_FOREVER -1
#define K_MSEC(x) (x)
#define CONTAINER_OF(ptr, type, field) ((type *)((char *)(ptr) - offsetof(type, field)))
#define STRINGIFY(x) #x
#define _CONCAT_INNER(a, b) a##b
#define _CONCAT(a, b) _CONCAT_INNER(a, b)
#define __used __attribute__((used))
#define Z_DECL_ALIGN(type) type __attribute__((aligned(_Alignof(type))))

static int64_t test_now;
#ifndef CLINE38_KERNEL_SHIM
struct k_work { void (*handler)(struct k_work *); };
struct k_work_delayable { struct k_work work; };
static inline struct k_work_delayable *k_work_delayable_from_work(struct k_work *work) {
    return CONTAINER_OF(work, struct k_work_delayable, work);
}
static inline int64_t k_uptime_get(void) { return test_now; }
static inline void k_msleep(int ms) { test_now += ms; }
static inline int k_work_schedule(struct k_work_delayable *work, int ms) { return 0; }
static inline int k_work_cancel_delayable(struct k_work_delayable *work) { return 0; }
static inline void k_work_init_delayable(struct k_work_delayable *work,
                                         void (*handler)(struct k_work *)) {
    work->work.handler = handler;
}
#define K_MUTEX_DEFINE(name) static int name
static inline void k_mutex_lock(int *mutex, int timeout) { ++*mutex; }
static inline void k_mutex_unlock(int *mutex) { assert(*mutex > 0); --*mutex; }
typedef int atomic_t;
static inline int atomic_get(atomic_t *value) { return *value; }
static inline void atomic_inc(atomic_t *value) { ++*value; }
#endif

#include "upstream_event_manager.h"
struct zmk_position_state_changed {
    uint8_t source;
    uint32_t position;
    bool state;
    int64_t timestamp;
};
struct zmk_keycode_state_changed {
    uint16_t usage_page;
    uint32_t keycode;
    uint8_t implicit_modifiers, explicit_modifiers;
    bool state;
    int64_t timestamp;
};
ZMK_EVENT_DECLARE(zmk_position_state_changed);
ZMK_EVENT_DECLARE(zmk_keycode_state_changed);
static inline bool is_mod(uint16_t page, uint32_t key) { return key >= 0xe0 && key <= 0xe7; }
struct device { const void *config; void *data; };
struct zmk_behavior_binding { char *behavior_dev; uint32_t param1, param2; };
struct zmk_behavior_binding_event { uint8_t source; uint32_t position; int64_t timestamp; };
struct behavior_driver_api {
    int (*binding_pressed)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
    int (*binding_released)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
};
#define ZMK_BEHAVIOR_OPAQUE 0
const struct device *zmk_behavior_get_binding(const char *name);
int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event, bool pressed);
