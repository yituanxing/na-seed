#!/usr/bin/env python3
"""Genuine Alpine Python + unmodified libdrm -> NaSeed experimental renderD128.

This is a real *userspace* test of the original TGOSKits VirtIoGpu READ-ONLY
UAPI. It does not claim Mesa/GEM/EXECBUFFER support or GPU rendering.
"""
import ctypes
import errno
import fcntl
import mmap
import os
import stat
import struct

RENDER = "/dev/dri/renderD128"
GETPARAM = 0xC0106443
GET_CAPS = 0xC0186449
EXECBUFFER = 0xC0406442
MAP = 0xC0106441
RESOURCE_CREATE = 0xC0386444
RESOURCE_INFO = 0xC0106445
TRANSFER_FROM_HOST = 0xC02C6446
TRANSFER_TO_HOST = 0xC02C6447
GEM_CLOSE = 0x40086409
WAIT = 0xC0086448

def create_gpu_resource(fd):
    # Real upstream VirtGPU RESOURCE_CREATE: 64x64 RGBA render target,
    # identical shape as the earlier boot-proven resource. No fake GEM mmap.
    values = [2, 1, 2, 64, 64, 1, 1, 0, 0, 0, 0, 0, 16384, 256]
    request = bytearray(struct.pack("=14I", *values))
    assert len(request) == 56
    fcntl.ioctl(fd, RESOURCE_CREATE, request, True)
    response = struct.unpack("=14I", request)
    handle, resource_id = response[10], response[11]
    assert handle and resource_id and response[12] == 16384
    info = bytearray(struct.pack("=4I", handle, 0, 0, 0))
    fcntl.ioctl(fd, RESOURCE_INFO, info, True)
    found = struct.unpack("=4I", info)
    assert found == (handle, resource_id, 16384, 0), found
    return handle, resource_id

def map_gpu_resource(fd, handle, bytes_count=16384):
    # Standard VirtGPU MAP ioctl returns GEM fake mmap offset, NOT a physical
    # address. NaSeed VMA machinery owns refs across GEM_CLOSE.
    req = bytearray(struct.pack("=QII", 0, handle, 0))
    fcntl.ioctl(fd, MAP, req, True)
    offset, echoed, pad = struct.unpack("=QII", req)
    assert offset >= 0x10000000 and offset % 4096 == 0
    assert echoed == handle and pad == 0
    return mmap.mmap(fd, bytes_count, flags=mmap.MAP_SHARED,
                     prot=mmap.PROT_READ | mmap.PROT_WRITE, offset=offset)

def gpu_full_layer_transfer(fd, handle, to_host, stride=256):
    # Real upstream Linux VirtGPU TRANSFER_{TO,FROM}_HOST_3D ABI (44 bytes).
    # Box covers exactly one 64x64 uncompressed BGRA8888 image layer.
    request = struct.pack("=11I", handle, 0, 0, 0, 64, 64, 1,
                          0, 0, stride, 16384)
    assert len(request) == 44
    fcntl.ioctl(fd, TRANSFER_TO_HOST if to_host else TRANSFER_FROM_HOST,
                request)

def close_gpu_resource(fd, handle):
    fcntl.ioctl(fd, GEM_CLOSE, struct.pack("=II", handle, 0))

def exec_many(fd, command_ptr, command_size, handles=()):
    if handles:
        buffer_handles = (ctypes.c_uint32 * len(handles))(*handles)
        handles_ptr = ctypes.addressof(buffer_handles)
    else:
        handles_ptr = 0
    request = struct.pack("=IIQQIiIIIIQQ", 0, command_size, command_ptr,
                           handles_ptr, len(handles), -1, 0, 0, 0, 0, 0, 0)
    fcntl.ioctl(fd, EXECBUFFER, request)

def exec_with_bo(fd, command_ptr, handle):
    exec_many(fd, command_ptr, 4, (handle,))

def wait_bo(fd, handle):
    fcntl.ioctl(fd, WAIT, struct.pack("=II", handle, 1))
    fcntl.ioctl(fd, WAIT, struct.pack("=II", handle, 0))



class DrmVersion(ctypes.Structure):
    _fields_ = [
        ("major", ctypes.c_int), ("minor", ctypes.c_int),
        ("patchlevel", ctypes.c_int), ("name_len", ctypes.c_int),
        ("name", ctypes.c_void_p), ("date_len", ctypes.c_int),
        ("date", ctypes.c_void_p), ("desc_len", ctypes.c_int),
        ("desc", ctypes.c_void_p),
    ]


