# M171 — Original LVGL 9.4 GUI in NaSeed user space

**Source provenance:** upstream LVGL v9.4.0, exact Git commit
`c016f72d4c125098287be5e83c0f1abed4706ee5`, pinned as the
`third_party/lvgl` git submodule. This is a real library build, not an
API-shaped stub or reimplementation.

The separate `naseed-lvgl` Linux ELF creates an upstream LVGL display,
draw buffers, pointer indev, a real button, labels and an event handler.
The LVGL display flush callback writes only clipped rectangles to the
client's **own** XRGB32 memfd (with opaque alpha). The existing compositor
receives the same SCM_RIGHTS descriptor protocol as M168 and composites
the surface; it does not link LVGL or open another framebuffer.

The compositor launches four independent clients in M171/no-arg mode:
ImageMagick image viewer, persistent BusyBox PTY terminal, Lynx/MC text
browser, and real LVGL application. It transforms QEMU physical tablet
coordinates into client-local MSG_POINTER press/release messages. LVGL
consumes those through its own input callback, repaints a button after
a click and commits through MSG_UPDATED to the compositor.

M171 QMP acceptance checks three **physical screenshots**: initial UI,
button click after a real pointer press/release with a pixel-difference
oracle, and independent removal of the LVGL window while the original
image, terminal and browser processes remain. In addition, the host
runs the real pinned LVGL self-test using the same executable source
and verifies that a button event changes shared XRGB32 pixels.

Signed Alpine ELF programs are not counted again or recompiled. The
M168 overlay is reused; only NaSeed-built static programs are added.
M167–M170 case selectors and tests remain unchanged. No kernel desktop
syscall, GPU driver, or direct /dev/fb0 access is added to GUI clients.

Limitations: not a general purpose window system; this is a bounded,
fixed bootstrap client table. No arbitrary external app registrations,
international fonts, keyboard navigation, GPU, or resize protocol yet.
