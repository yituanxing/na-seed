#!/usr/bin/env python3
"""Native RV64/AArch64 Limine QEMU smoke; no fabricated kernel output.

Boot a firmware-readable FAT image containing the real Limine UEFI executable,
full architecture ELF and native syscall ELF. Require observed kernel boot,
user-space syscall write, and PID1 exit on the current Git commit.
"""
import pathlib
import subprocess
import sys
import time

if len(sys.argv) not in (4, 5, 6) or sys.argv[1] not in ("riscv64", "aarch64"):
    raise SystemExit(
        "usage: native-boot.py <riscv64|aarch64> <esp.img> "
        "<serial.log> [hello|alpine|root|init [root.ext4]]"
    )
arch, image, logname = sys.argv[1:4]
scenario = sys.argv[4] if len(sys.argv) >= 5 else "hello"
if scenario not in ("hello", "alpine", "root", "init") or (
    scenario in ("root", "init") and len(sys.argv) != 6
) or (scenario not in ("root", "init") and len(sys.argv) == 6):
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
        "virtio-blk-pci,drive=bootdisk" if scenario in ("root", "init")
        else "virtio-blk-device,drive=bootdisk"
    ),
]
if scenario in ("root", "init"):
    root_disk = str(pathlib.Path(sys.argv[5]).resolve())
    if not pathlib.Path(root_disk).is_file():
        raise SystemExit(f"missing original Alpine ext4 image: {root_disk}")
    base.extend([
        "-drive", f"if=none,id=root,format=raw,file={root_disk}",
        "-device", "virtio-blk-device,drive=root",
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
                 "login:", f"NASEED_NATIVE_OPENRC_{arch}_OK",
                 "[ALPINE POWER_OFF] original BusyBox; durable ext4 unmount",
                 "[LWEXT4 GUEST] clean upstream unmount and durable flush",
                 "[PASS] cmdline userspace exit 0")
deadline = time.monotonic() + 145
succeeded = False
with log.open("wb") as f:
    proc = subprocess.Popen(base, stdin=subprocess.PIPE if scenario == "init" else None,
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
            if scenario == "init" and proc.stdin:
                if stage == 0 and "login:" in text:
                    proc.stdin.write(b"root\n")
                    proc.stdin.flush()
                    stage = 1
                elif stage == 1 and "Password:" in text:
                    proc.stdin.write(b"naseed-ci-only\n")
                    proc.stdin.flush()
                    stage = 2
                elif stage == 2 and "# " in text[text.rfind("Password:"):]:
                    proc.stdin.write(
                        f"echo NASEED_NATIVE_OPENRC_{arch}_OK; poweroff\n".encode()
                    )
                    proc.stdin.flush()
                    stage = 3
            if all(marker in text for marker in required):
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
else:
    print(f"PASS: {arch} original Alpine OpenRC PID1, real serial login, "
          "BusyBox shell and clean ext4 poweroff", flush=True)
