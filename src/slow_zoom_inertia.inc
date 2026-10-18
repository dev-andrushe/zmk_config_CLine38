/* SPDX-License-Identifier: MIT
 * Appended to the pinned upstream inertia processor, where to_idle is visible. */
#define CLINE38_CANCEL_INERTIA(n)                                                                  \
    to_idle(&scroll_inertia_data_##n);                                                              \
    scroll_inertia_data_##n.post_coast = false;                                                     \
    scroll_inertia_data_##n.last_event_time = 0;

void cline38_zoom_cancel_inertia(void) {
    DT_INST_FOREACH_STATUS_OKAY(CLINE38_CANCEL_INERTIA)
}
