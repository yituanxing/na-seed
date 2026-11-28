// SPDX-License-Identifier: MIT
// ORIGINAL TGOSKits virtio-gpu (pinned upstream) driven through NaSeed's
// QEMU-only transport/DMA providers. This is a REAL device-owner experiment,
// not a second protocol implementation or a render-node/DRM ioctl emulation.
use alloc::{boxed::Box, vec::Vec};
use core::{
    ffi::c_void,
    mem::{size_of, MaybeUninit},
    ptr::{self, NonNull},
    sync::atomic::{fence, AtomicBool, AtomicPtr, AtomicU32, Ordering},
};
use virtio_drivers::{
    BufferDirection, Error as VirtIoError, Hal,
    transport::{DeviceStatus, DeviceType, InterruptStatus, Transport},
};
use zerocopy::{FromBytes, Immutable, IntoBytes};
use virtio_gpu::{BlobMemory, GpuBox, ResourceCreate3d, Transfer3d, VirtIoGpu};
use log::{Level, LevelFilter, Log, Metadata, Record};

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
    // Existing NaSeed GEM supplies reference-counted PMM pages and VMA pins.
    // The Rust GPU owner must retire host DMA BEFORE the GEM handle is dropped.
    fn nas_drm_gem_tgos_backing_create(slot: u32, handle: u32, bytes: u32) -> i32;
    fn nas_drm_gem_tgos_backing_count(slot: u32, handle: u32, count: *mut u32) -> i32;
    fn nas_drm_gem_tgos_backing_page(slot: u32, handle: u32, page: u32, pa: *mut u64) -> i32;
    fn nas_drm_gem_destroy(slot: u32, handle: u32) -> i32;
}

// Unmodified virtio-drivers 0.13.0 requires a contiguous DMA address even for
// Rust heap requests. A direct virtual-to-physical cast is WRONG for TLSF
// (including its boot .bss arena), so every shared region uses an owned bounce
// mapping, copied back only after its used-ring completion.
// The original upstream ControlQueue logs an asynchronous device-side error
// without returning it from wait_idle()/wait_fence(). This experiment MUST
// observe those logs or it would falsely call host-rejected 3D commands PASS.
static GPU_ASYNC_ERRORS: AtomicU32 = AtomicU32::new(0);
struct GpuAuditLogger;
static GPU_AUDIT_LOGGER: GpuAuditLogger = GpuAuditLogger;
impl Log for GpuAuditLogger {
    fn enabled(&self, metadata: &Metadata<'_>) -> bool {
        metadata.level() <= Level::Warn && metadata.target().starts_with("virtio_gpu")
    }
    fn log(&self, record: &Record<'_>) {
        if self.enabled(record.metadata()) {
            GPU_ASYNC_ERRORS.fetch_add(1, Ordering::AcqRel);
        }
    }
    fn flush(&self) {}
}
fn gpu_async_ok() -> bool {
    GPU_ASYNC_ERRORS.load(Ordering::Acquire) == 0
}
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
// The real GPU runtime has one mutable queue. Serialize every user-origin
// hardware query with teardown. Busy failure is explicit, not a spin while a
// queue operation may itself need deferred servicing.
static DEVICE_BUSY: AtomicBool = AtomicBool::new(false);
fn with_device<T>(f: impl FnOnce(&mut VirtIoGpu<NaSeedHal, NaTransport>) -> Result<T, i32>)
    -> Result<T, i32> {
    if DEVICE_BUSY.compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire).is_err() {
        return Err(-16); // EBUSY
    }
    struct Release;
    impl Drop for Release {
        fn drop(&mut self) { DEVICE_BUSY.store(false, Ordering::Release); }
    }
    let _guard = Release;
    let ptr = DEVICE.load(Ordering::Acquire);
    if ptr.is_null() { return Err(-19); } // ENODEV
    // SAFETY: DEVICE is published only after initialization, removed only
    // while DEVICE_BUSY is held, and this lock ensures unique mutable access.
    f(unsafe { &mut *ptr })
}

