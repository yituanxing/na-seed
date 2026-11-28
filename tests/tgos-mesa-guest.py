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
# The real NaSeed PCI/sysfs device tree is now implemented. Keep Mesa's
# upstream driver-selection override for repeatable VirGL troubleshooting,
# NOT as a software fallback; GL_RENDERER is checked independently.
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

# The real generic NaSeed sysfs provider now projects the same PCI owner to
# Linux's /sys/dev/char and /sys/class/drm. libdrm requires this BEFORE EGL
# can select a rendering EGLDevice; a driver-name override alone is insufficient.
device_path = "/sys/dev/char/226:128/device"
assert os.path.isdir(device_path), "original GPU PCI device missing from real sysfs"
with open(device_path + "/vendor", "rt") as vendor_file:
    vendor = int(vendor_file.read().strip(), 16)
with open(device_path + "/device", "rt") as device_file:
    pci_device = int(device_file.read().strip(), 16)
with open(device_path + "/uevent", "rt") as uevent_file:
    uevent = uevent_file.read()
assert vendor == 0x1AF4 and pci_device == 0x1050, (
    f"GPU sysfs identity differs from QEMU PCI bus: {vendor:#x}:{pci_device:#x}"
)
assert "PCI_SLOT_NAME=" in uevent, "libdrm cannot identify GPU PCI slot"
print("TGOS_MESA_REAL_SYSFS_PCI_PASS", flush=True)

libdrm = C.CDLL("libdrm.so.2")
libdrm.drmGetDevice2.argtypes = [C.c_int, C.c_uint32, C.POINTER(C.c_void_p)]
libdrm.drmGetDevice2.restype = C.c_int
libdrm.drmFreeDevice.argtypes = [C.POINTER(C.c_void_p)]
drm_device = C.c_void_p()
drm_rc = libdrm.drmGetDevice2(fd, 0, C.byref(drm_device))
assert drm_rc == 0 and drm_device.value, (
    f"stock Alpine libdrm drmGetDevice2 failed: {drm_rc}"
)
libdrm.drmFreeDevice(C.byref(drm_device))
print("TGOS_MESA_LIBDRM_DEVICE_ENUM_PASS", flush=True)


egl = C.CDLL("libEGL.so.1")
egl.eglGetProcAddress.argtypes = [C.c_char_p]
egl.eglGetProcAddress.restype = C.c_void_p
egl.eglInitialize.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_int)]
egl.eglInitialize.restype = C.c_uint
egl.eglChooseConfig.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_void_p), C.c_int, C.POINTER(C.c_int)]
egl.eglChooseConfig.restype = C.c_uint
egl.eglGetConfigs.argtypes = [C.c_void_p, C.POINTER(C.c_void_p), C.c_int, C.POINTER(C.c_int)]
egl.eglGetConfigs.restype = C.c_uint
egl.eglGetConfigAttrib.argtypes = [C.c_void_p, C.c_void_p, C.c_int, C.POINTER(C.c_int)]
egl.eglGetConfigAttrib.restype = C.c_uint
egl.eglQueryString.argtypes = [C.c_void_p, C.c_int]
egl.eglQueryString.restype = C.c_char_p
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

# EGL platform discovery is not a request to fabricate a config. A GBM
# render node may expose EGL_WINDOW_BIT only, or surfaceless contexts. Query
# the REAL unmodified Alpine Mesa implementation before choosing the path.
exts = (egl.eglQueryString(dpy, 0x3055) or b"").decode(errors="replace")
client_apis = (egl.eglQueryString(dpy, 0x308D) or b"").decode(errors="replace")
vendor = (egl.eglQueryString(dpy, 0x3053) or b"").decode(errors="replace")
print("TGOS_MESA_EGL_VENDOR: " + vendor, flush=True)
print("TGOS_MESA_EGL_APIS: " + client_apis, flush=True)
print("TGOS_MESA_EGL_EXTENSIONS: " + exts, flush=True)
EGL_NONE = 0x3038
EGL_SURFACE_TYPE = 0x3033
EGL_PBUFFER_BIT = 0x0001
EGL_RENDERABLE_TYPE = 0x3040
EGL_OPENGL_ES2_BIT = 0x0004
EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE, EGL_ALPHA_SIZE = 0x3024, 0x3023, 0x3022, 0x3021
num_configs = C.c_int()
assert egl.eglGetConfigs(dpy, None, 0, C.byref(num_configs)), (
    f"eglGetConfigs query failed: {egl.eglGetError():#x}"
)
print(f"TGOS_MESA_EGL_CONFIG_COUNT: {num_configs.value}", flush=True)
all_configs = (C.c_void_p * min(512, max(1, num_configs.value)))()
cfg_count = C.c_int()
if num_configs.value:
    assert egl.eglGetConfigs(dpy, all_configs, len(all_configs), C.byref(cfg_count)), (
        f"eglGetConfigs enumeration failed: {egl.eglGetError():#x}"
    )

def config_attr(config, attribute):
    value = C.c_int()
    if not egl.eglGetConfigAttrib(dpy, config, attribute, C.byref(value)):
        raise RuntimeError(f"eglGetConfigAttrib({attribute:#x}) failed: {egl.eglGetError():#x}")
    return value.value

