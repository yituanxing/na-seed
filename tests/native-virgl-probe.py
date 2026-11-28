#!/usr/bin/env python3
"""Real QEMU VirGL host/capset handshake, using NaSeed's existing full kernel.

This is intentionally NOT a 3D-rendering PASS: guest render ioctls,
contexts/resources, Mesa winsys and command submission are still unfinished.
It proves that the host advertises VirGL and that the guest's control queue
actually obtained a nonzero VirGL capset from the unmodified QEMU backend.
"""
import os
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
esp = (pathlib.Path("build/naseed.iso") if arch == "x86_64"
       else work / "efi.img").resolve()
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
gpu_owner = os.environ.get("NAS_EXPERIMENT_TGOS_GPU_OWNER") == "1"
if gpu_owner:
    # Only the experimental original Rust GPU owner emits this marker.
    # It means a REAL QEMU capset reply and original CTX used-ring drain,
    # NOT a render node or Mesa/glmark2 result.
    capset = re.compile(
        r"\[TGOS ORIGINAL GPU CAPSET PASS\] capset_id=([12]) "
        r"original-ctx-create/destroy-used=1 render-node=absent"
    )
    ready_marker = "[TGOS ORIGINAL DRM ABI PASS]"
else:
    capset = re.compile(
        r"\[VIRTIO GPU 3D DISCOVERY\] host_virgl=1 capsets=(\d+) "
        r"virgl_id=([12]) version=(\d+) bytes=(\d+) "
        r"render_api=not-implemented"
    )
    ready_marker = "[M177 VIRTIO GPU READY]"
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
                if ready_marker not in output:
                    continue
                if gpu_owner:
                    print(
                        f"PASS: {arch} original TGOSKits VirtIoGpu owns real "
                        f"QEMU device, capset_id={m[1]}, CTX_CREATE/DESTROY "
                        "used-ring complete; NOT Mesa 3D", flush=True
                    )
                else:
                    print(
                        f"PASS: {arch} real QEMU VirGL feature+capset handshake: "
                        f"count={m[1]} id={m[2]} version={m[3]} bytes={m[4]}; "
                        "3D rendering not yet implemented", flush=True
                    )
                break
        else:
            raise RuntimeError(f"{arch}: timed out waiting for genuine VirGL capset")
        if not capset.search(output) or ready_marker not in output:
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
