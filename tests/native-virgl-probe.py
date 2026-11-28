#!/usr/bin/env python3
"""Real QEMU VirGL host/capset handshake, using NaSeed's existing full kernel.

This is intentionally NOT a 3D-rendering PASS: guest render ioctls,
contexts/resources, Mesa winsys and command submission are still unfinished.
It proves that the host advertises VirGL and that the guest's control queue
actually obtained a nonzero VirGL capset from the unmodified QEMU backend.
"""
import pathlib
import re
import subprocess
import sys
import time

if len(sys.argv) != 2 or sys.argv[1] not in ("riscv64", "aarch64"):
    raise SystemExit("usage: native-virgl-probe.py <riscv64|aarch64>")
arch = sys.argv[1]
work = pathlib.Path("build/native-boot") / arch
esp = (work / "efi.img").resolve()
log = work / "virgl-probe.log"
firmware = pathlib.Path({
    "riscv64": "/usr/share/qemu-efi-riscv64/RISCV_VIRT_CODE.fd",
    "aarch64": "/usr/share/AAVMF/AAVMF_CODE.fd",
}[arch])
for file in (esp, firmware):
    if not file.is_file():
        raise SystemExit(f"missing real boot input: {file}")
gpu_device = "virtio-gpu-gl-device"
qemu = f"qemu-system-{arch}"
help_text = subprocess.run(
    [qemu, "-device", "help"], stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT, text=True, check=True,
).stdout
if gpu_device not in help_text:
    raise SystemExit(f"{qemu} lacks real {gpu_device} host support")
args = [
    qemu,
    "-machine", "virt,acpi=off" if arch == "riscv64" else "virt",
    "-m", "1024M", "-smp", "1",
    "-display", "sdl,gl=on", "-monitor", "none",
    "-serial", "stdio", "-no-reboot", "-no-shutdown", "-nic", "none",
    "-global", "virtio-mmio.force-legacy=false",
    "-drive", f"if=pflash,unit=0,format=raw,file={firmware},readonly=on",
    "-drive", f"if=none,id=bootdisk,format=raw,file={esp},readonly=on",
    "-device", "virtio-blk-device,drive=bootdisk",
    "-device", gpu_device,
    "-cpu", "rv64" if arch == "riscv64" else "cortex-a72",
]
if arch == "aarch64":
    args += ["-semihosting-config", "enable=on,target=native"]
# Require the hardware capset response, not merely a -device switch or
# an echoed command string. All checks are based on actual guest UART.
capset = re.compile(
    r"\[VIRTIO GPU 3D DISCOVERY\] host_virgl=1 capsets=(\d+) "
    r"virgl_id=([12]) version=(\d+) bytes=(\d+) "
    r"render_api=not-implemented"
)
deadline = time.monotonic() + 150
with log.open("wb") as stream:
    proc = subprocess.Popen(args, stdout=stream, stderr=subprocess.STDOUT)
    try:
        while time.monotonic() < deadline:
            time.sleep(0.4)
            output = log.read_bytes().decode("utf-8", errors="replace").replace("\r", "")
            if proc.poll() is not None and not capset.search(output):
                break
            m = capset.search(output)
            if m and all(int(n) > 0 for n in m.groups()):
                if "[M177 VIRTIO GPU READY]" not in output:
                    continue
                print(
                    f"PASS: {arch} real QEMU VirGL feature+capset handshake: "
                    f"count={m[1]} id={m[2]} version={m[3]} bytes={m[4]}; "
                    "3D rendering not yet implemented", flush=True
                )
                break
        else:
            raise RuntimeError(f"{arch}: timed out waiting for genuine VirGL capset")
        if not capset.search(output) or "[M177 VIRTIO GPU READY]" not in output:
            raise RuntimeError(f"{arch}: no actual VirGL feature/capset response")
    except Exception:
        print("=== QEMU VirGL probe serial tail ===", file=sys.stderr)
        print(log.read_text(errors="replace")[-10000:], file=sys.stderr)
        raise
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=8)