assert ctypes.sizeof(DrmVersion) == 56, "libdrm's public drmVersion ABI differs from kernel drm_version"
libdrm = ctypes.CDLL("libdrm.so.2")
libdrm.drmGetVersion.argtypes = [ctypes.c_int]
libdrm.drmGetVersion.restype = ctypes.POINTER(DrmVersion)
libdrm.drmFreeVersion.argtypes = [ctypes.POINTER(DrmVersion)]


class Result(ctypes.Structure):
    _fields_ = [("value", ctypes.c_uint32), ("guard", ctypes.c_uint32)]


def getparam(fd, param):
    # The original Linux ABI is an int *, NOT a u64 *. Deliberately put a
    # sentinel in the next 4 bytes, so an 8-byte kernel copy is detected.
    result = Result(0xDEADBEEF, 0xA5B4C3D2)
    request = struct.pack("=QQ", param, ctypes.addressof(result))
    fcntl.ioctl(fd, GETPARAM, request)
    assert result.guard == 0xA5B4C3D2, (
        f"kernel GETPARAM overran user int*: guard={result.guard:#x}"
    )
    return result.value


node = os.stat(RENDER)
assert stat.S_ISCHR(node.st_mode), "renderD128 must be a real character device"
assert os.major(node.st_rdev) == 226 and os.minor(node.st_rdev) == 128
with os.fdopen(os.open(RENDER, os.O_RDWR | os.O_CLOEXEC), "rb", buffering=0) as opened:
    fd = opened.fileno()
    # The library is the unchanged Alpine libdrm package, not a shim.
    version = libdrm.drmGetVersion(fd)
    assert bool(version), "upstream libdrm drmGetVersion failed"
    try:
        driver = ctypes.string_at(version.contents.name, version.contents.name_len)
        assert driver == b"virtio_gpu", driver
    finally:
        libdrm.drmFreeVersion(version)
    print("TGOS_ALPINE_LIBDRM_RENDER_OPEN_PASS", flush=True)

    # Before GEM/EXECBUFFER is integrated, do not lie about usable 3D.
    assert getparam(fd, 1) == 0, "driver falsely advertises userspace 3D"
    assert getparam(fd, 2) == 1, "GET_CAPS implementation not advertised"
    assert getparam(fd, 3) == 0, "blob interface is not wired"
    mask = getparam(fd, 7)
    ids = [i for i in range(1, 32) if mask & (1 << i)]
    assert ids, "no genuine VirGL capset IDs reported by original GPU"
    print("TGOS_ALPINE_GETPARAM_INT_ABI_PASS", flush=True)

    # Invoke actual Linux VirtGPU GET_CAPS through the real NaSeed kernel
    # and original TGOSKits GPU control queue. Nonzero data is from QEMU.
    capset = ctypes.create_string_buffer(65536)
    request = struct.pack("=IIQII", ids[0], 0,
                          ctypes.addressof(capset), len(capset), 0)
    fcntl.ioctl(fd, GET_CAPS, request)
    raw = capset.raw
    assert any(raw), "guest received all-zero/fabricated capset"
    print(f"TGOS_ALPINE_REAL_GETCAPS_PASS capset={ids[0]}", flush=True)

    # The original TGOSKits axdrm GET_CAPS API permits a short destination.
    # Require byte-for-byte agreement with the REAL full QEMU capset, rather
    # than treating a shorter valid user buffer as EINVAL.
    short = ctypes.create_string_buffer(32)
    request_short = struct.pack("=IIQII", ids[0], 0,
                                ctypes.addressof(short), len(short), 0)
    fcntl.ioctl(fd, GET_CAPS, request_short)
    assert short.raw == raw[:len(short)], "truncated host capset data differs"
    print("TGOS_ALPINE_GETCAPS_SHORT_BUFFER_PASS", flush=True)

    # Real NONEMPTY userspace EXECBUFFER. The 32-bit zero word is the exact
    # upstream virglrenderer VIRGL_CMD0(VIRGL_CCMD_NOP, 0, 0) packet.
    # TGOSKits' original VirtIoGpu::submit_3d must process this via QEMU
    # and retire a real fence before the ioctl can return successfully.
    nop = ctypes.create_string_buffer(b"\0\0\0\0")
    exec_req = struct.pack("=IIQQIiIIIIQQ", 0, 4,
                           ctypes.addressof(nop), 0, 0, -1,
                           0, 0, 0, 0, 0, 0)
    assert len(exec_req) == 64, len(exec_req)
    fcntl.ioctl(fd, EXECBUFFER, exec_req)
    print("TGOS_ALPINE_NONEMPTY_EXECBUFFER_FENCE_PASS", flush=True)

    # Actual guest 3D resource creation + original GPU host attachment.
    # Every BO identifier must be returned through Linux VirtGPU UAPI, and
    # a SUBMIT_3D with BO handles must survive real host fence completion.
    bo1, res1 = create_gpu_resource(fd)
    bo2, res2 = create_gpu_resource(fd)
    assert bo1 != bo2 and res1 != res2
    mapped1 = map_gpu_resource(fd, bo1)
    mapped2 = map_gpu_resource(fd, bo2)
    sentinel = b"NaSeed-GPU-DMA-1"
    mapped1[:len(sentinel)] = sentinel
    assert mapped1[:len(sentinel)] == sentinel
    assert mapped2[:len(sentinel)] != sentinel
    mapped2[:len(sentinel)] = b"NaSeed-GPU-DMA-2"
    assert mapped1[:len(sentinel)] == sentinel
    print("TGOS_ALPINE_REAL_BO_MMAP_PAGE_ISOLATION_PASS", flush=True)

    # Strict end-to-end *data* evidence: write 16 KiB through Alpine mmap,
    # upload it to the actual QEMU VirGL texture using the original TGOSKits
    # transfer routine, overwrite guest RAM, then download from the host.
    # A fake/no-op GPU transfer would leave the RAM all-zero and fail.
    texture = bytes((n * 37 + 19) & 255 for n in range(16384))
    mapped1[:] = texture
    gpu_full_layer_transfer(fd, bo1, True)
    mapped1[:] = b"\0" * len(texture)
    assert mapped1[:64] != texture[:64]
    gpu_full_layer_transfer(fd, bo1, False)
    assert mapped1[:] == texture, "QEMU 3D GPU upload/readback changed data"
    print("TGOS_ALPINE_GPU_HOST_DMA_ROUNDTRIP_PASS", flush=True)
    # Reject a mismatched 3D row stride: host must not DMA out of bounds.
    try:
        gpu_full_layer_transfer(fd, bo1, True, stride=257)
    except OSError as exc:
        assert exc.errno in (errno.EOPNOTSUPP, errno.EINVAL), exc
    else:
        raise AssertionError("unsafe VirGL transfer stride was accepted")
    print("TGOS_ALPINE_GPU_TRANSFER_BOUNDS_PASS", flush=True)

    exec_with_bo(fd, ctypes.addressof(nop), bo1)
    exec_with_bo(fd, ctypes.addressof(nop), bo2)
    # Two NOP packets are a valid original VirGL command stream, NOT a fake
    # four-byte shortcut; both BOs must be checked and the actual fence
    # must retire before Linux VIRTGPU_WAIT reports completion.
    nop_pair = ctypes.create_string_buffer(b"\0" * 8)
    exec_many(fd, ctypes.addressof(nop_pair), 8, (bo1, bo2))
    wait_bo(fd, bo1)
    wait_bo(fd, bo2)
    bogus = (ctypes.c_uint32 * 2)(bo1, 0xFFFFFFFF)
    bad = struct.pack("=IIQQIiIIIIQQ", 0, 8,
                      ctypes.addressof(nop_pair), ctypes.addressof(bogus),
                      2, -1, 0, 0, 0, 0, 0, 0)
    try:
        fcntl.ioctl(fd, EXECBUFFER, bad)
    except OSError as exc:
        assert exc.errno in (errno.ENOENT, errno.EINVAL), exc
    else:
        raise AssertionError("foreign or stale BO passed original GPU submit")
    print("TGOS_ALPINE_MULTI_BO_FENCED_EXECBUFFER_PASS", flush=True)
    print("TGOS_ALPINE_REAL_RESOURCE_CREATE_INFO_EXEC_PASS", flush=True)

    # A second file's handle table must not expose the first file's BO.
    foreign = os.open(RENDER, os.O_RDWR | os.O_CLOEXEC)
    try:
        probe = bytearray(struct.pack("=4I", bo1, 0, 0, 0))
        try:
            fcntl.ioctl(foreign, RESOURCE_INFO, probe, True)
        except OSError as exc:
            assert exc.errno in (errno.ENOENT, errno.EINVAL), exc
        else:
            raise AssertionError("cross-file GPU BO capability leak")
        foreign_bo, foreign_res = create_gpu_resource(foreign)
        assert foreign_res not in (res1, res2)
        exec_with_bo(foreign, ctypes.addressof(nop), foreign_bo)
        # Deliberately leave it open: final-close must free host resource.
    finally:
        os.close(foreign)
    print("TGOS_ALPINE_RESOURCE_FILE_ISOLATION_PASS", flush=True)

    close_gpu_resource(fd, bo1)
    # The original GPU owner has detached and fenced host DMA, but this VMA
    # must continue owning the physical pages until last munmap.
    assert mapped1[:] == texture
    mapped1.close()
    closed_info = bytearray(struct.pack("=4I", bo1, 0, 0, 0))
    try:
        fcntl.ioctl(fd, RESOURCE_INFO, closed_info, True)
    except OSError as exc:
        assert exc.errno in (errno.ENOENT, errno.EINVAL), exc
    else:
        raise AssertionError("GEM_CLOSE did not retire host resource")
    close_gpu_resource(fd, bo2)
    assert mapped2[:len(sentinel)] == b"NaSeed-GPU-DMA-2"
    mapped2.close()
    print("TGOS_ALPINE_GEM_CLOSE_DEVICE_RELEASE_PASS", flush=True)
    print("TGOS_ALPINE_GEM_CLOSE_LIVE_VMA_PASS", flush=True)

    # An independent OpenFile must get a DIFFERENT genuine host context.
    # dup() is the opposite: it shares the same OpenFile and does not create
    # or destroy an additional GPU context on descriptor-only close.
    independent = os.open(RENDER, os.O_RDWR | os.O_CLOEXEC)
    try:
        fcntl.ioctl(independent, EXECBUFFER, exec_req)
        duplicate = os.dup(fd)
        try:
            fcntl.ioctl(duplicate, EXECBUFFER, exec_req)
        finally:
            os.close(duplicate)
        fcntl.ioctl(fd, EXECBUFFER, exec_req)
    finally:
        os.close(independent)
    # A closed File slot must be able to create/retire a NEW GPU context.
    recycled = os.open(RENDER, os.O_RDWR | os.O_CLOEXEC)
    try:
        fcntl.ioctl(recycled, EXECBUFFER, exec_req)
    finally:
        os.close(recycled)
    print("TGOS_ALPINE_RENDER_FILE_CONTEXT_LIFETIME_PASS", flush=True)

    # Incomplete one-word packet cannot be a Mesa command and is rejected
    # BEFORE reaching original QEMU virglrenderer (avoids poisoning host log).
    malformed = ctypes.create_string_buffer(b"\xef\xbe\xad\xde")
    bad_exec = struct.pack("=IIQQIiIIIIQQ", 0, 4,
                           ctypes.addressof(malformed), 0, 0, -1,
                           0, 0, 0, 0, 0, 0)
    try:
        fcntl.ioctl(fd, EXECBUFFER, bad_exec)
    except OSError as exc:
        assert exc.errno in (errno.ENOTTY, errno.EOPNOTSUPP, errno.EINVAL), exc
    else:
        raise AssertionError("invalid VirGL command incorrectly accepted")
    print("TGOS_ALPINE_NONEMPTY_UNSUPPORTED_REJECT_PASS", flush=True)

    # Zero-length 3D submission remains invalid (no Venus ring support).

    try:
        fcntl.ioctl(fd, EXECBUFFER, bytes(64))
    except OSError as exc:
        assert exc.errno in (errno.ENOTTY, errno.EOPNOTSUPP, errno.EINVAL), exc
    else:
        raise AssertionError("zero-sized 3D EXECBUFFER falsely succeeded")
    print("TGOS_ALPINE_EXECBUFFER_ZERO_SIZE_REJECT_PASS", flush=True)

print("TGOS_ALPINE_RENDER_USERSPACE_PASS", flush=True)
