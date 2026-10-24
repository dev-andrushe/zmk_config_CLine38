#!/usr/bin/env python3
"""Exercise the pinned, generated Hold-Tap with a host event/HID shim."""
import argparse
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("retro_generator", ROOT / "scripts/generate_retro_pointer.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


def check_source_order(app, work):
    project = work / "order"
    project.mkdir()
    for relative in generator.HASHES:
        dest = project / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(app / relative, dest)
    for name in ("before", "combo", "keymap", "after"):
        (project / "src" / f"{name}.c").write_text("/* configure-only fixture */\n")
    (project / "dummy.c").write_text("int main(void) { return 0; }\n")
    (project / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.20)
project(HoldTapSourceOrder C)
set(APPLICATION_SOURCE_DIR "${{CMAKE_CURRENT_SOURCE_DIR}}")
add_library(app STATIC src/before.c src/behaviors/behavior_hold_tap.c
  src/combo.c src/keymap.c src/pointing/input_listener.c src/after.c)
add_executable(zephyr_final dummy.c)
set(CONFIG_CLINE38_RETRO_POINTER ON)
add_subdirectory("{ROOT.as_posix()}" module)
file(GENERATE OUTPUT "${{CMAKE_BINARY_DIR}}/sources.txt"
  CONTENT "$<TARGET_PROPERTY:app,SOURCES>")
''')
    cmake = shutil.which("cmake")
    command = [cmake] if cmake else [sys.executable, "-m", "cmake"]
    build = project / "build"
    subprocess.run(command + ["-S", str(project), "-B", str(build)], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    sources = (build / "sources.txt").read_text().split(";")
    names = [Path(p).name for p in sources]
    assert names == ["before.c", "behavior_hold_tap.c", "combo.c", "keymap.c",
                     "input_listener.c", "after.c"], f"Changed event listener order: {names}"
    assert str(build / "module/cline38-retro/behavior_hold_tap.c") in sources
    print("Hold-Tap source replacement preserves every source position", flush=True)


def check_key_events(app, generated, work):
    shutil.copyfile(app / "include/zmk/event_manager.h", generated / "upstream_event_manager.h")
    shutil.copyfile(app / "src/event_manager.c", generated / "upstream_event_manager.c")
    includes = work / "shim"
    includes.mkdir()
    source = "\n".join(path.read_text() for path in generated.iterdir())
    for header in re.findall(r"^#include <([^>]+)>", source, re.M):
        if not header.startswith(("zephyr/", "zmk/", "drivers/", "dt-bindings/")):
            continue
        dest = includes / header
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text('#include "hold_tap_shim.h"\n')
    executable = work / "hold-tap-keys"
    linker = work / "events.ld"
    linker.write_text('''SECTIONS { .event_subscription : ALIGN(8) {
__event_subscriptions_start = .;
KEEP(*(.event_subscription))
__event_subscriptions_end = .;
} } INSERT AFTER .data;
''')
    command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fno-toplevel-reorder",
                    "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-const-variable",
                    "-Wno-sign-compare",  # Existing upstream int/size_t comparisons.
                    "-fsanitize=undefined", "-I", str(includes), "-I", str(ROOT / "tests"),
                    "-I", str(ROOT / "include"), "-I", str(ROOT / "src"),
                    "-I", str(generated), str(ROOT / "tests/hold_tap_keys_test.c"),
                    f"-Wl,-T,{linker}", "-o", str(executable)]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
    subprocess.run([sys.executable, str(ROOT / "scripts/check_hold_tap_order.py"),
                    "--nm", "nm", "--elf", str(executable)], check=True)
    # Prove that this behavioral test detects keymap-before-Hold-Tap ordering.
    subprocess.run(command + ["-DBROKEN_SUBSCRIPTION_ORDER"], check=True)
    broken = subprocess.run([str(executable)], capture_output=True, text=True, cwd=work)
    assert broken.returncode != 0 and "seen_layer" in broken.stderr, broken.stderr
    print("Negative control: old listener order fails on the first other key", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, help="Optional existing pinned ZMK app sources")
    parser.add_argument("--order-only", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        app = args.app or work / "upstream"
        if args.app is None:
            manifest = (ROOT / "config/west.yml").read_text()
            revision = re.search(r"- name: zmk\n(?:(?!    - name:)[\s\S])*?revision: ([0-9a-f]{40})",
                                 manifest).group(1)
            for relative in [*generator.HASHES, "src/event_manager.c", "include/zmk/event_manager.h"]:
                dest = app / relative
                dest.parent.mkdir(parents=True, exist_ok=True)
                url = f"https://raw.githubusercontent.com/cormoran/zmk/{revision}/app/{relative}"
                with urllib.request.urlopen(url, timeout=60) as response:
                    dest.write_bytes(response.read())
        generated = work / "generated"
        generator.generate(app, generated)
        check_source_order(app, work)
        if not args.order_only:
            check_key_events(app, generated, work)


if __name__ == "__main__":
    main()
