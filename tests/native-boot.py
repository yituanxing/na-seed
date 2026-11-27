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

if len(sys.argv) != 4 or sys.argv[1] not in ("riscv64", "aarch64"):
    raise SystemExit("usage: native-boot.py <riscv64|aarch64> <esp.img> <serial.log>")
arch, image, logname = sys.argv[1:]
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
    f"qemu-system-{arch}", "-machine", "virt", "-m", "1024M",
    "-smp", "1", "-display", "none", "-monitor", "none",
    "-serial", "stdio", "-no-reboot", "-no-shutdown",
    "-drive", f"if=pflash,unit=0,format=raw,file={firmware},readonly=on",
    "-drive", f"if=none,id=bootdisk,format=raw,file={image},readonly=on",
    "-device", "virtio-blk-device,drive=bootdisk",
]
if arch == "aarch64":
    base += ["-cpu", "cortex-a72", "-semihosting-config",
             "enable=on,target=native"]
else:
    base += ["-cpu", "rv64"]

# Exit markers must come from executing the real ELF under the shared kernel.
required = (
    "[BOOT] NaSeed kernel entered",
    "[PASS] boot / serial / memory",
    "[ELF] userspace entry=",
    "hello",
    "[EXIT] user status=0",
    "[PASS] userspace exit 0",
)
deadline = time.monotonic() + 145
succeeded = False
with log.open("wb") as f:
    proc = subprocess.Popen(base, stdout=f, stderr=subprocess.STDOUT)
    try:
        while time.monotonic() < deadline:
            time.sleep(0.3)
            text = log.read_bytes().decode("utf-8", errors="replace").replace("\r", "")
            if "[FAIL]" in text:
                break
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
print(f"PASS: {arch} QEMU UEFI/Limine full kernel boot, real native "
      "ELF write(2)/exit(2), PID1 exit", flush=True)
