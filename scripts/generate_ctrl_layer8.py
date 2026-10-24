#!/usr/bin/env python3
"""Pinned, build-local event/queue bridges. Preserve original source ordering."""
import argparse
import hashlib
from pathlib import Path
from generate_retro_pointer import replace_once

HASHES = {
    "src/event_manager.c": "e500c192d236d759f3e228ea97e3823a1e9a628df5236905cdf7530809cb9e82",
    "src/behavior_queue.c": "b5201256f674a83f92705630919fbb0237765b93a01b147c4b34bde816f8a05c",
}
RUNTIME_HASH = "db48081d0aaa6e6b1da534dea6a4235633a982673ae97b7d3243041e1242f295"
PREFIX = '#include "retro_pointer.h"\n#include "ctrl_layer8.h"\n'


def adapt_events(text):
    return PREFIX + '#include <zmk/events/position_state_changed.h>\n#include <zmk/events/keycode_state_changed.h>\n' + replace_once(text,
        'int zmk_event_manager_raise(zmk_event_t *event) { return zmk_event_manager_handle_from(event, 0); }',
        '''int zmk_event_manager_raise(zmk_event_t *event) {
    /* Non-input events may originate in IRQ context. Leave their path alone. */
    if (!as_zmk_position_state_changed(event) && !as_zmk_keycode_state_changed(event)) {
        return zmk_event_manager_handle_from(event, 0);
    }
    cline38_retro_lock();
    int ret = cline38_ctrl8_capture(event) ? 0 : zmk_event_manager_handle_from(event, 0);
    cline38_retro_unlock();
    return ret;
}''')


def adapt_queue(text):
    text = replace_once(text, 'static void behavior_queue_process_next(struct k_work *work) {',
        'static unsigned cline38_queue_depth;\n'
        'static void behavior_queue_process_next_impl(struct k_work *work) {')
    text = text.replace('zmk_behavior_invoke_binding(&item.binding, event, true);',
                        'cline38_ctrl8_invoke(&item.binding, event, true);')
    text = text.replace('zmk_behavior_invoke_binding(&item.binding, event, false);',
                        'cline38_ctrl8_invoke(&item.binding, event, false);')
    # All queue state and macro-generated reports share the event transaction lock.
    text = replace_once(text, 'int zmk_behavior_queue_add(', '''
static void behavior_queue_process_next(struct k_work *work) {
    cline38_retro_lock();
    ++cline38_queue_depth;
    behavior_queue_process_next_impl(work);
    --cline38_queue_depth;
    cline38_retro_unlock();
}
bool cline38_ctrl8_queue_busy(void) {
    return cline38_queue_depth || k_msgq_num_used_get(&zmk_behavior_queue_msgq) ||
           k_work_delayable_is_pending(&queue_work);
}
static int cline38_queue_add_impl(''')
    text += '''
int zmk_behavior_queue_add(const struct zmk_behavior_binding_event *event,
                          const struct zmk_behavior_binding binding, bool press, uint32_t wait) {
    cline38_retro_lock();
    int ret = cline38_queue_add_impl(event, binding, press, wait);
    cline38_retro_unlock();
    return ret;
}
'''
    return PREFIX + text


def generate(app, output, runtime=None):
    originals = {}
    for relative, expected in HASHES.items():
        raw = (app / relative).read_bytes()
        if hashlib.sha256(raw).hexdigest() != expected:
            raise ValueError(f"Unsupported Ctrl8 upstream source: {relative}")
        originals[relative] = raw.decode()
    runtime_source = None
    if runtime:
        runtime_source = runtime / "src/runtime_macro.c"
        if hashlib.sha256(runtime_source.read_bytes()).hexdigest() != RUNTIME_HASH:
            raise ValueError("Unsupported Ctrl8 runtime macro source")
    generated = {
        "event_manager.c": adapt_events(originals["src/event_manager.c"]),
        "behavior_queue.c": adapt_queue(originals["src/behavior_queue.c"]),
    }
    if runtime_source:
        generated["runtime_macro.c"] = f'''#include "{runtime_source.as_posix()}"
bool cline38_ctrl8_runtime_busy(void) {{
    k_mutex_lock(&player.lock, K_FOREVER);
    bool busy = player.active;
    k_mutex_unlock(&player.lock);
    return busy;
}}
'''
    output.mkdir(parents=True, exist_ok=True)
    for name, content in generated.items():
        dest = output / name
        if not dest.exists() or dest.read_text() != content:
            dest.write_text(content)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--runtime", type=Path)
    args = parser.parse_args()
    generate(args.app, args.output, args.runtime)