// First genuine non-empty user-supplied SUBMIT_3D gate. File ownership lives
// in NaSeed's shared OpenFile slot; this mirrors upstream axdrm's per-DrmFile
// context but intentionally does not pretend that GEM/BO/Fence FD exist.
// The context's kernel-owned ID is disjoint from the probe's temporary ID.
const RENDER_SLOTS: usize = 128;
static RENDER_CONTEXTS: [AtomicBool; RENDER_SLOTS] =
    [const { AtomicBool::new(false) }; RENDER_SLOTS];
// This is a bounded *transitional* NaSeed File -> original GPU resource
// adapter. Original axdrm GEM/refcount/VM ownership is still required for
// arbitrary Mesa BO allocation and mapping. No copied GPU command engine.
const BO_PER_FILE: usize = 16;
static RENDER_BO_SIZE: [AtomicU32; RENDER_SLOTS * BO_PER_FILE] =
    [const { AtomicU32::new(0) }; RENDER_SLOTS * BO_PER_FILE];
// Keep shape facts for validating guest-supplied 3D transfers before
// allowing the host to DMA into a pinned user-mappable GEM allocation.
static RENDER_BO_WIDTH: [AtomicU32; RENDER_SLOTS * BO_PER_FILE] =
    [const { AtomicU32::new(0) }; RENDER_SLOTS * BO_PER_FILE];
static RENDER_BO_HEIGHT: [AtomicU32; RENDER_SLOTS * BO_PER_FILE] =
    [const { AtomicU32::new(0) }; RENDER_SLOTS * BO_PER_FILE];
#[repr(C)]
pub struct GuestResource3d {
    target: u32, format: u32, bind: u32, width: u32, height: u32,
    depth: u32, array_size: u32, last_level: u32, nr_samples: u32,
    flags: u32, bo_handle: u32, res_handle: u32, size: u32, stride: u32,
}
const _: () = assert!(size_of::<GuestResource3d>() == 56);

// Pinned upstream axdrm VirtGPU ioctl TRANSFER_*_HOST3D UAPI, 44 bytes.
#[repr(C)]
pub struct GuestTransfer3d {
    bo_handle: u32,
    box_: [u32; 6],
    level: u32,
    offset: u32,
    stride: u32,
    layer_stride: u32,
}
const _: () = assert!(size_of::<GuestTransfer3d>() == 44);


fn bo_index(file: usize, handle: u32) -> Result<usize, i32> {
    if file >= RENDER_SLOTS || handle == 0 || handle > BO_PER_FILE as u32 {
        return Err(-2); // ENOENT; never resolve other files' BOs
    }
    Ok(file * BO_PER_FILE + (handle as usize - 1))
}
fn bo_resource_id(file: usize, handle: u32) -> u32 {
    0x5000_0000 | ((file as u32) << 8) | handle
}

// Physical pages come from NaSeed's EXISTING refcounted GEM implementation.
// Collect all backing entries before ANY GPU command is enqueued. An invalid
// or disappeared backing object must never be advertised as DMA-safe.
fn backing_segments(file: u32, handle: u32) -> Result<Vec<BlobMemory>, i32> {
    let mut count = 0u32;
    let rc = unsafe {
        nas_drm_gem_tgos_backing_count(file, handle, &mut count)
    };
    if rc != 0 { return Err(rc); }
    if count == 0 || count > 8192 { return Err(-22); }
    let mut segments = Vec::new();
    segments.try_reserve_exact(count as usize).map_err(|_| -12)?;
    for page in 0..count {
        let mut pa = 0u64;
        let rc = unsafe {
            nas_drm_gem_tgos_backing_page(file, handle, page, &mut pa)
        };
        if rc != 0 || pa == 0 || pa % 4096 != 0 { return Err(-5); }
        segments.push(BlobMemory { paddr: pa, length: 4096 });
    }
    Ok(segments)
}



fn render_ctx_id(slot: usize) -> u32 {
    0x4e53_1000 + slot as u32
}

