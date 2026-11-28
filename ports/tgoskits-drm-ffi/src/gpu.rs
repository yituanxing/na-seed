// SPDX-License-Identifier: MIT
// ORIGINAL TGOSKits virtio-gpu (pinned upstream) driven through NaSeed's
// QEMU-only transport/DMA providers. This is a REAL device-owner experiment,
// not a second protocol implementation or a render-node/DRM ioctl emulation.
use alloc::boxed::Box;
use core::{
    ffi::c_void,
    mem::{size_of, MaybeUninit},
    ptr::{self, NonNull},
    sync::atomic::{AtomicBool, AtomicPtr, Ordering},
};
use virtio_drivers::{
    BufferDirection, Error as VirtIoError, Hal,
    transport::{DeviceStatus, DeviceType, InterruptStatus, Transport},
};
use zerocopy::{FromBytes, Immutable, IntoBytes};
use virtio_gpu::VirtIoGpu;

unsafe extern "C" {
    fn nas_tgos_gpu_dma_alloc(pages: usize, out_pa: *mut u64, out_va: *mut *mut u8) -> i32;
    fn nas_tgos_gpu_dma_free(pa: u64, va: *mut u8, pages: usize) -> i32;
    fn nas_tgos_gpu_bounce_share(ptr: *mut u8, bytes: usize, direction: u32) -> u64;
    fn nas_tgos_gpu_bounce_unshare(pa: u64, ptr: *mut u8, bytes: usize, direction: u32) -> i32;
    fn nas_tgos_gpu_bounce_reap_after_reset();

    fn nas_tgos_gpu_vt_status(t: *const c_void) -> u32;
    fn nas_tgos_gpu_vt_set_status(t: *const c_void, value: u32);
    fn nas_tgos_gpu_vt_features(t: *const c_void) -> u64;
    fn nas_tgos_gpu_vt_set_features(t: *const c_void, features: u64);
    fn nas_tgos_gpu_vt_setup_queue(t: *const c_void, q: u32, n: u32, desc: u64, driver: u64, device: u64) -> i32;
    fn nas_tgos_gpu_vt_notify(t: *const c_void, q: u32);
    fn nas_tgos_gpu_vt_read(t: *const c_void, off: u32, dst: *mut c_void, len: u32) -> i32;
    fn nas_tgos_gpu_vt_write(t: *const c_void, off: u32, src: *const c_void, len: u32) -> i32;
    fn nas_tgos_gpu_vt_irq_ack(t: *const c_void) -> u32;
    fn nas_arch_monotonic_ns() -> u64;
}

// Unmodified virtio-drivers 0.13.0 requires a contiguous DMA address even for
// Rust heap requests. A direct virtual-to-physical cast is WRONG for TLSF
// (including its boot .bss arena), so every shared region uses an owned bounce
// mapping, copied back only after its used-ring completion.
struct NaSeedHal;
fn direction_code(d: BufferDirection) -> u32 {
    match d {
        BufferDirection::DriverToDevice => 1,
        BufferDirection::DeviceToDriver => 2,
        BufferDirection::Both => 3,
    }
}
// SAFETY: the C provider allocates zeroed, physically contiguous PMM frames
// with a 4KiB HHDM CPU mapping and retains them until paired release. share()
// retains an independent bounce allocation until unshare(), not until submit().
unsafe impl Hal for NaSeedHal {
    fn dma_alloc(pages: usize, _direction: BufferDirection) -> (u64, NonNull<u8>) {
        let mut pa = 0;
        let mut va = ptr::null_mut();
        let rc = unsafe { nas_tgos_gpu_dma_alloc(pages, &mut pa, &mut va) };
        (if rc == 0 { pa } else { 0 },
         NonNull::new(va).unwrap_or_else(NonNull::dangling))
    }
    unsafe fn dma_dealloc(pa: u64, va: NonNull<u8>, pages: usize) -> i32 {
        unsafe { nas_tgos_gpu_dma_free(pa, va.as_ptr(), pages) }
    }
    unsafe fn mmio_phys_to_virt(_paddr: u64, _size: usize) -> NonNull<u8> {
        // This implementation uses an already-mapped NaSeed C transport, NEVER
        // the native virtio-drivers PCI/MMIO constructors.
        panic!("TGOS GPU: unexpected direct MMIO translation")
    }
    unsafe fn share(buffer: NonNull<[u8]>, direction: BufferDirection) -> u64 {
        unsafe {
            nas_tgos_gpu_bounce_share(
                buffer.as_ptr() as *mut u8, buffer.len(), direction_code(direction))
        }
    }
    unsafe fn unshare(pa: u64, buffer: NonNull<[u8]>, direction: BufferDirection) {
        let rc = unsafe { nas_tgos_gpu_bounce_unshare(
            pa, buffer.as_ptr() as *mut u8, buffer.len(), direction_code(direction)) };
        if rc != 0 { panic!("TGOS GPU: cannot retire DMA mapping"); }
    }
}

