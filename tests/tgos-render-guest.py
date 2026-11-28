#!/usr/bin/env python3
"""Genuine Alpine Python + unmodified libdrm -> NaSeed experimental renderD128.

This is a real *userspace* test of the original TGOSKits VirtIoGpu READ-ONLY
UAPI. It does not claim Mesa/GEM/EXECBUFFER support or GPU rendering.
"""
import ctypes
import errno
import fcntl
import os
import stat
import struct

RENDER = "/dev/dri/renderD128"
GETPARAM = 0xC0106443
GET_CAPS = 0xC0186449
EXECBUFFER = 0xC0406442


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

    # This read-only render node must FAIL unknown 3D command submission,
    # rather than silently claim it succeeded.
    try:
        fcntl.ioctl(fd, EXECBUFFER, bytes(64))
    except OSError as exc:
        assert exc.errno in (errno.ENOTTY, errno.EOPNOTSUPP, errno.EINVAL), exc
    else:
        raise AssertionError("unimplemented 3D EXECBUFFER falsely succeeded")
    print("TGOS_ALPINE_EXECBUFFER_NOT_READY_CONFIRMED", flush=True)

print("TGOS_ALPINE_RENDER_USERSPACE_PASS", flush=True)