#[unsafe(no_mangle)]
pub extern "C" fn nas_tgos_original_gpu_render_open(slot: u32) -> i32 {
    let index = slot as usize;
    if index >= RENDER_SLOTS { return -22; }
    if RENDER_CONTEXTS[index].swap(true, Ordering::AcqRel) {
        return -16;
    }
    let result = with_device(|gpu| {
        if !gpu.has_virgl() { return Err(-95); }
        gpu.ctx_create(render_ctx_id(index), "naseed-alpine-render", 0)
            .map_err(|_| -5)?;
        gpu.ctrl_notify();
        gpu.wait_idle().map_err(|_| -5)?;
        if !gpu_async_ok() { return Err(-5); }
        Ok(())
    });
    if let Err(e) = result {
        // A timed-out asynchronous CTX_CREATE may still reach the host:
        // don't let a reused File slot alias the same GPU context ID.
        // The experimental driver fails closed until a device reset.
        if e != -5 {
            RENDER_CONTEXTS[index].store(false, Ordering::Release);
        }
        return e;
    }
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn nas_tgos_original_gpu_render_close(slot: u32) -> i32 {
    let index = slot as usize;
    if index >= RENDER_SLOTS || !RENDER_CONTEXTS[index].load(Ordering::Acquire) {
        return -22;
    }
    let result = with_device(|gpu| {
        // Every still-open BO belongs to this File. Destroy resources
        // BEFORE the context and only after actual QEMU used-ring proof.
        // A failure retains the slot IDs rather than risking DMA reuse.
        for handle in 1..=BO_PER_FILE as u32 {
            let idx = bo_index(index, handle)?;
            if RENDER_BO_SIZE[idx].load(Ordering::Acquire) == 0 { continue; }
            let resource = bo_resource_id(index, handle);
            gpu.ctx_detach_resource(render_ctx_id(index), resource)
                .map_err(|_| -5)?;
            gpu.resource_detach_backing(resource).map_err(|_| -5)?;
            let fence = gpu.resource_unref(resource).map_err(|_| -5)?;
            gpu.ctrl_notify();
            gpu.wait_fence(fence).map_err(|_| -5)?;
            if !gpu.fence_completed(fence) || !gpu_async_ok() {
                return Err(-5);
            }
            // Host has retired all DMA commands; only NOW can GEM drop its
            // handle pin. Live mmaps retain their own VM references.
            if unsafe { nas_drm_gem_destroy(slot, handle) } != 0 {
                return Err(-5);
            }
            RENDER_BO_SIZE[idx].store(0, Ordering::Release);
            RENDER_BO_WIDTH[idx].store(0, Ordering::Release);
            RENDER_BO_HEIGHT[idx].store(0, Ordering::Release);
        }
        gpu.ctx_destroy(render_ctx_id(index)).map_err(|_| -5)?;
        gpu.ctrl_notify();
        gpu.wait_idle().map_err(|_| -5)?;
        if !gpu_async_ok() { return Err(-5); }
        Ok(())
    });
    match result {
        Ok(()) => {
            RENDER_CONTEXTS[index].store(false, Ordering::Release);
            0
        }
        // A failed retirement must NOT release the context ID for reuse.
        Err(e) => e,
    }
}

// Linux VirtGPU RESOURCE_CREATE: kernel-validated struct, not user pointers.
// Device creation and context attachment are delegated to ORIGINAL upstream
// VirtIoGpu APIs. This limited stage has HOST 3D objects but NO guest mmap,
// transfer-to-host backing, PRIME, multi-plane or reusable dma-buf.
#[unsafe(no_mangle)]
pub extern "C" fn nas_tgos_original_gpu_render_resource_create(
    slot: u32, request: *const GuestResource3d,
    out_handle: *mut u32, out_resource: *mut u32,
) -> i32 {
    let file = slot as usize;
    if file >= RENDER_SLOTS || request.is_null() ||
       out_handle.is_null() || out_resource.is_null() ||
       !RENDER_CONTEXTS[file].load(Ordering::Acquire) {
        return -22;
    }
    let req = unsafe { &*request };
    if req.width == 0 || req.height == 0 || req.depth == 0 ||
       req.array_size == 0 || req.size == 0 || req.size > 32*1024*1024 ||
       req.size % 4 != 0 ||
       req.width > 8192 || req.height > 8192 || req.depth > 8192 {
        return -22;
    }
    match with_device(|gpu| {
        if !gpu_async_ok() { return Err(-5); }
        for handle in 1..=BO_PER_FILE as u32 {
            let idx = bo_index(file, handle)?;
            if RENDER_BO_SIZE[idx].load(Ordering::Acquire) != 0 { continue; }
            // Reserve before the first async host command. A failed host
            // operation is fail-closed: BO and ID stay pinned for teardown.
            RENDER_BO_SIZE[idx].store(req.size, Ordering::Release);
            let rc = unsafe {
                nas_drm_gem_tgos_backing_create(slot, handle, req.size)
            };
            if rc != 0 {
                RENDER_BO_SIZE[idx].store(0, Ordering::Release);
                return Err(rc);
            }
            let segments = match backing_segments(slot, handle) {
                Ok(segments) => segments,
                Err(error) => {
                    let _ = unsafe { nas_drm_gem_destroy(slot, handle) };
                    RENDER_BO_SIZE[idx].store(0, Ordering::Release);
                    return Err(error);
                }
            };
            let id = bo_resource_id(file, handle);
            gpu.resource_create_3d(ResourceCreate3d {
                ctx_id: 0, resource_id: id, target: req.target,
                format: req.format, bind: req.bind, width: req.width,
                height: req.height, depth: req.depth, array_size: req.array_size,
                last_level: req.last_level, nr_samples: req.nr_samples,
                flags: req.flags,
            }).map_err(|_| -5)?;
            // Original upstream virtio-gpu code owns the actual wire format
            // of RESOURCE_ATTACH_BACKING. Pages outlive all QEMU DMA.
            unsafe {
                gpu.resource_attach_backing_segments(id, &segments)
            }.map_err(|_| -5)?;
            gpu.ctx_attach_resource(render_ctx_id(file), id).map_err(|_| -5)?;
            gpu.ctrl_notify();
            gpu.wait_idle().map_err(|_| -5)?;
            if !gpu_async_ok() { return Err(-5); }
            // Only two-dimensional uncompressed BGRA8888 is currently
            // admitted to GPU transfer IOCTLs. Other resource formats can
            // still be created, but never DMA-transferred without layout
            // validation against the pinned upstream axdrm buffer rules.
            let transferable = req.target == 2 && req.format == 1 &&
                req.depth == 1 && req.array_size == 1 &&
                req.last_level == 0 && req.nr_samples == 0 &&
                req.width.checked_mul(req.height)
                    .and_then(|v| v.checked_mul(4)) == Some(req.size);
            if transferable {
                RENDER_BO_WIDTH[idx].store(req.width, Ordering::Release);
                RENDER_BO_HEIGHT[idx].store(req.height, Ordering::Release);
            }
            unsafe {
                *out_handle = handle;
                *out_resource = id;
            }
            return Ok(());
        }
        Err(-28) // ENOSPC
    }) {
        Ok(()) => 0,
        Err(e) => e,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn nas_tgos_original_gpu_render_resource_info(
    slot: u32, handle: u32, id: *mut u32, size: *mut u32,
) -> i32 {
    if id.is_null() || size.is_null() { return -22; }
    let file = slot as usize;
    match with_device(|_| {
        if !RENDER_CONTEXTS.get(file).is_some_and(|entry|
            entry.load(Ordering::Acquire)) { return Err(-9); }
        let idx = bo_index(file, handle)?;
        let bytes = RENDER_BO_SIZE[idx].load(Ordering::Acquire);
        if bytes == 0 { return Err(-2); }
        unsafe {
            *id = bo_resource_id(file, handle);
            *size = bytes;
        }
        Ok(())
    }) {
        Ok(()) => 0, Err(e) => e,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn nas_tgos_original_gpu_render_resource_close(
    slot: u32, handle: u32,
) -> i32 {
    let file = slot as usize;
    match with_device(|gpu| {
        if !RENDER_CONTEXTS.get(file).is_some_and(|entry|
            entry.load(Ordering::Acquire)) { return Err(-9); }
        let idx = bo_index(file, handle)?;
        if RENDER_BO_SIZE[idx].load(Ordering::Acquire) == 0 { return Err(-2); }
        let id = bo_resource_id(file, handle);
        gpu.ctx_detach_resource(render_ctx_id(file), id).map_err(|_| -5)?;
        gpu.resource_detach_backing(id).map_err(|_| -5)?;
        let fence = gpu.resource_unref(id).map_err(|_| -5)?;
        gpu.ctrl_notify();
        gpu.wait_fence(fence).map_err(|_| -5)?;
        if !gpu.fence_completed(fence) || !gpu_async_ok() {
            return Err(-5);
        }
        if unsafe { nas_drm_gem_destroy(slot, handle) } != 0 {
            return Err(-5);
        }
        RENDER_BO_SIZE[idx].store(0, Ordering::Release);
        RENDER_BO_WIDTH[idx].store(0, Ordering::Release);
        RENDER_BO_HEIGHT[idx].store(0, Ordering::Release);
        Ok(())
    }) {
        Ok(()) => 0, Err(e) => e,
    }
}

// Transfer is a REAL host-DMA operation, implemented by the pinned ORIGINAL
// upstream VirtIoGpu routines. This early gate allows a full-layer RGBA/BGRA
// upload/readback only; partial boxes, compressed formats and arbitrary Mesa
// resource layouts require the complete original axdrm adapter.
#[unsafe(no_mangle)]
pub extern "C" fn nas_tgos_original_gpu_render_transfer(
    slot: u32, request: *const GuestTransfer3d, to_host: u32,
) -> i32 {
    if request.is_null() || to_host > 1 { return -22; }
    let file = slot as usize;
    if file >= RENDER_SLOTS || !RENDER_CONTEXTS[file].load(Ordering::Acquire) {
        return -9;
    }
    let req = unsafe { &*request };
    let outcome = with_device(|gpu| {
        let idx = bo_index(file, req.bo_handle)?;
        let width = RENDER_BO_WIDTH[idx].load(Ordering::Acquire);
        let height = RENDER_BO_HEIGHT[idx].load(Ordering::Acquire);
        let size = RENDER_BO_SIZE[idx].load(Ordering::Acquire);
        if width == 0 || height == 0 || size == 0 { return Err(-95); }
        let row = width.checked_mul(4).ok_or(-22)?;
        if req.box_ != [0, 0, 0, width, height, 1] ||
            req.level != 0 || req.offset != 0 ||
            req.stride != row || req.layer_stride != size ||
            row.checked_mul(height) != Some(size) {
            return Err(-95);
        }
        if !gpu_async_ok() { return Err(-5); }
        let params = Transfer3d {
            ctx_id: render_ctx_id(file),
            resource_id: bo_resource_id(file, req.bo_handle),
            box_: GpuBox {
                x: 0, y: 0, z: 0,
                w: width, h: height, d: 1,
            },
            offset: 0,
            level: 0,
            stride: row,
            layer_stride: size,
        };
        if to_host == 1 {
            // Guest's CPU writes to mapped GEM pages must become globally
            // visible before the direct-DMA QEMU device reads them.
            fence(Ordering::SeqCst);
            gpu.transfer_to_host_3d(params).map_err(|_| -5)?;
            gpu.ctrl_notify();
            gpu.wait_idle().map_err(|_| -5)?;
        } else {
            let completion = gpu.transfer_from_host_3d(params).map_err(|_| -5)?;
            if completion == 0 { return Err(-5); }
            gpu.ctrl_notify();
            gpu.wait_fence(completion).map_err(|_| -5)?;
            if !gpu.fence_completed(completion) { return Err(-5); }
            // Host has completed the DMA write; user-mode may read now.
            fence(Ordering::SeqCst);
        }
        if !gpu_async_ok() { return Err(-5); }
        Ok(())
    });
    match outcome { Ok(()) => 0, Err(e) => e }
}

// Use the upstream TGOSKits VirtIoGpu queue implementation unchanged.
// The first admitted user command is VIRGL_CCMD_NOP: non-empty 4-byte
// VirGL protocol command, accepted by original virglrenderer decoder.
// Nothing here claims a Mesa render, GL surface or pixel output.
#[unsafe(no_mangle)]
pub extern "C" fn nas_tgos_original_gpu_render_exec_nop(
    slot: u32, command: *const u8, size: usize, bo_handle: u32,
) -> i32 {
    let index = slot as usize;
    if index >= RENDER_SLOTS ||
       !RENDER_CONTEXTS[index].load(Ordering::Acquire) { return -9; }
    if command.is_null() || size != size_of::<u32>() { return -22; }
    // SAFETY: NaSeed C usercopy provided a private, kernel-owned buffer.
    let bytes = unsafe { core::slice::from_raw_parts(command, size) };
    // VIRGL_CMD0(VIRGL_CCMD_NOP=0, VIRGL_OBJECT_NULL=0, len=0).
    // Other commands need full DRM GEM/context/fence resource tracking.
    if bytes != [0, 0, 0, 0] { return -95; }
    match with_device(|gpu| {
        if !gpu_async_ok() { return Err(-5); }
        if bo_handle != 0 {
            let idx = bo_index(index, bo_handle)?;
            if RENDER_BO_SIZE[idx].load(Ordering::Acquire) == 0 {
                return Err(-2);
            }
        }
        let fence = gpu.submit_3d(render_ctx_id(index), bytes).map_err(|_| -5)?;
        if fence == 0 { return Err(-5); }
        gpu.ctrl_notify();
        gpu.wait_fence(fence).map_err(|_| -5)?;
        if !gpu.fence_completed(fence) || !gpu_async_ok() {
            return Err(-5);
        }
        Ok(())
    }) {
        Ok(()) => 0,
        Err(e) => e,
    }
}

/// A user ioctl reads original QEMU GPU capset metadata and bytes through
/// upstream VirtIoGpu; no guest-supplied pointer is dereferenced in Rust.
/// The C VFS layer validates user addresses, bounds and the per-File owner.
#[unsafe(no_mangle)]
pub extern "C" fn nas_tgos_original_gpu_get_caps(
    id: u32, version: u32, dst: *mut u8, capacity: usize, written: *mut usize,
) -> i32 {
    if dst.is_null() || written.is_null() || capacity == 0 || capacity > 65536 {
        return -22;
    }
    let result = with_device(|gpu| {
        if !gpu.has_virgl() { return Err(-95); }
        let mut found = None;
        for index in 0..64 {
            let Ok(info) = gpu.get_capset_info(index) else { break; };
            if info.capset_id == id {
                found = Some(info);
                break;
            }
        }
        let info = found.ok_or(-22)?;
        if version > info.max_version { return Err(-22); }
        let bytes = gpu.get_capset(id, version, info.max_size).map_err(|_| -5)?;
        if bytes.is_empty() { return Err(-5); }
        // Match the pinned upstream axdrm ioctl_get_caps: copy only
        // min(request.size, contents.len()). A small user buffer is valid;
        // it must NOT require space for the host's entire max_size capset.
        let count = bytes.len().min(capacity);
        // SAFETY: caller promises capacity bytes writable in kernel memory.
        // VFS passes a private heap buffer, never an unchecked user pointer.
        unsafe {
            ptr::copy_nonoverlapping(bytes.as_ptr(), dst, count);
            *written = count;
        }
        Ok(())
    });
    match result { Ok(()) => 0, Err(e) => e }
}

/// Query only truthful, user-visible capabilities. The host has 3D support,
/// but this early render-node stage DOES NOT expose GEM/EXECBUFFER to Mesa,
/// so DRM_VIRTGPU_PARAM_3D_FEATURES must remain 0 until that ABI works.
#[unsafe(no_mangle)]
pub extern "C" fn nas_tgos_original_gpu_get_param(param: u64, value: *mut u64) -> i32 {
    if value.is_null() { return -22; }
    let result = with_device(|gpu| {
        let answer = match param {
            1 => 0,  // 3D_FEATURES: not yet usable from unmodified Mesa
            2 => 1,  // CAPSET_QUERY_FIX: GET_CAPS returns the correct byte length
            3 => 0,  // RESOURCE_BLOB: no userspace GEM backing yet
            4 => 0,  // HOST_VISIBLE: not mapped to userspace
            5 => 0,  // CROSS_DEVICE: PRIME path absent
            6 => 0,  // CONTEXT_INIT: no user ioctl yet
            7 => {
                let mut mask = 0u64;
                for index in 0..64 {
                    let Ok(info) = gpu.get_capset_info(index) else { break; };
                    if (1..64).contains(&info.capset_id) {
                        mask |= 1u64 << info.capset_id;
                    }
                }
                mask
            }
            _ => return Err(-22),
        };
        // SAFETY: caller passes an aligned kernel-owned stack out parameter.
        unsafe { *value = answer; }
        Ok(())
    });
    match result { Ok(()) => 0, Err(e) => e }
}


#[unsafe(no_mangle)]
pub extern "C" fn nas_tgos_original_gpu_probe(raw: *const c_void,
                                                capset_id: *mut u32) -> i32 {
    if raw.is_null() || capset_id.is_null() { return -22; }
    if !DEVICE.load(Ordering::Acquire).is_null() { return -16; }
    BAD_QUEUE.store(false, Ordering::Release);
    // A missing async error sink is a HARD FAILURE, not a quiet pass.
    if log::set_logger(&GPU_AUDIT_LOGGER).is_err() {
        return -16;
    }
    log::set_max_level(LevelFilter::Warn);
    GPU_ASYNC_ERRORS.store(0, Ordering::Release);
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
    // Upstream blocking GET_CAPSET validates the original QEMU response type
    // and used length. Mere feature advertisement is not enough for Mesa.
    let capset_blob = match gpu.get_capset(
        capset.capset_id, capset.max_version, capset.max_size,
    ) {
        Ok(blob) if !blob.is_empty()
            && blob.len() <= capset.max_size as usize
            && blob.iter().any(|byte| *byte != 0) => blob,
        _ => {
            gpu.reset();
            unsafe { nas_tgos_gpu_bounce_reap_after_reset(); }
            return -5;
        }
    };
    // The resource shape uses Mesa Gallium PIPE_TEXTURE_2D=2,
    // PIPE_FORMAT_B8G8R8A8_UNORM=1 and PIPE_BIND_RENDER_TARGET=(1<<1).
    // It is passed unchanged into the pinned upstream VirtIoGpu API.
    // A 0-byte SUBMIT_3D is a REAL fenced host command but draws no pixels:
    // this is not a claim of Mesa render output.
    let ctx: u32 = 0x4e534544;
    let res: u32 = 0x4e530001;
    let resource = ResourceCreate3d {
        ctx_id: 0,
        resource_id: res,
        target: 2,
        format: 1,
        bind: 1 << 1,
        width: 64,
        height: 64,
        depth: 1,
        array_size: 1,
        last_level: 0,
        nr_samples: 0,
        flags: 0,
    };
    macro_rules! fail_after_reset {
        () => {{
            gpu.reset();
            unsafe { nas_tgos_gpu_bounce_reap_after_reset(); }
            return -5;
        }};
    }
    if gpu.ctx_create(ctx, "naseed-gpu-import", 0).is_err()
        || gpu.resource_create_3d(resource).is_err()
        || gpu.ctx_attach_resource(ctx, res).is_err()
    {
        fail_after_reset!();
    }
    gpu.ctrl_notify();
    if gpu.wait_idle().is_err() || !gpu_async_ok() {
        fail_after_reset!();
    }
    let fence = match gpu.submit_3d(ctx, &[]) {
        Ok(fence) if fence != 0 => fence,
        _ => fail_after_reset!(),
    };
    gpu.ctrl_notify();
    if gpu.wait_fence(fence).is_err()
        || !gpu.fence_completed(fence)
        || !gpu_async_ok()
    {
        fail_after_reset!();
    }
    if gpu.ctx_detach_resource(ctx, res).is_err() {
        fail_after_reset!();
    }
    let release_fence = match gpu.resource_unref(res) {
        Ok(fence) if fence != 0 => fence,
        _ => fail_after_reset!(),
    };
    if gpu.wait_fence(release_fence).is_err()
        || !gpu.fence_completed(release_fence)
        || !gpu_async_ok()
        || gpu.ctx_destroy(ctx).is_err()
    {
        fail_after_reset!();
    }
    gpu.ctrl_notify();
    if gpu.wait_idle().is_err() || !gpu_async_ok() {
        fail_after_reset!();
    }
    // Keep this explicit for runtime auditing; loading real nonzero caps
    // must precede any user-mode Mesa GEM/EXECBUFFER work.
    let _validated_capset_bytes = capset_blob.len();
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
    if DEVICE_BUSY.compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire).is_err() {
        return -16;
    }
    let ptr = DEVICE.swap(ptr::null_mut(), Ordering::AcqRel);
    if ptr.is_null() {
        DEVICE_BUSY.store(false, Ordering::Release);
        return -19;
    }
    // Drop invokes the original VirtIoGpu::reset BEFORE its DMA pages and
    // queued command buffers can be destroyed. New ioctls cannot enter.
    unsafe { drop(Box::from_raw(ptr)); nas_tgos_gpu_bounce_reap_after_reset(); }
    DEVICE_BUSY.store(false, Ordering::Release);
    0
}
