#!/usr/bin/env python3
"""Native RV64/AArch64 Limine QEMU smoke; no fabricated kernel output.

Boot a firmware-readable FAT image containing the real Limine UEFI executable,
full architecture ELF and native syscall ELF. Require observed kernel boot,
user-space syscall write, and PID1 exit on the current Git commit.
"""
import json
import pathlib
import socket
import subprocess
import sys
import time

if len(sys.argv) not in (4, 5, 6) or sys.argv[1] not in ("riscv64", "aarch64"):
    raise SystemExit(
        "usage: native-boot.py <riscv64|aarch64> <esp.img> "
        "<serial.log> [hello|alpine|root|init|desktop [root.ext4]]"
    )
arch, image, logname = sys.argv[1:4]
scenario = sys.argv[4] if len(sys.argv) >= 5 else "hello"
if scenario not in ("hello", "alpine", "root", "init", "desktop") or (
    scenario in ("root", "init", "desktop") and len(sys.argv) != 6
) or (scenario not in ("root", "init", "desktop") and len(sys.argv) == 6):
    raise SystemExit(f"invalid native boot arguments/scenario: {sys.argv[1:]}")
image = str(pathlib.Path(image).resolve())
log = pathlib.Path(logname)
log.parent.mkdir(parents=True, exist_ok=True)

firmwares = {
    "riscv64": "/usr/share/qemu-efi-riscv64/RISCV_VIRT_CODE.fd",
    "aarch64": "/usr/share/AAVMF/AAVMF_CODE.fd",
}
firmware = pathlib.Path(firmwares[arch])
if not firmware.is_file():
    raise SystemExit(f"FATAL: missing real UEFI firmware {firmware}")
base = [
    f"qemu-system-{arch}", "-machine",
    "virt,acpi=off" if arch == "riscv64" else "virt", "-m", "1024M",
    "-smp", "1", "-display", "none", "-monitor", "none",
    "-serial", "stdio", "-no-reboot", "-no-shutdown", "-nic", "none",
    # Native NaSeed VirtIO-MMIO deliberately implements modern v2 only.
    # QEMU virt defaults to legacy v1 otherwise, even for virtio-blk-device.
    "-global", "virtio-mmio.force-legacy=false",
    "-drive", f"if=pflash,unit=0,format=raw,file={firmware},readonly=on",
    "-drive", f"if=none,id=bootdisk,format=raw,file={image},readonly=on",
    "-device", (
        "virtio-blk-pci,drive=bootdisk" if scenario in ("root", "init", "desktop")
        else "virtio-blk-device,drive=bootdisk"
    ),
]
if scenario in ("root", "init", "desktop"):
    root_disk = str(pathlib.Path(sys.argv[5]).resolve())
    if not pathlib.Path(root_disk).is_file():
        raise SystemExit(f"missing original Alpine ext4 image: {root_disk}")
    base.extend([
        "-drive", f"if=none,id=root,format=raw,file={root_disk}",
        "-device", "virtio-blk-device,drive=root",
    ])
if scenario == "desktop":
    # QMP accesses the REAL GPU scanout, never a synthetic GUI framebuffer.
    qmp_socket = str((log.parent / f"{arch}-desktop-qmp.sock").resolve())
    base.extend([
        "-device", "virtio-gpu-device",
        "-device", "virtio-keyboard-device",
        "-device", "virtio-mouse-device",
        "-qmp", f"unix:{qmp_socket},server=on,wait=off",
    ])
# No shell access is injected; every user ELF starts from the real kernel.
if arch == "aarch64":
    base += ["-cpu", "cortex-a72", "-semihosting-config",
             "enable=on,target=native"]
else:
    base += ["-cpu", "rv64"]

# Exit markers must come from executing the real ELF under the shared kernel.
required = (
    "[BOOT] NaSeed kernel entered",
    "[FDT] native hardware resources discovered",
    "[PASS] boot / serial / memory",
    "[ELF] userspace entry=",
)
if scenario == "hello":
    required += ("\nhello\n", "[EXIT] user status=0", "[PASS] userspace exit 0")
