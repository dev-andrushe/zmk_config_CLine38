/* SPDX-License-Identifier: MIT
 * Deterministic host work queue; production behaviors and event dispatch are unchanged. */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define CLINE38_KERNEL_SHIM
#define CONFIG_CLINE38_CTRL_LAYER8 1
#define CONFIG_CLINE38_RETRO_POINTER 1
#define CONFIG_ZMK_RUNTIME_MACRO 1
#define CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE 64
#define CONFIG_ZMK_INPUT_PROCESSOR_SCROLL_INERTIA 0
#define CONFIG_ZMK_LOG_LEVEL 0
#define K_NO_WAIT 0
#define K_MSEC(x) (x)
#define CONTAINER_OF(ptr, type, field) ((type *)((char *)(ptr) - offsetof(type, field)))
struct k_work { void (*handler)(struct k_work *); };
struct k_work_delayable { struct k_work work; int64_t due; bool pending; };
static struct k_work_delayable *works[64];
static unsigned work_count;
static inline struct k_work_delayable *k_work_delayable_from_work(struct k_work *work) {
    return CONTAINER_OF(work, struct k_work_delayable, work);
}
static void remember_work(struct k_work_delayable *work) {
    for (unsigned i = 0; i < work_count; ++i) { if (works[i] == work) { return; } }
    assert(work_count < 64); works[work_count++] = work;
}
#include "hold_tap_shim.h"
static int64_t k_uptime_get(void) { return test_now; }
static void k_msleep(int ms) { test_now += ms; }
static void k_work_init_delayable(struct k_work_delayable *work, void (*handler)(struct k_work *)) {
    *work = (struct k_work_delayable){.work = {.handler = handler}};
    remember_work(work);
}
#define K_WORK_DELAYABLE_DEFINE(name, handler_fn) \
    struct k_work_delayable name = {.work = {.handler = handler_fn}}
static int k_work_schedule(struct k_work_delayable *work, int ms) {
    remember_work(work);
    if (!work->pending) { work->pending = true; work->due = test_now + ms; }
    return 0;
}
static int k_work_reschedule(struct k_work_delayable *work, int ms) {
    work->pending = false; return k_work_schedule(work, ms);
}
static int k_work_cancel_delayable(struct k_work_delayable *work) { work->pending = false; return 0; }
static bool k_work_delayable_is_pending(struct k_work_delayable *work) { return work->pending; }
static void advance(int64_t until) {
    for (unsigned guard = 0; guard < 10000; ++guard) {
        struct k_work_delayable *next = NULL;
        for (unsigned i = 0; i < work_count; ++i) {
            if (works[i]->pending && works[i]->due <= until &&
                (!next || works[i]->due < next->due)) { next = works[i]; }
        }
        if (!next) { test_now = until; return; }
        test_now = next->due; next->pending = false;
        next->work.handler(&next->work);
    }
    assert(!"work queue did not drain");
}
#define K_MUTEX_DEFINE(name) static int name
static void k_mutex_lock(int *mutex, int timeout) { ++*mutex; }
static void k_mutex_unlock(int *mutex) { assert(*mutex > 0); --*mutex; }
typedef int atomic_t;
static int atomic_get(atomic_t *value) { return *value; }
static void atomic_inc(atomic_t *value) { ++*value; }
typedef int k_tid_t;
static k_tid_t k_current_get(void) { return 1; }
struct k_msgq { unsigned char *bytes; size_t size, capacity, length, head; };
#define K_MSGQ_DEFINE(name, item_size, count, align) \
    static unsigned char name##_bytes[(item_size) * (count)]; \
    static struct k_msgq name = {.bytes = name##_bytes, .size = item_size, .capacity = count}
static int k_msgq_get(struct k_msgq *q, void *out, int timeout) {
    if (!q->length) { return -1; }
    memcpy(out, q->bytes + q->head * q->size, q->size);
    q->head = (q->head + 1) % q->capacity; --q->length; return 0;
}
static int k_msgq_put(struct k_msgq *q, const void *item, int timeout) {
    if (q->length == q->capacity) { return -1; }
    memcpy(q->bytes + ((q->head + q->length) % q->capacity) * q->size, item, q->size);
    ++q->length; return 0;
}
static unsigned k_msgq_num_used_get(struct k_msgq *q) { return q->length; }
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ARG_UNUSED(x) (void)(x)
#define LOG_MODULE_REGISTER(...)
#define BUILD_ASSERT(x) _Static_assert(x, #x)
#define DT_NUM_INST_STATUS_OKAY(x) 1
#define DT_DRV_INST(x) x
#define DEVICE_DT_NAME(x) "ctrl8_hold"
#define BEHAVIOR_DT_INST_DEFINE(...)
#define MOD_LCTL 1
#define HID_USAGE_KEY 7
#define HID_USAGE_CONSUMER 12
#define ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL 255
struct zmk_endpoint_changed { int endpoint; };
ZMK_EVENT_DECLARE(zmk_endpoint_changed);
static bool layers[16];
static bool zmk_keymap_layer_active(int layer) { return layers[layer]; }
static int zmk_keymap_layer_activate(int layer, bool locking) { layers[layer] = true; return 0; }
static int zmk_keymap_layer_deactivate(int layer, bool locking) { layers[layer] = false; return 0; }
static int mod_counts[8], explicit_mods, implicit_mods;
static int zmk_hid_register_mods(int flags) {
    for (int i = 0; i < 8; ++i) { if (flags & (1 << i)) { ++mod_counts[i]; explicit_mods |= 1 << i; } }
    return 0;
}
static int zmk_hid_unregister_mods(int flags) {
    for (int i = 0; i < 8; ++i) { if (flags & (1 << i)) {
        assert(mod_counts[i] > 0);
        if (!--mod_counts[i]) { explicit_mods &= ~(1 << i); }
    } }
    return 0;
}
struct report { int mods, key; };
static struct report reports[2048];
static unsigned report_count;
static int down_key, report_error;
struct zmk_hid_keyboard_report { struct { unsigned char keys[6]; } body; };
static struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void) {
    static struct zmk_hid_keyboard_report report;
    memset(&report, 0, sizeof(report)); report.body.keys[0] = down_key;
    return &report;
}
static int zmk_endpoint_send_report(int page) {
    assert(report_count < ARRAY_SIZE(reports));
    reports[report_count++] = (struct report){explicit_mods | implicit_mods, down_key};
    return report_error;
}
