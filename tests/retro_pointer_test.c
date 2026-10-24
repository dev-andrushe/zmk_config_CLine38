/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "../src/retro_pointer_logic.h"

/* Minimal behavior boundary; the state transition below is the production
 * Hold-Tap extension, not a separate reimplementation of it. */
enum status { STATUS_UNDECIDED, STATUS_TAP, STATUS_HOLD_INTERRUPT, STATUS_HOLD_TIMER };
struct behavior_hold_tap_config { bool retro_tap; };
struct active_hold_tap {
    int32_t position;
    bool work_is_cancelled;
    const struct behavior_hold_tap_config *config;
    enum status status;
    int64_t timestamp;
};
#define ZMK_BHV_HOLD_TAP_MAX_HELD 8
#define ZMK_BHV_HOLD_TAP_POSITION_NOT_USED 9999
static struct active_hold_tap active_hold_taps[ZMK_BHV_HOLD_TAP_MAX_HELD];
static struct active_hold_tap *undecided_hold_tap;
static int pressed[ZMK_BHV_HOLD_TAP_MAX_HELD], captures_released;
static void press_binding(struct active_hold_tap *hold) {
    assert(hold->status == STATUS_HOLD_INTERRUPT);
    ++pressed[hold - active_hold_taps];
}
static void release_captured_events(void) { ++captures_released; }
#include "../src/retro_pointer_hold_tap.inc"

static void test_hold_resolution(void) {
    static const struct behavior_hold_tap_config retro = {true}, normal = {false};
    active_hold_taps[0] = (struct active_hold_tap){.position = 1, .config = &retro,
                                                .status = STATUS_UNDECIDED, .timestamp = 100};
    active_hold_taps[1] = (struct active_hold_tap){.position = 2, .config = &retro,
                                                .status = STATUS_HOLD_TIMER, .timestamp = 100};
    active_hold_taps[2] = (struct active_hold_tap){.position = 3, .config = &normal,
                                                .status = STATUS_UNDECIDED};
    active_hold_taps[3] = (struct active_hold_tap){.position = 4, .config = &retro,
                                                .status = STATUS_TAP};
    active_hold_taps[4] = (struct active_hold_tap){.position = 5, .config = &retro,
                                                .status = STATUS_HOLD_INTERRUPT};
    active_hold_taps[5] = (struct active_hold_tap){.position = 6, .config = &retro,
                                                .status = STATUS_UNDECIDED, .work_is_cancelled = true};
    active_hold_taps[6] = (struct active_hold_tap){.position = ZMK_BHV_HOLD_TAP_POSITION_NOT_USED};
    active_hold_taps[7] = (struct active_hold_tap){.position = ZMK_BHV_HOLD_TAP_POSITION_NOT_USED};
    undecided_hold_tap = &active_hold_taps[0];
    assert(cline38_retro_pending());
    assert(!cline38_retro_commit(99)); /* A frame older than key-down cannot resolve it. */
    assert(cline38_retro_commit(101));
    assert(undecided_hold_tap == NULL && captures_released == 1);
    assert(pressed[0] == 1 && pressed[1] == 1);
    for (int i = 2; i < ZMK_BHV_HOLD_TAP_MAX_HELD; ++i) {
        assert(pressed[i] == 0);
    }
    assert(active_hold_taps[2].status == STATUS_UNDECIDED); /* lt1 untouched. */
    assert(active_hold_taps[3].status == STATUS_TAP); /* Quick tap stays a tap. */
    assert(!cline38_retro_pending() && !cline38_retro_commit(1000)); /* No double press. */
}

static void test_motion(void) {
    struct retro_motion_state s = {0};
    struct retro_motion_result r;
    /* Every cardinal direction resolves at 8 raw counts, independent of scale. */
    const int32_t directions[][2] = {{8, 0}, {-8, 0}, {0, 8}, {0, -8}};
    for (int i = 0; i < 4; ++i) {
        s = (struct retro_motion_state){0};
        r = retro_motion_step(&s, 1, 1, true, directions[i][0], directions[i][1], 0, 8, 150);
        assert(r.action == RETRO_COMMIT && r.x == directions[i][0] && r.y == directions[i][1]);
    }
    s = (struct retro_motion_state){0};
    assert(retro_motion_step(&s, 1, 1, true, 1, 3, 0, 8, 150).action == RETRO_BUFFER);
    assert(retro_motion_step(&s, 1, 1, true, 1, 3, 8, 8, 150).action == RETRO_BUFFER);
    r = retro_motion_step(&s, 1, 1, true, 1, 3, 16, 8, 150);
    assert(r.action == RETRO_COMMIT && r.x == 3 && r.y == 9); /* First motion retained. */
    r = retro_motion_step(&s, 1, 1, false, -4, 2, 24, 8, 150);
    assert(r.action == RETRO_FORWARD && r.x == -4 && r.y == 2);

    /* A queued report before release or before a new press is stale. */
    assert(retro_motion_step(&s, 2, 1, true, 100, 0, 32, 8, 150).action == RETRO_DROP);
    assert(retro_motion_step(&s, 2, 2, true, 7, 0, 40, 8, 150).action == RETRO_BUFFER);
    assert(retro_motion_step(&s, 3, 3, true, 2, 0, 48, 8, 150).action == RETRO_BUFFER);
    /* Release with no motion does not turn buffered jitter into cursor movement. */
    r = retro_motion_step(&s, 4, 4, false, 0, 0, 56, 8, 150);
    assert(r.action == RETRO_FORWARD && r.x == 0 && r.y == 0);

    assert(retro_motion_step(&s, 5, 5, true, 6, 0, 0, 8, 150).action == RETRO_BUFFER);
    assert(retro_motion_step(&s, 5, 5, true, 0, 0, 100, 8, 150).action == RETRO_BUFFER);
    assert(retro_motion_step(&s, 5, 5, true, 2, 0, 150, 8, 150).action == RETRO_BUFFER);
    s = (struct retro_motion_state){0};
    for (int i = 0; i < 1000; ++i) {
        assert(retro_motion_step(&s, 6, 6, true, i % 2 ? -1 : 1, 0, i * 8, 8, 150).action == RETRO_BUFFER);
    }
    assert(retro_motion_add(10, INT32_MAX) == 32767);
    assert(retro_motion_add(-10, INT32_MIN) == -32767);
}

int main(void) {
    test_hold_resolution();
    test_motion();
    puts("retro pointer: hold resolution, replay and stale-frame tests passed");
}
