/* SPDX-License-Identifier: MIT
 * Included after input_handler and before the upstream input callbacks. */
#include "retro_pointer.h"
#include "retro_pointer_logic.h"

struct cline38_pointer_frame {
    const struct input_listener_config *config;
    struct input_listener_data *data;
    const struct device *dev;
    int64_t timestamp;
    uint32_t epoch, sequence;
    int32_t x, y;
    bool have_x, have_y;
};

K_MSGQ_DEFINE(cline38_pointer_frames, sizeof(struct cline38_pointer_frame), 64, 4);
static struct retro_motion_state cline38_pointer_motion;

static void cline38_pointer_process(struct k_work *work) {
    ARG_UNUSED(work);
    struct cline38_pointer_frame frame;
    static uint32_t last_sequence;
    /* Yield to keyboard/timer work even if the sensor keeps filling the queue. */
    for (int batch = 0; batch < 8 && k_msgq_get(&cline38_pointer_frames, &frame, K_NO_WAIT) == 0;
         ++batch) {
        cline38_retro_lock();
        if (frame.sequence != last_sequence + 1) {
            cline38_pointer_motion = (struct retro_motion_state){0};
        }
        last_sequence = frame.sequence;
        uint32_t epoch = cline38_retro_epoch();
        struct retro_motion_result result = retro_motion_step(
            &cline38_pointer_motion, epoch, frame.epoch, cline38_retro_pending(),
            frame.x, frame.y, frame.timestamp, CONFIG_CLINE38_RETRO_POINTER_THRESHOLD,
            CONFIG_CLINE38_RETRO_POINTER_IDLE_MS);
        bool replay = result.action == RETRO_FORWARD;
        if (result.action == RETRO_COMMIT) {
            replay = cline38_retro_commit(frame.timestamp) && cline38_retro_epoch() == epoch;
            /* Replay the full buffered X/Y report under the newly active layer. */
            frame.have_x = frame.have_y = true;
        }
        if (replay) {
            struct input_event event = {.dev = frame.dev, .type = INPUT_EV_REL};
            if (frame.have_x) {
                event.code = INPUT_REL_X;
                event.value = result.x;
                event.sync = !frame.have_y;
                input_handler(frame.config, frame.data, &event);
            }
            if (frame.have_y) {
                event.code = INPUT_REL_Y;
                event.value = result.y;
                event.sync = true;
                input_handler(frame.config, frame.data, &event);
            }
        }
        cline38_retro_unlock();
    }
    if (k_msgq_num_used_get(&cline38_pointer_frames) != 0) {
        k_work_submit(work);
    }
}
K_WORK_DEFINE(cline38_pointer_work, cline38_pointer_process);

static void cline38_pointer_input(const struct input_listener_config *config,
                                  struct input_listener_data *data, struct input_event *event) {
    /* Only physical ball X/Y participates. The mixed-mode virtual device,
     * generated inertia and other input sources must never trigger a hold. */
    if (event->dev != DEVICE_DT_GET(DT_NODELABEL(trackball)) || event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        input_handler(config, data, event);
        return;
    }
    /* This accumulator is owned by the one input thread; the work item owns
     * Hold-Tap state and buffered motion. Never wait for a work item here. */
    static struct cline38_pointer_frame frame;
    static uint32_t sequence;
    if (!frame.have_x && !frame.have_y) {
        frame.epoch = cline38_retro_epoch();
    }
    if (event->code == INPUT_REL_X) {
        frame.x = retro_motion_add(frame.x, event->value);
        frame.have_x = true;
    } else {
        frame.y = retro_motion_add(frame.y, event->value);
        frame.have_y = true;
    }
    if (event->sync) {
        frame.config = config;
        frame.data = data;
        frame.dev = event->dev;
        frame.timestamp = k_uptime_get();
        frame.sequence = ++sequence;
        if (k_msgq_put(&cline38_pointer_frames, &frame, K_NO_WAIT) == 0) {
            k_work_submit(&cline38_pointer_work);
        } else {
            LOG_WRN("Retro pointer queue full: dropped sensor frame");
        }
        frame = (struct cline38_pointer_frame){0};
    }
}
