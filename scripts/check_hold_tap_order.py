#!/usr/bin/env python3
"""Fail the firmware build if a normal key can run before Hold-Tap decides."""
import argparse
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--elf", required=True)
    args = parser.parse_args()
    result = subprocess.run([args.nm, "-n", "--defined-only", args.elf], check=True,
                            capture_output=True, text=True)
    symbols = {}
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    prefix = "zmk_event_sub_"
    suffix = "zmk_position_state_changed"
    names = [prefix + module + suffix for module in ("behavior_hold_tap", "combo", "keymap")]
    missing = [name for name in names if name not in symbols]
    if missing:
        raise SystemExit(f"Cannot verify Hold-Tap subscription order; missing: {missing}")
    hold, combo, keymap = [symbols[name] for name in names]
    if not hold < combo < keymap:
        raise SystemExit(f"Unsafe key event order: Hold-Tap={hold:#x}, combo={combo:#x}, keymap={keymap:#x}")
    print(f"CLine38 verified ELF key order: Hold-Tap {hold:#x} < combo {combo:#x} < keymap {keymap:#x}")


if __name__ == "__main__":
    main()
