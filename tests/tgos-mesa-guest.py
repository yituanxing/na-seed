#!/usr/bin/env python3
"""Unmodified Alpine Mesa GBM/EGL/GLES2 -> NaSeed DRM render node -> QEMU virgl.

No synthetic pixel buffers, no software fallback, no hand-encoded 3D
commands. All drawing is performed by original Mesa libGLESv2.so.2.
"""
import ctypes as C
import os
import sys

os.environ["LIBGL_ALWAYS_SOFTWARE"] = "0"
os.environ["EGL_PLATFORM"] = "gbm"
# NaSeed has a real Linux DRM render node but /sys/dev/char/226:128 is not
# yet implemented. This is Mesa's documented driver-selection override,
# NOT a software fallback; the actual guest renderer must still be VIRGL.
os.environ["MESA_LOADER_DRIVER_OVERRIDE"] = "virtio_gpu"
os.environ["LIBGL_DEBUG"] = "verbose"

# The host must have loaded a REAL QEMU virtio-gpu-gl device. GBM gets the
# existing NaSeed Linux DRM render node; no vtest or llvmpipe fallback.
fd = os.open("/dev/dri/renderD128", os.O_RDWR | os.O_CLOEXEC)
gbm = C.CDLL("libgbm.so.1")
gbm.gbm_create_device.argtypes = [C.c_int]
gbm.gbm_create_device.restype = C.c_void_p
gbm.gbm_device_destroy.argtypes = [C.c_void_p]
device = gbm.gbm_create_device(fd)
assert device, "Alpine Mesa GBM refused NaSeed virtio_gpu DRM renderD128"
print("TGOS_MESA_GBM_RENDER_NODE_PASS", flush=True)

egl = C.CDLL("libEGL.so.1")
egl.eglGetProcAddress.argtypes = [C.c_char_p]
egl.eglGetProcAddress.restype = C.c_void_p
egl.eglInitialize.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_int)]
egl.eglInitialize.restype = C.c_uint
egl.eglChooseConfig.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_void_p), C.c_int, C.POINTER(C.c_int)]
egl.eglChooseConfig.restype = C.c_uint
egl.eglBindAPI.argtypes = [C.c_uint]
egl.eglBindAPI.restype = C.c_uint
egl.eglCreateContext.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.POINTER(C.c_int)]
egl.eglCreateContext.restype = C.c_void_p
egl.eglCreatePbufferSurface.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(C.c_int)]
egl.eglCreatePbufferSurface.restype = C.c_void_p
egl.eglMakeCurrent.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p]
egl.eglMakeCurrent.restype = C.c_uint
egl.eglGetError.restype = C.c_uint

get_platform_address = egl.eglGetProcAddress(b"eglGetPlatformDisplayEXT")
assert get_platform_address, "Alpine Mesa EGL missing GBM platform support"
PlatformDisplay = C.CFUNCTYPE(C.c_void_p, C.c_uint, C.c_void_p, C.c_void_p)
platform_display = PlatformDisplay(get_platform_address)
EGL_PLATFORM_GBM_KHR = 0x31D7
EGL_NONE = 0x3038
dpy = platform_display(EGL_PLATFORM_GBM_KHR, device, None)
assert dpy, f"eglGetPlatformDisplay(GBM) failed: {egl.eglGetError():#x}"
major, minor = C.c_int(), C.c_int()
assert egl.eglInitialize(dpy, C.byref(major), C.byref(minor)), (
    f"Alpine Mesa EGL virgl initialize failed: {egl.eglGetError():#x}"
)
print(f"TGOS_MESA_EGL_INITIALIZE_PASS {major.value}.{minor.value}", flush=True)

# EGL window-system-independent pbuffer; display scanout is not required.
EGL_SURFACE_TYPE = 0x3033
EGL_PBUFFER_BIT = 0x0001
EGL_RENDERABLE_TYPE = 0x3040
EGL_OPENGL_ES2_BIT = 0x0004
EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE, EGL_ALPHA_SIZE = 0x3024, 0x3023, 0x3022, 0x3021
config_attribs = (C.c_int * 13)(
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE,
)
cfg = C.c_void_p()
num_cfg = C.c_int()
assert egl.eglChooseConfig(dpy, config_attribs, C.byref(cfg), 1, C.byref(num_cfg)) and num_cfg.value, (
    f"unmodified Mesa did not expose RGBA8 GLES2 pbuffer: {egl.eglGetError():#x}"
)
assert egl.eglBindAPI(0x30A0), "EGL_OPENGL_ES_API unavailable"
context_attribs = (C.c_int * 3)(0x3098, 2, EGL_NONE)
ctx = egl.eglCreateContext(dpy, cfg, None, context_attribs)
assert ctx, f"eglCreateContext failed: {egl.eglGetError():#x}"
surface_attribs = (C.c_int * 5)(0x3057, 64, 0x3056, 64, EGL_NONE)
surface = egl.eglCreatePbufferSurface(dpy, cfg, surface_attribs)
assert surface, f"eglCreatePbufferSurface failed: {egl.eglGetError():#x}"
assert egl.eglMakeCurrent(dpy, surface, surface, ctx), (
    f"eglMakeCurrent failed: {egl.eglGetError():#x}"
)

gles = C.CDLL("libGLESv2.so.2")
gles.glGetString.argtypes = [C.c_uint]
gles.glGetString.restype = C.c_char_p
gles.glClearColor.argtypes = [C.c_float, C.c_float, C.c_float, C.c_float]
gles.glClear.argtypes = [C.c_uint]
gles.glReadPixels.argtypes = [C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p]
gles.glGetError.restype = C.c_uint

renderer = (gles.glGetString(0x1F01) or b"").decode(errors="replace")
version = (gles.glGetString(0x1F02) or b"").decode(errors="replace")
print("TGOS_MESA_GL_RENDERER: " + renderer, flush=True)
print("TGOS_MESA_GL_VERSION: " + version, flush=True)
lowered = renderer.lower()
assert "virgl" in lowered and "llvmpipe" not in lowered.split("virgl")[0], (
    "Mesa rendered using software fallback instead of VirtIO-GPU VirGL: " + renderer
)
print("TGOS_MESA_VIRGL_RENDERER_PASS", flush=True)

# Original Mesa emits the real nonempty 3D commands, not a hand-built VirGL
# packet. Read actual pixels through GLES2 (on top of QEMU virglrenderer).
gles.glClearColor(C.c_float(0.25), C.c_float(0.5), C.c_float(0.75), C.c_float(1.0))
gles.glClear(0x4000)  # GL_COLOR_BUFFER_BIT
gles.glFinish()
assert gles.glGetError() == 0, "actual GLES2 clear generated GL errors"
pixel = (C.c_uint8 * 4)()
gles.glReadPixels(31, 31, 1, 1, 0x1908, 0x1401, C.cast(pixel, C.c_void_p))
assert gles.glGetError() == 0, "actual GPU glReadPixels generated GL errors"
rgba = tuple(pixel)
print("TGOS_MESA_GL_CLEAR_RGBA: " + repr(rgba), flush=True)
assert all(abs(actual - goal) <= 2 for actual, goal in zip(rgba, (64, 128, 191, 255))), (
    "Mesa did not render expected QEMU VirGL clear pixels: " + repr(rgba)
)
print("TGOS_MESA_GL_CLEAR_PIXELS_PASS", flush=True)

egl.eglMakeCurrent(dpy, None, None, None)
gbm.gbm_device_destroy(device)
os.close(fd)
print("TGOS_MESA_REAL_APPLICATION_PASS", flush=True)