// One owner of the C transport. Queue 0 is the 64-descriptor control queue
// required by the ORIGINAL crate. This first gate is intentionally QEMU-only;
// queue_set failures are fatal and detected before publishing a GPU device.
static BAD_QUEUE: AtomicBool = AtomicBool::new(false);
struct NaTransport {
    raw: *const c_void,
    queue_set: bool,
}
impl Transport for NaTransport {
    fn device_type(&self) -> DeviceType { DeviceType::GPU }
    fn read_device_features(&mut self) -> u64 {
        unsafe { nas_tgos_gpu_vt_features(self.raw) }
    }
    fn write_driver_features(&mut self, features: u64) {
        unsafe { nas_tgos_gpu_vt_set_features(self.raw, features); }
    }
    fn max_queue_size(&mut self, queue: u16) -> u32 {
        if queue == 0 { 64 } else { 0 }
    }
    fn notify(&mut self, queue: u16) {
        if !BAD_QUEUE.load(Ordering::Acquire) {
            unsafe { nas_tgos_gpu_vt_notify(self.raw, u32::from(queue)); }
        }
    }
    fn get_status(&self) -> DeviceStatus {
        DeviceStatus::from_bits_retain(unsafe { nas_tgos_gpu_vt_status(self.raw) })
    }
    fn set_status(&mut self, status: DeviceStatus) {
        unsafe { nas_tgos_gpu_vt_set_status(self.raw, status.bits()); }
    }
    fn set_guest_page_size(&mut self, _page_size: u32) {
        // Modern VirtIO 1.2 transport, legacy guest page-size register absent.
    }
    fn requires_legacy_layout(&self) -> bool { false }
    fn queue_set(&mut self, queue: u16, size: u32,
                 desc: u64, driver: u64, device: u64) {
        let rc = unsafe { nas_tgos_gpu_vt_setup_queue(
            self.raw, u32::from(queue), size, desc, driver, device) };
        if rc != 0 { BAD_QUEUE.store(true, Ordering::Release); }
        else { self.queue_set = true; }
    }
    fn queue_unset(&mut self, _queue: u16) {
        // Current NaSeed transport has no per-queue reset. Reset the ENTIRE
        // exclusively-owned device before the original queue DMA is dropped.
        unsafe { nas_tgos_gpu_vt_set_status(self.raw, 0); }
        self.queue_set = false;
    }
    fn queue_used(&mut self, queue: u16) -> bool {
        queue == 0 && self.queue_set
    }
    fn ack_interrupt(&mut self) -> InterruptStatus {
        InterruptStatus::from_bits_truncate(
            unsafe { nas_tgos_gpu_vt_irq_ack(self.raw) })
    }
    fn read_config_generation(&self) -> u32 {
        // Config generation is not currently exported by NaSeed's transport.
        // The bootstrap test only reads static GPU config, with no hotplug.
        0
    }
    fn read_config_space<T: FromBytes + IntoBytes>(&self, offset: usize)
        -> Result<T, VirtIoError> {
        if offset > u32::MAX as usize || size_of::<T>() > u32::MAX as usize {
            return Err(VirtIoError::InvalidParam);
        }
        let mut result = MaybeUninit::<T>::uninit();
        let rc = unsafe { nas_tgos_gpu_vt_read(self.raw, offset as u32,
            result.as_mut_ptr().cast::<c_void>(), size_of::<T>() as u32) };
        if rc != 0 { return Err(VirtIoError::ConfigSpaceTooSmall); }
        Ok(unsafe { result.assume_init() })
    }
    fn write_config_space<T: IntoBytes + Immutable>(&mut self, offset: usize,
        value: T) -> Result<(), VirtIoError> {
        if offset > u32::MAX as usize || size_of::<T>() > u32::MAX as usize {
            return Err(VirtIoError::InvalidParam);
        }
        let rc = unsafe { nas_tgos_gpu_vt_write(self.raw, offset as u32,
            (&raw const value).cast::<c_void>(), size_of::<T>() as u32) };
        if rc != 0 { Err(VirtIoError::IoError) } else { Ok(()) }
    }
}
fn clock() -> u64 { unsafe { nas_arch_monotonic_ns() } }