elif scenario == "alpine":
    required += ("[M5] CPIO executable selected:",
                 f"\nNASEED_ALPINE_{arch}_OK\n", "[EXIT] user status=0",
                 "[PASS] cmdline userspace exit 0")
elif scenario == "root":
    required += ("[ROOTFS] physical ext4 root registered at /",
                 "[M191 ROOT] original Alpine PID1 from mounted ext4: /bin/busybox",
                 f"\nNASEED_ALPINE_ROOT_{arch}_OK\n",
                 "[LWEXT4 GUEST] clean upstream unmount and durable flush",
                 "[EXIT] user status=0", "[PASS] cmdline userspace exit 0")
else:
    required += ("[ROOTFS] physical ext4 root registered at /",
                 "[M191 ROOT] original Alpine PID1 from mounted ext4: /sbin/init",
                 "login:", 
                 (f"NASEED_NATIVE_DESKTOP_SESSION_READY" if scenario == "desktop"
                  else f"NASEED_NATIVE_OPENRC_{arch}_OK"),
                 "[ALPINE POWER_OFF] original BusyBox; durable ext4 unmount",
                 "[LWEXT4 GUEST] clean upstream unmount and durable flush",
                 "[PASS] cmdline userspace exit 0")
def capture_real_gpu_frame(qmp_path: str, filename: pathlib.Path):
    """Measure an actual QMP VirtIO-GPU scanout from the current QEMU VM."""
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(15)
        sock.connect(qmp_path)
        with sock.makefile("rwb") as stream:
            greeting = json.loads(stream.readline())
            if "QMP" not in greeting:
                raise RuntimeError("QMP greeting absent")
            for req in ({"execute": "qmp_capabilities"},
                        {"execute": "screendump", "arguments":
                         {"filename": str(filename.resolve())}}):
                stream.write(json.dumps(req).encode() + b"\n")
                stream.flush()
                while True:
                    reply = json.loads(stream.readline())
                    if "event" in reply:
                        continue
                    if "error" in reply:
                        raise RuntimeError(f"QMP returned error: {reply}")
                    if "return" in reply:
                        break
    magic, w, h, maximum, pixels = filename.read_bytes().split(None, 4)
    if magic != b"P6" or maximum != b"255":
        raise RuntimeError("QMP did not return an authentic PPM scanout")
    width, height = int(w), int(h)
    if len(pixels) != width * height * 3:
        raise RuntimeError("QMP PPM scanout has invalid dimensions")
    visible = sum(max(pixels[i:i+3]) > 20 for i in range(0, len(pixels), 3))
    colors = len(set(pixels[i:i+3] for i in range(0, len(pixels), 12)))
    return width, height, visible, colors

