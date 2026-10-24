#!/usr/bin/env python3
"""Run actual Ctrl8, pinned Hold-Tap, event manager and behavior queue on host."""
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
sys.path.insert(0, str(ROOT / "scripts"))
import generate_ctrl_layer8 as ctrl8
import generate_retro_pointer as retro


def check_bridges(app, work):
    runtime = work / "runtime"
    dest = runtime / "src/runtime_macro.c"
    dest.parent.mkdir(parents=True)
    revision = re.search(r"- name: zmk-feature-runtime-macro\n(?:(?!    - name:)[\s\S])*?revision: ([0-9a-f]{40})",
                         (ROOT / "config/west.yml").read_text()).group(1)
    with urllib.request.urlopen(f"https://raw.githubusercontent.com/cormoran/zmk-feature-runtime-macro/{revision}/src/runtime_macro.c", timeout=60) as response:
        dest.write_bytes(response.read())
    project = work / "order"
    for relative in {*retro.HASHES, *ctrl8.HASHES}:
        dest = project / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(app / relative, dest)
    for name in ["combo", "keymap", "after"]:
        (project / "src" / f"{name}.c").write_text("/* configure only */")
    (project / "dummy.c").write_text("int main(void) { return 0; }")
    (project / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.20)
project(Ctrl8SourceOrder C)
set(APPLICATION_SOURCE_DIR "${{CMAKE_CURRENT_SOURCE_DIR}}")
set(ZEPHYR_ZMK_FEATURE_RUNTIME_MACRO_MODULE_DIR "{runtime.as_posix()}")
add_library(app STATIC src/event_manager.c src/behaviors/behavior_hold_tap.c
  src/combo.c src/behavior_queue.c src/keymap.c src/pointing/input_listener.c
  "{runtime.as_posix()}/src/runtime_macro.c" src/after.c)
add_executable(zephyr_final dummy.c)
set(CONFIG_CLINE38_RETRO_POINTER ON)
set(CONFIG_CLINE38_CTRL_LAYER8 ON)
set(CONFIG_ZMK_RUNTIME_MACRO ON)
add_subdirectory("{ROOT.as_posix()}" module)
file(GENERATE OUTPUT "${{CMAKE_BINARY_DIR}}/sources.txt"
  CONTENT "$<TARGET_PROPERTY:app,SOURCES>")
''')
    build = project / "build"
    cmake = [shutil.which("cmake")] if shutil.which("cmake") else [sys.executable, "-m", "cmake"]
    result = subprocess.run(cmake + ["-S", str(project), "-B", str(build)], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert result.returncode == 0, result.stdout
    sources = (build / "sources.txt").read_text().split(";")
    assert [Path(p).name for p in sources] == [
        "event_manager.c", "behavior_hold_tap.c", "combo.c", "behavior_queue.c", "keymap.c",
        "input_listener.c", "runtime_macro.c", "after.c", "behavior_ctrl_layer8.c"], sources
    for i in [0, 3, 6]:
        assert "/cline38-ctrl8/" in sources[i], sources
    print("Ctrl8 CMake: pinned event, queue and DYA macro bridges preserve source order", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        app = args.app or work / "upstream"
        if not args.app:
            revision = re.search(r"- name: zmk\n(?:(?!    - name:)[\s\S])*?revision: ([0-9a-f]{40})",
                                 (ROOT / "config/west.yml").read_text()).group(1)
            for relative in {*retro.HASHES, *ctrl8.HASHES, "include/zmk/event_manager.h"}:
                dest = app / relative
                dest.parent.mkdir(parents=True, exist_ok=True)
                url = f"https://raw.githubusercontent.com/cormoran/zmk/{revision}/app/{relative}"
                with urllib.request.urlopen(url, timeout=60) as response:
                    dest.write_bytes(response.read())
        generated = work / "generated"
        retro.generate(app, generated)
        ctrl8.generate(app, generated)
        check_bridges(app, work)
        (generated / "upstream_event_manager.h").write_bytes((app / "include/zmk/event_manager.h").read_bytes())
        includes = work / "shim"
        sources = list(generated.iterdir()) + [ROOT / "src/behavior_ctrl_layer8.c"]
        source = "\n".join(path.read_text() for path in sources)
        for header in re.findall(r"^#include <([^>]+)>", source, re.M):
            if not header.startswith(("zephyr/", "zmk/", "drivers/", "dt-bindings/")):
                continue
            dest = includes / header
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text('#include "ctrl8_kernel_shim.h"\n')
        linker = work / "events.ld"
        linker.write_text('''SECTIONS { .event_subscription : ALIGN(8) {
__event_subscriptions_start = .;
KEEP(*(.event_subscription))
__event_subscriptions_end = .;
} } INSERT AFTER .data;
''')
        executable = work / "ctrl8-test"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fno-toplevel-reorder",
            "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-const-variable",
            "-Wno-sign-compare", "-fsanitize=undefined",
            *[item for path in [includes, ROOT / "tests", ROOT / "include", ROOT / "src", generated]
              for item in ["-I", str(path)]],
            str(ROOT / "tests/ctrl8_integration_test.c"), f"-Wl,-T,{linker}", "-o", str(executable)
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