// No public method reads the device through this pointer yet: publication
// happens only after bootstrap has completed all blocking GPU checks. The bus
// remove callback owns shutdown, including transport reset before DMA release.
static DEVICE: AtomicPtr<VirtIoGpu<NaSeedHal, NaTransport>> =
    AtomicPtr::new(ptr::null_mut());

#[unsafe(no_mangle)]
pub extern "C" fn nas_tgos_original_gpu_probe(raw: *const c_void,
                                                capset_id: *mut u32) -> i32 {
    if raw.is_null() || capset_id.is_null() { return -22; }
    if !DEVICE.load(Ordering::Acquire).is_null() { return -16; }
    BAD_QUEUE.store(false, Ordering::Release);
    let transport = NaTransport { raw, queue_set: false };
    let mut gpu = match VirtIoGpu::<NaSeedHal, _>::new(transport, clock) {
        Ok(gpu) => gpu,
        Err(_) => {
            unsafe { nas_tgos_gpu_vt_set_status(raw, 0); }
            return -5;
        }
    };
    if BAD_QUEUE.load(Ordering::Acquire) || !gpu.has_virgl() {
        gpu.reset();
        unsafe { nas_tgos_gpu_bounce_reap_after_reset(); }
        return -95;
    }
    // Actual command submitted to QEMU, checked against original capset wire
    // response. No synthetic 3D response or NaSeed hand-coded GPU command.
    let capset = match gpu.get_capset_info(0) {
        Ok(info) if info.capset_id != 0 && info.max_size != 0 => info,
        _ => {
            gpu.reset();
            unsafe { nas_tgos_gpu_bounce_reap_after_reset(); }
            return -5;
        }
    };
    // Exercise the upstream 3D context submission path. This verifies used
    // ring completion, NOT full rendering or a successful Mesa EXECBUFFER.
    let ctx: u32 = 0x4e534544;
    if gpu.ctx_create(ctx, "naseed-gpu-import", 0).is_err() {
        gpu.reset();
        unsafe { nas_tgos_gpu_bounce_reap_after_reset(); }
        return -5;
    }
    gpu.ctrl_notify();
    if gpu.wait_idle().is_err() {
        gpu.reset();
        unsafe { nas_tgos_gpu_bounce_reap_after_reset(); }
        return -5;
    }
    if gpu.ctx_destroy(ctx).is_err() {
        gpu.reset();
        unsafe { nas_tgos_gpu_bounce_reap_after_reset(); }
        return -5;
    }
    gpu.ctrl_notify();
    if gpu.wait_idle().is_err() {
        gpu.reset();
        unsafe { nas_tgos_gpu_bounce_reap_after_reset(); }
        return -5;
    }
    let owned = Box::into_raw(Box::new(gpu));
    if DEVICE.compare_exchange(ptr::null_mut(), owned,
        Ordering::AcqRel, Ordering::Acquire).is_err() {
        unsafe { drop(Box::from_raw(owned)); }
        return -16;
    }
    unsafe { *capset_id = capset.capset_id; }
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn nas_tgos_original_gpu_shutdown() -> i32 {
    let ptr = DEVICE.swap(ptr::null_mut(), Ordering::AcqRel);
    if ptr.is_null() { return -19; }
    // Drop invokes the original VirtIoGpu::reset BEFORE its DMA pages and
    // queued command buffers can be destroyed; no concurrent users exist.
    unsafe { drop(Box::from_raw(ptr)); nas_tgos_gpu_bounce_reap_after_reset(); }
    0
}