# Require real ES2 support, prefer RGBA8 and a native pbuffer when possible.
# A surfaceless ES2+FBO path is legitimate original GLES rendering and avoids
# assuming QEMU's render node also offers KMS/window surfaces.
choice = None
have_pbuffer = False
for i in range(cfg_count.value):
    config = all_configs[i]
    surf = config_attr(config, EGL_SURFACE_TYPE)
    rend = config_attr(config, EGL_RENDERABLE_TYPE)
    red = config_attr(config, EGL_RED_SIZE)
    green = config_attr(config, EGL_GREEN_SIZE)
    blue = config_attr(config, EGL_BLUE_SIZE)
    alpha = config_attr(config, EGL_ALPHA_SIZE)
    if i < 10:
        print(f"TGOS_MESA_EGL_CONFIG_{i} surf={surf:#x} render={rend:#x} "
              f"rgba={red}/{green}/{blue}/{alpha}", flush=True)
    if not (rend & EGL_OPENGL_ES2_BIT) or min(red,green,blue) < 8:
        continue
    candidate_pbuffer = bool(surf & EGL_PBUFFER_BIT)
    if choice is None or (candidate_pbuffer and not have_pbuffer):
        choice = config
        have_pbuffer = candidate_pbuffer

extensions = set(exts.split())
if choice is None:
    assert "EGL_KHR_no_config_context" in extensions or "EGL_MESA_configless_context" in extensions, (
        "No original EGL GLES2 configs and no EGL_KHR_no_config_context "
        "(renderer not ready; see config/APIs diagnostics above)"
    )
    print("TGOS_MESA_EGL_NO_CONFIG_CONTEXT", flush=True)
    choice = C.c_void_p()
assert egl.eglBindAPI(0x30A0), "EGL_OPENGL_ES_API unavailable"
context_attribs = (C.c_int * 3)(0x3098, 2, EGL_NONE)
ctx = egl.eglCreateContext(dpy, choice, None, context_attribs)
assert ctx, f"real Mesa eglCreateContext failed: {egl.eglGetError():#x}"
surface = C.c_void_p()
if have_pbuffer:
    surface_attribs = (C.c_int * 5)(0x3057, 64, 0x3056, 64, EGL_NONE)
    surface = egl.eglCreatePbufferSurface(dpy, choice, surface_attribs)
    assert surface, f"real Mesa eglCreatePbufferSurface failed: {egl.eglGetError():#x}"
    print("TGOS_MESA_EGL_PBUFFER_PATH", flush=True)
else:
    assert "EGL_KHR_surfaceless_context" in extensions, (
        "Mesa has no pbuffer and no EGL_KHR_surfaceless_context"
    )
    print("TGOS_MESA_EGL_SURFACELESS_PATH", flush=True)
assert egl.eglMakeCurrent(dpy, surface, surface, ctx), (
    f"real Mesa eglMakeCurrent failed: {egl.eglGetError():#x}"
)
print("TGOS_MESA_REAL_GLES2_CONTEXT_PASS", flush=True)

gles = C.CDLL("libGLESv2.so.2")
gles.glGetString.argtypes = [C.c_uint]
gles.glGetString.restype = C.c_char_p
gles.glClearColor.argtypes = [C.c_float, C.c_float, C.c_float, C.c_float]
gles.glClear.argtypes = [C.c_uint]
gles.glFinish.argtypes = []
gles.glFinish.restype = None
gles.glViewport.argtypes = [C.c_int, C.c_int, C.c_int, C.c_int]
gles.glGenTextures.argtypes = [C.c_int, C.POINTER(C.c_uint)]
gles.glBindTexture.argtypes = [C.c_uint, C.c_uint]
gles.glTexParameteri.argtypes = [C.c_uint, C.c_uint, C.c_int]
gles.glTexImage2D.argtypes = [C.c_uint, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p]
gles.glGenFramebuffers.argtypes = [C.c_int, C.POINTER(C.c_uint)]
gles.glBindFramebuffer.argtypes = [C.c_uint, C.c_uint]
gles.glFramebufferTexture2D.argtypes = [C.c_uint, C.c_uint, C.c_uint, C.c_uint, C.c_int]
gles.glCheckFramebufferStatus.argtypes = [C.c_uint]
gles.glCheckFramebufferStatus.restype = C.c_uint
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

# Render to an actual GLES2 texture/FBO instead of relying on window-system
# scanout. The real Mesa VirGL Gallium driver emits all resource and drawing
# commands. No manually encoded VirGL commands, no synthesized pixel results.
GL_TEXTURE_2D, GL_FRAMEBUFFER = 0x0DE1, 0x8D40
GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_COMPLETE = 0x8CE0, 0x8CD5
GL_RGBA, GL_UNSIGNED_BYTE = 0x1908, 0x1401
tex = C.c_uint()
gles.glGenTextures(1, C.byref(tex))
assert tex.value != 0, "Mesa failed to allocate a real GLES2 texture"
gles.glBindTexture(GL_TEXTURE_2D, tex.value)
gles.glTexParameteri(GL_TEXTURE_2D, 0x2801, 0x2600)  # MIN_FILTER NEAREST
gles.glTexParameteri(GL_TEXTURE_2D, 0x2800, 0x2600)  # MAG_FILTER NEAREST
gles.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, None)
fbo = C.c_uint()
gles.glGenFramebuffers(1, C.byref(fbo))
gles.glBindFramebuffer(GL_FRAMEBUFFER, fbo.value)
gles.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex.value, 0)
status = gles.glCheckFramebufferStatus(GL_FRAMEBUFFER)
assert status == GL_FRAMEBUFFER_COMPLETE, f"real Mesa FBO incomplete: {status:#x}"
gles.glViewport(0, 0, 64, 64)
print("TGOS_MESA_REAL_GLES2_FBO_PASS", flush=True)
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
