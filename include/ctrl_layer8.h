/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>

#define CLINE38_ZOOM_POSITION (INT32_MAX - 2)

#if IS_ENABLED(CONFIG_CLINE38_CTRL_LAYER8)
bool cline38_ctrl8_hold_behavior(const char *name);
bool cline38_ctrl8_capture(const zmk_event_t *event);
bool cline38_ctrl8_transaction_active(void);
bool cline38_ctrl8_begin(void);
void cline38_ctrl8_end(void);
int cline38_ctrl8_invoke(const struct zmk_behavior_binding *binding,
                         struct zmk_behavior_binding_event event, bool pressed);
bool cline38_ctrl8_queue_busy(void);
#else
static inline bool cline38_ctrl8_hold_behavior(const char *name) { return false; }
static inline bool cline38_ctrl8_transaction_active(void) { return false; }
#endif
