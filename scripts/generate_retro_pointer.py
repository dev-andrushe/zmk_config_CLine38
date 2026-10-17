#!/usr/bin/env python3
"""Build-local adapters for the pinned cormoran ZMK; never edit west sources."""
import argparse
import hashlib
from pathlib import Path

HASHES = {
    "src/behaviors/behavior_hold_tap.c": "e63582773ebd867fd9f1b7b5c7773f181a598e9f7f3a69a6d5477fda1e22cbca",
    "src/pointing/input_listener.c": "0e83fb3e6c1cba3d023237ae0495d7340e9fd5716fad900ff9f82ed7c2e8c75e",
}


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise ValueError(f"Expected one upstream anchor: {before!r}")
    return text.replace(before, after, 1)


def adapt_hold_tap(text):
    prefix = '''/* CLine38 build-local Retro Tap pointer adapter. */
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include "retro_pointer.h"
K_MUTEX_DEFINE(cline38_retro_mutex);
static atomic_t cline38_retro_generation;
uint32_t cline38_retro_epoch(void) { return (uint32_t)atomic_get(&cline38_retro_generation); }
void cline38_retro_lock(void) { k_mutex_lock(&cline38_retro_mutex, K_FOREVER); }
void cline38_retro_unlock(void) { k_mutex_unlock(&cline38_retro_mutex); }
'''
    for name in ("on_hold_tap_binding_pressed", "on_hold_tap_binding_released"):
        text = replace_once(text, f"static int {name}(", f"static int {name}_impl(")
    wrappers = ""
    for name in ("on_hold_tap_binding_pressed", "on_hold_tap_binding_released"):
        wrappers += f'''static int {name}(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {{
    cline38_retro_lock();
    atomic_inc(&cline38_retro_generation);
    int ret = {name}_impl(binding, event);
    cline38_retro_unlock();
    return ret;
}}
'''
    text = replace_once(text, "static const struct behavior_driver_api behavior_hold_tap_driver_api =",
                        wrappers + "\nstatic const struct behavior_driver_api behavior_hold_tap_driver_api =")
    text = replace_once(text, "int behavior_hold_tap_listener(const zmk_event_t *eh)",
                        "static int behavior_hold_tap_listener_impl(const zmk_event_t *eh)")
    text = replace_once(text, "ZMK_LISTENER(behavior_hold_tap, behavior_hold_tap_listener);", '''
int behavior_hold_tap_listener(const zmk_event_t *eh) {
    cline38_retro_lock();
    int ret = behavior_hold_tap_listener_impl(eh);
    cline38_retro_unlock();
    return ret;
}
ZMK_LISTENER(behavior_hold_tap, behavior_hold_tap_listener);''')
    text = replace_once(text, "void behavior_hold_tap_timer_work_handler(struct k_work *item)",
                        "static void behavior_hold_tap_timer_work_handler_impl(struct k_work *item)")
    text = replace_once(text, "static int behavior_hold_tap_init(const struct device *dev)", '''
void behavior_hold_tap_timer_work_handler(struct k_work *item) {
    cline38_retro_lock();
    behavior_hold_tap_timer_work_handler_impl(item);
    cline38_retro_unlock();
}
static int behavior_hold_tap_init(const struct device *dev)''')
    text = replace_once(text, "#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */",
                        '#include "retro_pointer_hold_tap.inc"\n\n#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */')
    return prefix + text


def adapt_listener(text):
    text = replace_once(text, "#define IL_INST(n)", '#include "retro_pointer_listener.inc"\n\n#define IL_INST(n)')
    return replace_once(text, "input_handler(&config_##n, &data_##n, evt);",
                        "cline38_pointer_input(&config_##n, &data_##n, evt);")


def generate(app, output):
    # Verify both before writing either: changed upstream versions require review.
    sources = {}
    for path, expected in HASHES.items():
        raw = (app / path).read_bytes()
        if hashlib.sha256(raw).hexdigest() != expected:
            raise ValueError(f"Unsupported ZMK source: {path}; review the Retro Tap adapter before updating ZMK")
        sources[path] = raw.decode()
    output.mkdir(parents=True, exist_ok=True)
    generated = {
        "behavior_hold_tap.c": adapt_hold_tap(sources["src/behaviors/behavior_hold_tap.c"]),
        "input_listener.c": adapt_listener(sources["src/pointing/input_listener.c"]),
    }
    for name, content in generated.items():
        dest = output / name
        if not dest.exists() or dest.read_text() != content:
            dest.write_text(content)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    generate(args.app, args.output)
