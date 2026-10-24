/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct scroll_inertia_data {
    bool scheduled, stop_scheduled, post_coast;
    int64_t last_event_time;
};
static struct scroll_inertia_data scroll_inertia_data_0;
static int cancelled;
static void to_idle(struct scroll_inertia_data *data) {
    data->scheduled = data->stop_scheduled = false;
    ++cancelled;
}
#define DT_INST_FOREACH_STATUS_OKAY(fn) fn(0)
#include "../src/slow_zoom_inertia.inc"

int main(void) {
    scroll_inertia_data_0 = (struct scroll_inertia_data){true, true, true, 100};
    cline38_zoom_cancel_inertia();
    assert(cancelled == 1);
    assert(!scroll_inertia_data_0.scheduled && !scroll_inertia_data_0.stop_scheduled);
    assert(!scroll_inertia_data_0.post_coast && scroll_inertia_data_0.last_event_time == 0);
    cline38_zoom_cancel_inertia(); /* Idempotent even across a sub-tick layer toggle. */
    assert(!scroll_inertia_data_0.scheduled && !scroll_inertia_data_0.post_coast);
    puts("slow zoom: pending inertia and previous-gesture credit cancelled");
}
