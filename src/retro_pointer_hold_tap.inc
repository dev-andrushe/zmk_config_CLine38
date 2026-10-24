/* SPDX-License-Identifier: MIT
 * Included in the generated upstream Hold-Tap translation unit. */
static bool cline38_retro_eligible(const struct active_hold_tap *hold) {
    return hold->config && hold->position != ZMK_BHV_HOLD_TAP_POSITION_NOT_USED && !hold->work_is_cancelled &&
           (hold->config->retro_tap
#if defined(CONFIG_CLINE38_CTRL_LAYER8) && CONFIG_CLINE38_CTRL_LAYER8
            ||
            (hold->status == STATUS_UNDECIDED &&
             cline38_ctrl8_hold_behavior(hold->config->hold_behavior_dev))
#endif
           ) &&
           (hold->status == STATUS_UNDECIDED || hold->status == STATUS_HOLD_TIMER);
}

bool cline38_retro_pending(void) {
    for (int i = 0; i < ZMK_BHV_HOLD_TAP_MAX_HELD; ++i) {
        if (cline38_retro_eligible(&active_hold_taps[i])) {
            return true;
        }
    }
    return false;
}

bool cline38_retro_commit(int64_t timestamp) {
    bool resolved = false;
    /* A decided TAP (e.g. quick-tap/prior-idle) must never become a hold.
     * Non-retro behaviors, including lt1, are intentionally untouched. */
    for (int i = 0; i < ZMK_BHV_HOLD_TAP_MAX_HELD; ++i) {
        struct active_hold_tap *hold = &active_hold_taps[i];
        if (!cline38_retro_eligible(hold) || hold->timestamp > timestamp) {
            continue;
        }
        hold->status = STATUS_HOLD_INTERRUPT;
        if (hold == undecided_hold_tap) {
            undecided_hold_tap = NULL;
        }
        press_binding(hold);
        resolved = true;
    }
    if (resolved) {
        release_captured_events();
    }
    return resolved;
}
