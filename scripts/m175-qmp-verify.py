#!/usr/bin/env python3
"""M175 strict IRQ-burst extension of M173's five real physical QMP frames.

Reuses the original M173 pixel oracle and GUI interaction unchanged.  Injects
>8 distinct physical actions through EACH QEMU VirtIO input device, then the
kernel's own IRQ ACK / deferred drain counters must prove their delivery.
A QMP-send token alone never constitutes an IRQ success.
"""
import runpy
import sys
import time
from pathlib import Path

m173 = runpy.run_path("scripts/m173-qmp-verify.py", run_name="m175_m173_library")
original_main = m173["main"]
globals_ = original_main.__globals__
base = globals_["base"]
original_pointer_click = globals_["pointer_click"]
pointer_calls = 0
pointer_moves = 0
keyboard_physical_actions = 0
serial_path = None


def irq_guest_marker_present():
    if serial_path is None or not serial_path.is_file():
        return False
    raw = serial_path.read_bytes()
    return raw.count(b'[M175 INPUT IRQ ONLY PASS]') > 0


def pointer_click_with_burst(sock, reader, x, y):
    global pointer_calls, pointer_moves, keyboard_physical_actions
    pointer_calls += 1
    # After the FIRST physical frame but BEFORE M173's normal interactions,
    # both independent input devices must demonstrate an IRQ-driven burst.
    # This makes the real guest's later >8-events marker causally tied to our
    # injection, not to routine keyboard/UI actions already present in M173.
    if pointer_calls == 1:
        if irq_guest_marker_present():
            raise RuntimeError('guest claimed IRQ-only before M175 QMP bursts')
        # Twelve distinct full X/Y tablet moves, twelve QMP hardware calls.
        for i in range(12):
            px = 620 + (i % 4) * 8
            py = 450 + (i // 4) * 8
            base["send"](sock, reader, base["pointer"](px, py))
            pointer_moves += 1
        # A single hardware QMP keyboard burst: twelve actual key press/release
        # pairs for x plus twelve independent Backspace actions.  The current
        # startup desktop has not yet focused the interactive PTY, so these
        # cannot corrupt the later "gui" or "echo 3" shell command assertions.
        events = []
        for _ in range(12):
            for name in ("x", "backspace"):
                events.append(base["key"](name, True))
                events.append(base["key"](name, False))
                keyboard_physical_actions += 1
        base["send"](sock, reader, events)
        # The guest may report IRQ completion only after the scheduler-safe
        # deferred consumer runs.  Require that report AFTER the hardware
        # burst, not an unrelated startup marker written before injection.
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline and not irq_guest_marker_present():
            time.sleep(0.1)
        if not irq_guest_marker_present():
            raise RuntimeError('no guest IRQ-only evidence after QMP bursts')
    return original_pointer_click(sock, reader, x, y)


globals_["pointer_click"] = pointer_click_with_burst


def main():
    global serial_path
    try:
        serial_path = Path(sys.argv[sys.argv.index('--serial') + 1])
    except (ValueError, IndexError) as exc:
        raise RuntimeError('M175 requires original --serial guest log') from exc
    # This does every M173 check: actual QEMU keyboard path, five captured
    # 800x600 physical framebuffer states, fourth-to-fifth GUI launch, LVGL
    # mouse click/repaint, independent close, and subsequent BusyBox shell.
    original_main()
    if pointer_calls != 4 or pointer_moves != 12:
        raise RuntimeError(f"tablet burst missing: clicks={pointer_calls} moves={pointer_moves}")
    if keyboard_physical_actions != 24:
        raise RuntimeError(f"keyboard burst missing: actions={keyboard_physical_actions}")
    print("[M175 QMP IRQ BURST PASS] five M173 real QEMU frames + "
          "12 distinct tablet moves in 12 QMP commands + "
          "12 keyboard x/backspace action pairs in one 48-event QMP batch; "
          "native guest device IRQ counters must pass separately", flush=True)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print("[M175 QMP IRQ BURST FAIL]", exc, file=sys.stderr, flush=True)
        raise SystemExit(1)
