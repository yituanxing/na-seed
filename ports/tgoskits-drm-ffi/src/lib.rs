// SPDX-License-Identifier: MIT
// Minimal *real* kdrm-core C ABI reachability probe for NaSeed.
// This invokes original TGOSKits functions, never fake DRM ioctls and
// never claims that a 3D driver or Mesa is operational.
#![no_std]
#![feature(alloc_error_handler)]

extern crate alloc;
use alloc::alloc::{alloc, dealloc};
use core::alloc::{GlobalAlloc, Layout};
use core::panic::PanicInfo;
use drm_kapi::kernel_services::ProviderSlot;

unsafe extern "C" {
    fn nas_tgos_alloc_aligned(size: usize, align: usize) -> *mut u8;
    fn nas_tgos_free_aligned(ptr: *mut u8);
    fn nas_tgos_probe_stage(stage: u32);
    fn nas_tgos_panic_report(kind: u32);
}

// Real NaSeed TLSF provider. The kernel heap is process-context-only;
// this adapter MUST NOT be invoked in IRQ or while holding a heap lock.
struct NaSeedHeap;
#[global_allocator]
static ALLOC: NaSeedHeap = NaSeedHeap;
unsafe impl GlobalAlloc for NaSeedHeap {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        unsafe { nas_tgos_alloc_aligned(layout.size(), layout.align()) }
    }
    unsafe fn dealloc(&self, p: *mut u8, _layout: Layout) {
        unsafe { nas_tgos_free_aligned(p) }
    }
}

#[panic_handler]
fn panic(_: &PanicInfo<'_>) -> ! {
    unsafe { nas_tgos_panic_report(1) };
    loop { core::hint::spin_loop() }
}

#[alloc_error_handler]
fn alloc_error(_: Layout) -> ! {
    unsafe { nas_tgos_panic_report(2) };
    loop { core::hint::spin_loop() }
}

// Deliberately tests only an independent ProviderSlot to avoid installing
// invented process/file/eventfd semantics into the real DRM-KAPI globals.
static TICKET: u32 = 0x44524d31;
static SLOT: ProviderSlot<u32> = ProviderSlot::new();

#[unsafe(no_mangle)]
pub extern "C" fn nas_tgos_original_drm_ffi_selftest() -> i32 {
    unsafe { nas_tgos_probe_stage(100) };
    // Execute the actual upstream Linux-ported DRM FOURCC functions.
    let fourcc = kdrm_core::drm_fourcc_upstream::drm_mode_legacy_fb_format(32, 24);
    unsafe { nas_tgos_probe_stage(101) };
    if fourcc != 0x3432_5258 { // DRM_FORMAT_XRGB8888, not an emulation.
        return -1;
    }
    let info = kdrm_core::drm_fourcc_upstream::drm_format_info(fourcc);
    unsafe { nas_tgos_probe_stage(102) };
    if info.is_null() {
        return -2;
    }
    // Access the same original C-layout DRM metadata consumed by KMS.
    let (fmt, depth, planes) = unsafe {
        ((*info).format, (*info).depth, (*info).num_planes)
    };
    unsafe { nas_tgos_probe_stage(103) };
    if fmt != fourcc || depth != 24 || planes != 1 {
        return -3;
    }
    // Verify the upstream DRM-KAPI install-once ownership machinery.
    unsafe { nas_tgos_probe_stage(104) };
    if SLOT.install(&TICKET).is_err() || SLOT.get().copied() != Some(TICKET) {
        return -4;
    }
    unsafe { nas_tgos_probe_stage(105) };
    // Same Linux UAPI constant as NaSeed's existing DRM primary char device.
    if kdrm_core::uapi_drm_consts::DRM_IOCTL_VERSION != 0xc040_6400 {
        return -5;
    }
    // REAL NaSeed TLSF-backed allocations through the Rust GlobalAlloc
    // callback. These are deliberately *not* GPU DMA buffers: the test
    // checks C/Rust heap ownership and the 64/4096-byte alignment contract.
    // Executed from NaSeed's process-context DRM registration, never IRQ.
    unsafe { nas_tgos_probe_stage(106) };
    for (index, alignment) in [8usize, 16, 64, 4096].into_iter().enumerate() {
        unsafe { nas_tgos_probe_stage(10 + 10 * index as u32) };
        let layout = match Layout::from_size_align(513, alignment) {
            Ok(layout) => layout,
            Err(_) => return -6,
        };
        // SAFETY: valid layout; non-null pointer comes from the selected
        // kernel TLSF global allocator. Deallocated once with same layout.
        let ptr = unsafe { alloc(layout) };
        unsafe { nas_tgos_probe_stage(11 + 10 * index as u32) };
        if ptr.is_null() {
            return -7;
        }
        if (ptr as usize) & (alignment - 1) != 0 {
            unsafe { dealloc(ptr, layout) };
            return -8;
        }
        unsafe { core::ptr::write_bytes(ptr, 0xa5, layout.size()) };
        let last = unsafe { *ptr.add(layout.size() - 1) };
        unsafe { nas_tgos_probe_stage(12 + 10 * index as u32) };
        unsafe { dealloc(ptr, layout) };
        unsafe { nas_tgos_probe_stage(13 + 10 * index as u32) };
        if last != 0xa5 {
            return -9;
        }
    }
    0
}
