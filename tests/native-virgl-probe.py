#!/usr/bin/env python3
"""Real QEMU VirGL capset + guest context-lifecycle acceptance.

Guest-side VirGL rendering and AFTER-BOOT asynchronous context acceptance.
Requires host-fenced bootstrap render/readback and a second context lifecycle
completed in the scheduler's existing IRQ-driven 2D control-queue service.
This is NOT a Mesa or accelerated Weston acceptance.
"""
import pathlib
import re
import subprocess
import sys
import time

if len(sys.argv) != 2 or sys.argv[1] not in ("x86_64", "riscv64", "aarch64"):
    raise SystemExit("usage: native-virgl-probe.py <x86_64|riscv64|aarch64>")
arch = sys.argv[1]
work = pathlib.Path("build/native-boot") / arch
work.mkdir(parents=True, exist_ok=True)
esp = (work / ("virgl-alpine.iso" if arch == "x86_64"
               else "virgl-alpine-efi.img")).resolve()
log = work / "virgl-probe.log"
if arch == "x86_64":
    firmware = next(
        (pathlib.Path(p) for p in (
            "/usr/share/OVMF/OVMF_CODE_4M.fd",
            "/usr/share/OVMF/OVMF_CODE.fd",
        ) if pathlib.Path(p).is_file()),
        pathlib.Path("/missing-ovmf"),
    )
else:
    firmware = pathlib.Path({
        "riscv64": "/usr/share/qemu-efi-riscv64/RISCV_VIRT_CODE.fd",
        "aarch64": "/usr/share/AAVMF/AAVMF_CODE.fd",
    }[arch])
for file in (esp, firmware):
    if not file.is_file():
        raise SystemExit(f"missing real boot input: {file}")
gpu_device = "virtio-gpu-gl-pci" if arch == "x86_64" else "virtio-gpu-gl-device"
qemu = f"qemu-system-{arch}"
help_text = subprocess.run(
    [qemu, "-device", "help"], stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT, text=True, check=True,
).stdout
if gpu_device not in help_text:
    raise SystemExit(f"{qemu} lacks real {gpu_device} host support")
if arch == "x86_64":
    # Real modern PCI VirtIO-GPU and the same UEFI/Limine ISO used by smoke.
    args = [
        qemu, "-M", "q35", "-m", "1024M", "-cpu", "qemu64",
        "-display", "sdl,gl=on", "-monitor", "none",
        "-serial", "stdio", "-no-reboot", "-no-shutdown", "-nic", "none",
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
        "-drive", f"if=pflash,unit=0,format=raw,file={firmware},readonly=on",
        "-cdrom", str(esp), "-vga", "none",
        "-device", gpu_device + ",disable-legacy=on",
    ]
else:
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
context = re.compile(
    r"\[VIRTIO GPU 3D CONTEXT\] create=1 destroy=1 ctx=1 "
    r"render_api=not-implemented"
)
pixel = re.compile(
    r"\[VIRTIO GPU 3D PIXEL\] submit=1 readback=1 "
    r"color=verified size=16x16"
)
runtime = re.compile(
    r"\[VIRTIO GPU 3D RUNTIME\] ctx_create=1 ctx_destroy=1 "
    r"irq_fence=1 deferred=1"
)
deadline = time.monotonic() + 150
with log.open("wb") as stream:
    proc = subprocess.Popen(args, stdout=stream, stderr=subprocess.STDOUT)
    try:
        while time.monotonic() < deadline:
            time.sleep(0.4)
            output = log.read_bytes().decode("utf-8", errors="replace").replace("\r", "")
            if "[VIRTIO GPU 3D PIXEL ERROR]" in output:
                raise RuntimeError(f"{arch}: actual VirGL pixel readback mismatch")
            if "[VIRTIO PCI FAIL]" in output:
                raise RuntimeError(f"{arch}: guest GPU probe failed during PCI binding")
            if "[PASS] userspace exit 0" in output and not runtime.search(output):
                raise RuntimeError(f"{arch}: upstream Alpine exited before async GPU context completion")
            if proc.poll() is not None and not (capset.search(output) and context.search(output) and pixel.search(output) and runtime.search(output)):
                break
            m = capset.search(output)
            if m and all(int(n) > 0 for n in m.groups()) and context.search(output) and pixel.search(output) and runtime.search(output):
                if ("[M177 VIRTIO GPU READY]" not in output or
                    "[M4] real Alpine busybox via musl interpreter in ring 3" not in output):
                    continue
                print(
                    f"PASS: {arch} real QEMU VirGL feature+capset handshake: "
                    f"count={m[1]} id={m[2]} version={m[3]} bytes={m[4]}; "
                    "original Alpine userland + VirGL pixels + deferred IRQ context OK; Mesa pending", flush=True
                )
                break
        else:
            raise RuntimeError(f"{arch}: timed out waiting for genuine VirGL capset")
        if not capset.search(output) or not context.search(output) or not pixel.search(output) or not runtime.search(output) or "[M177 VIRTIO GPU READY]" not in output or "[M4] real Alpine busybox via musl interpreter in ring 3" not in output:
            raise RuntimeError(f"{arch}: missing original user entry, VirGL pixels or runtime IRQ completion")
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
