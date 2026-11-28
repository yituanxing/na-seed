// SPDX-License-Identifier: MIT
// ORIGINAL TGOSKits virtio-gpu (pinned upstream) driven through NaSeed's
// QEMU-only transport/DMA providers. This is a REAL device-owner experiment,
// not a second protocol implementation or a render-node/DRM ioctl emulation.
use alloc::boxed::Box;
use core::{
    ffi::c_void,
    mem::{size_of, MaybeUninit},
    ptr::{self, NonNull},
    sync::atomic::{AtomicBool, AtomicPtr, AtomicU32, Ordering},
};
use virtio_drivers::{
    BufferDirection, Error as VirtIoError, Hal,
    transport::{DeviceStatus, DeviceType, InterruptStatus, Transport},
};
use zerocopy::{FromBytes, Immutable, IntoBytes};
use virtio_gpu::{VirtIoGpu, ResourceCreate3d};
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
        if version > info.max_version || info.max_size as usize > capacity {
            return Err(-22);
        }
        let bytes = gpu.get_capset(id, version, info.max_size).map_err(|_| -5)?;
        if bytes.is_empty() || bytes.len() > capacity { return Err(-5); }
        // SAFETY: caller promises capacity bytes writable in kernel memory.
        // VFS passes a private heap buffer, never an unchecked user pointer.
        unsafe {
            ptr::copy_nonoverlapping(bytes.as_ptr(), dst, bytes.len());
            *written = bytes.len();
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
