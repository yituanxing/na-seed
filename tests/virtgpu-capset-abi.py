#!/usr/bin/env python3
"""Verify genuine Linux VirtGPU discovery ioctls from unmodified Alpine Python.

This checks standard Linux DRM UAPI against host-returned GPU capability data.
It is not an OpenGL renderer, a substitute for Mesa or a fake /dev/dri node.
"""
import ctypes
import errno
import fcntl
import os
import struct

DRM_IOCTL_VERSION = 0xC0406400
DRM_IOCTL_VIRTGPU_GETPARAM = 0xC0106443
DRM_IOCTL_VIRTGPU_GET_CAPS = 0xC0186449


def getparam(fd, param):
    value = ctypes.c_uint64(0xBAD0BAD0BAD0BAD0)
    fcntl.ioctl(fd, DRM_IOCTL_VIRTGPU_GETPARAM,
                struct.pack("<QQ", param, ctypes.addressof(value)))
    return value.value


def get_caps(fd, capset_id, version):
    blob = ctypes.create_string_buffer(2048)
    request = struct.pack("<IIQII", capset_id, version,
                          ctypes.addressof(blob), ctypes.sizeof(blob), 0)
    fcntl.ioctl(fd, DRM_IOCTL_VIRTGPU_GET_CAPS, request)
    return bytes(blob)


with os.fdopen(os.open("/dev/dri/card0", os.O_RDWR), "rb", closefd=True) as opened:
    fd = opened.fileno()
    # Native kernel reply must name the real upstream VirtIO GPU driver.
    name = ctypes.create_string_buffer(64)
    version = bytearray(struct.pack("<iiiIQQQQQQ", 0, 0, 0, 0,
                                    64, ctypes.addressof(name),
                                    0, 0, 0, 0))
    fcntl.ioctl(fd, DRM_IOCTL_VERSION, version, True)
    assert name.value == b"virtio_gpu", name.value

    # A real host VirGL is present, but NaSeed must not yet claim guest Mesa
    # acceleration until render node / EXECBUFFER / resource life are ready.
    assert getparam(fd, 1) == 0, "premature 3D userspace claim"
    assert getparam(fd, 2) == 1, "capset query fix missing"
    supported = getparam(fd, 7)
    assert supported & (1 << 2), f"VirGL2 missing: {supported:#x}"

    # Actual Mesa winsys zero-initializes cap_set_ver and requests id 2,
    # version 0. Do not replace it with invented version-2 capability data.
    # Query both: each must be a real host-specific GET_CAPSET response.
    first = get_caps(fd, 2, 0)
    second = get_caps(fd, 2, 0)
    newest = get_caps(fd, 2, 2)
    assert any(newest[:1384]), "host max-version capset all-zero"
    assert first == second, "unstable host capset snapshot"
    assert any(first[:1384]), "host capset unexpectedly all-zero"
    assert first[1384:] == bytes(2048 - 1384), "capset copy overrun"

    # A version not obtained from the host must NOT masquerade as supported.
    try:
        get_caps(fd, 2, 0xFFFFFFFF)
    except OSError as exc:
        assert exc.errno == errno.EINVAL, exc
    else:
        raise AssertionError("uncached capset version accepted")

print(f"NASEED_VIRGL_CAPS_USER_PASS id=2 version=0 bytes=1384 mask={supported:#x}", flush=True)