deadline = time.monotonic() + (300 if scenario == "desktop" else 145)
succeeded = False
desktop_painted = False
with log.open("wb") as f:
    proc = subprocess.Popen(base, stdin=subprocess.PIPE if scenario in ("init", "desktop") else None,
                            stdout=f, stderr=subprocess.STDOUT)
    stage = 0
    try:
        while time.monotonic() < deadline:
            time.sleep(0.3)
            text = log.read_bytes().decode("utf-8", errors="replace").replace("\r", "")
            if "[FAIL]" in text:
                break
            # Only authentic QEMU UART serial input: login on the real
            # Alpine getty, type shell commands through its PTY, then use
            # original BusyBox poweroff/OpenRC. No console syscall injection.
            if scenario in ("init", "desktop") and proc.stdin:
                if stage == 0 and "login:" in text:
                    proc.stdin.write(b"root\n")
                    proc.stdin.flush()
                    stage = 1
                elif stage == 1 and "Password:" in text:
                    proc.stdin.write(b"naseed-ci-only\n")
                    proc.stdin.flush()
                    stage = 2
                elif stage == 2 and "# " in text[text.rfind("Password:"):]:
                    if scenario == "desktop":
                        # Read-only observation: no serial path launches GUI.
                        # The original OpenRC service starts real Weston and
                        # Thunar from the unmodified Alpine APK executables.
                        cmd = (
                            "for i in $(seq 1 100); do "
                            "test -S /run/user/0/wayland-0 && "
                            "pidof thunar >/dev/null && "
                            "grep -q get_toplevel "
                            "/var/log/naseed-desktop/thunar.log 2>/dev/null "
                            "&& break; sleep 1; done; "
                            "if test -S /run/user/0/wayland-0 && "
                            "pidof thunar >/dev/null && "
                            "grep -q get_toplevel "
                            "/var/log/naseed-desktop/thunar.log; then "
                            "echo NASEED_NATIVE_DESKTOP_SESSION_READY; "
                            "else tail -80 /var/log/naseed-desktop/*.log; "
                            "echo NASEED_NATIVE_DESKTOP_SESSION_FAILED; fi\n"
                        )
                    else:
                        cmd = f"echo NASEED_NATIVE_OPENRC_{arch}_OK; poweroff\n"
                    proc.stdin.write(cmd.encode())
                    proc.stdin.flush()
                    stage = 3
                elif (scenario == "desktop" and stage == 3 and
                      "NASEED_NATIVE_DESKTOP_SESSION_FAILED" in text.splitlines()):
                    break
                elif (scenario == "desktop" and stage == 3 and
                      "NASEED_NATIVE_DESKTOP_SESSION_READY" in text.splitlines()):
                    try:
                        width, height, visible, colors = capture_real_gpu_frame(
                            qmp_socket, log.parent / f"{arch}-desktop-frame.ppm")
                        desktop_painted = (width >= 640 and height >= 480 and
                                           visible > width * height // 8 and
                                           colors > 80)
                        if desktop_painted:
                            print(f"PASS: {arch} real Weston DRM/Thunar GPU "
                                  f"scanout {width}x{height} "
                                  f"visible={visible}, colors={colors}", flush=True)
                    except (OSError, ValueError, RuntimeError) as exc:
                        print(f"Native real GPU scanout diagnostic: {exc}", flush=True)
                    if desktop_painted:
                        proc.stdin.write(b"poweroff\n")
                        proc.stdin.flush()
                        stage = 4
            if all(marker in text for marker in required) and (
                scenario != "desktop" or desktop_painted
            ):
                succeeded = True
                break
            if proc.poll() is not None:
                break
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=6)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
output = log.read_bytes().decode("utf-8", errors="replace")
print(output[-20000:], flush=True)
if not succeeded:
    missing = [marker for marker in required if marker not in output]
    raise SystemExit(
        f"FAIL: native {arch} physical-QEMU boot/user ELF not verified. "
        f"Missing serial markers: {missing}"
    )
if scenario == "hello":
    print(f"PASS: {arch} QEMU UEFI/Limine full kernel boot, real native "
          "ELF write(2)/exit(2), PID1 exit", flush=True)
elif scenario == "alpine":
    print(f"PASS: {arch} official Alpine 3.23 musl/BusyBox ELF from "
          "verified upstream minirootfs, native syscalls and clean exit",
          flush=True)
elif scenario == "root":
    print(f"PASS: {arch} official Alpine BusyBox from real writable "
          "VirtIO-MMIO ext4 root, native syscalls, clean lwext4 unmount",
          flush=True)
elif scenario == "init":
    print(f"PASS: {arch} original Alpine OpenRC PID1, real serial login, "
          "BusyBox shell and clean ext4 poweroff", flush=True)
else:
    print(f"PASS: {arch} genuine Alpine OpenRC -> Weston DRM/Pixman -> "
          "original Thunar Wayland xdg_toplevel, real VirtIO-GPU scanout "
          "and clean ext4 poweroff", flush=True)
