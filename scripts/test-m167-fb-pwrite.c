/* Linux fbdev positional-I/O regression through the REAL file_io.c adapter
 * and REAL firmware framebuffer provider.  No syscall is reimplemented:
 * only a tiny test Task, controlled usercopy, and unattached VFS fallbacks
 * are provided by this host process. */
#include "../kernel/drivers/display/fbdev.h"
#include "../kernel/fs/file_ops.h"
#include "../kernel/fs/io.h"
#include "../kernel/fs/packet_socket.h"
#include "../kernel/fs/vfs.h"
#include "../kernel/fd.h"
#include "../kernel/task.h"
#include "../kernel/errno.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

static struct nas_task task;
static struct nas_fd_table descriptors;
struct nas_task *nas_current_task=&task;
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
static unsigned copy_reads, copy_writes;
static unsigned fail_read_at_call, fail_write_at_call;

bool nas_user_read_bytes(uint64_t addr,uint8_t *dst,uint64_t size) {
    if(++copy_reads==fail_read_at_call)return false;
    memcpy(dst,(const void *)(uintptr_t)addr,(size_t)size);
    return true;
}
bool nas_user_write_bytes(uint64_t addr,const uint8_t *src,uint64_t size) {
    if(++copy_writes==fail_write_at_call)return false;
    memcpy((void *)(uintptr_t)addr,src,(size_t)size);
    return true;
}
/* The FileOps path below is a non-VFS framebuffer.  These link-only
 * fallbacks must NOT execute; every one aborts if positional FileOps
 * accidentally re-enters regular-file or mmap logic. */
int64_t nas_vfs_read_at(const struct nas_fs_node *node,uint64_t offset,
                        uint8_t *to,uint64_t n) {
    (void)node;(void)offset;(void)to;(void)n;
    assert(!"unexpected VFS mmap read");return -EIO;
}
int64_t nas_vfs_write_at(const struct nas_fs_node *node,uint64_t offset,
                          const uint8_t *data,uint64_t n) {
    (void)node;(void)offset;(void)data;(void)n;
    assert(!"unexpected VFS write");return -EIO;
}
bool nas_vfs_can_write(const struct nas_fs_node *node) {
    (void)node;assert(!"unexpected VFS permission check");return false;
}
/* AF_PACKET was added to the real file_io.c device mmap dispatcher after
 * this FBDEV-only host test.  Link-only stubs must never be invoked: the
 * test's open descriptions are FD_FRAMEBUFFER, not FD_SOCKET.  Do not pull
 * the real NIC/TPACKET provider into a standalone framebuffer test. */
bool nas_packet_socket_is_slot(unsigned slot) {
    (void)slot;assert(!"unexpected AF_PACKET lookup in FBDEV test");return false;
}
int nas_packet_socket_mmap_pin(unsigned slot,struct nas_vm_device **device) {
    (void)slot;(void)device;
    assert(!"unexpected AF_PACKET mmap in FBDEV test");return -EOPNOTSUPP;
}
uint64_t nas_vfs_node_size(const struct nas_fs_node *node) {
    (void)node;assert(!"unexpected VFS size");return 0;
}
/* A filesystem without shared-page support must fail before attempting to
 * allocate or pin a VMA owner.  These tripwires make that ordering explicit. */
void *nas_heap_alloc(size_t length) {
    (void)length;assert(!"unexpected VMA owner allocation");return NULL;
}
bool nas_heap_free(void *ptr) {
    (void)ptr;assert(!"unexpected VMA owner release");return false;
}
int nas_vfs_node_pin(const struct nas_fs_node *node) {
    (void)node;assert(!"unexpected VFS inode pin");return -EIO;
}

static uint64_t fb_read_positioned(unsigned slot,uint64_t offset,
                                   uint8_t *dst,uint64_t n) {
    (void)slot;return nas_fbdev_read_at(offset,dst,n);
}
static uint64_t fb_write_positioned(unsigned slot,uint64_t offset,
                                    const uint8_t *src,uint64_t n) {
    (void)slot;return nas_fbdev_write_at(offset,src,n);
}
static const struct nas_file_ops framebuffer={
    .read_at=fb_read_positioned,.write_at=fb_write_positioned
};
static const struct nas_file_ops no_positioned_io={0};
/* Model a mutable regular-file provider with working read_at but no
 * coherent shared-page callback, as in the current lwext4 adapter. */
static const struct nas_vfs_type no_shared_page_fs={
    .read_at=nas_vfs_read_at,
};

int main(void) {
    enum { FD=6, ALIAS=7, SLOT=19, CAP=8192 };
    static uint8_t pixels[CAP];
    memset(pixels,0x55,sizeof(pixels));
    struct nas_fb_boot_info hw={
        .address=pixels,.width=512,.height=4,.pitch=2048,.bpp=32,
        .memory_model=1,.red_size=8,.red_shift=16,
        .green_size=8,.green_shift=8,.blue_size=8,.blue_shift=0
    };
    assert(nas_fbdev_boot_init(&hw));
    task.fds=&descriptors;
    descriptors.entries[FD]=(struct nas_fd_entry){.kind=FD_FRAMEBUFFER,.slot=SLOT};
    descriptors.entries[ALIAS]=descriptors.entries[FD];
    nas_open_files[SLOT].refcount=2;
    nas_open_files[SLOT].status_flags=2; /* O_RDWR */
    nas_open_files[SLOT].position=731;
    nas_open_files[SLOT].ops=&framebuffer;
    /* Device mmap dispatch is a separate FD authorization path. This
     * firmware-owned framebuffer has no PMM-managed DMA backing and must
     * fail closed, even though ordinary positional pixel I/O still works. */
    struct nas_vm_device *mmap_owner=(void *)(uintptr_t)1;
    assert(nas_fs_mmap_device_pin(FD,true,&mmap_owner)==-EOPNOTSUPP);
    assert(mmap_owner==NULL);
    assert(nas_fs_mmap_device_pin(NAS_FD_LIMIT,true,&mmap_owner)==-EBADF);
    nas_open_files[SLOT].status_flags=0; /* O_RDONLY */
    assert(nas_fs_mmap_device_pin(FD,true,&mmap_owner)==-EACCES);
    nas_open_files[SLOT].status_flags=1; /* O_WRONLY */
    assert(nas_fs_mmap_device_pin(FD,false,&mmap_owner)==-EACCES);
    nas_open_files[SLOT].status_flags=2;

    /* M236 libdeflate-gzip: original upstream calls MAP_SHARED/PROT_READ
     * on an ext4 regular file.  Linux ENODEV accurately signals lack of
     * mmap support and allows the program's original read() fallback;
     * ENOSYS is wrong because the mmap syscall itself is implemented.
     * Do not accept a private copy as a fake MAP_SHARED success. */
    enum { REG_FD=8, REG_SLOT=20 };
    descriptors.entries[REG_FD]=(struct nas_fd_entry){
        .kind=FD_BOOTFS,.slot=REG_SLOT
    };
    nas_open_files[REG_SLOT].refcount=1;
    nas_open_files[REG_SLOT].status_flags=0; /* O_RDONLY */
    nas_open_files[REG_SLOT].vnode_pinned=true;
    nas_open_files[REG_SLOT].vnode=(struct nas_fs_node){
        .filesystem=&no_shared_page_fs,.mode=0100644U,
    };
    struct nas_mapped_file *mapped=(void *)(uintptr_t)1;
    assert(nas_fs_mmap_pin(REG_FD,false,&mapped)==-ENODEV);
    assert(mapped==NULL); /* no pretend coherent owner */
    assert(nas_fs_mmap_pin(REG_FD,true,&mapped)==-EACCES);
    nas_open_files[REG_SLOT].status_flags=1; /* O_WRONLY */
    assert(nas_fs_mmap_pin(REG_FD,false,&mapped)==-EACCES);
    nas_open_files[REG_SLOT].status_flags=0;
    assert(nas_fs_mmap_pin(NAS_FD_LIMIT,false,&mapped)==-EBADF);
    nas_open_files[REG_SLOT].vnode.mode=0040755U; /* directory */
    assert(nas_fs_mmap_pin(REG_FD,false,&mapped)==-ENODEV);
    nas_open_files[REG_SLOT].vnode.mode=0100644U;

    static uint8_t source[1300],target[1300];
    for(unsigned i=0;i<sizeof(source);i++)source[i]=(uint8_t)(i*17U+3U);
    assert(nas_fs_fd_pwrite(FD,(uint64_t)(uintptr_t)source,
                             sizeof(source),137)==sizeof(source));
    assert(!memcmp(pixels+137,source,sizeof(source)));
    assert(pixels[136]==0x55 && pixels[1437]==0x55);
    assert(nas_open_files[SLOT].position==731);
    assert(nas_fs_fd_pread(ALIAS,(uint64_t)(uintptr_t)target,
                            sizeof(target),137)==sizeof(target));
    assert(!memcmp(source,target,sizeof(source)));
    assert(nas_open_files[SLOT].position==731);

    /* Positioned I/O clips at the real framebuffer length; it cannot
     * leak past the last valid scanline or mutate the shared OFD cursor. */
    assert(nas_fs_fd_pwrite(ALIAS,(uint64_t)(uintptr_t)source,8,CAP-3)==3);
    assert(!memcmp(pixels+CAP-3,source,3));
    assert(nas_fs_fd_pread(FD,(uint64_t)(uintptr_t)target,8,CAP-3)==3);
    assert(!memcmp(target,source,3));
    assert(nas_fs_fd_pwrite(FD,(uint64_t)(uintptr_t)source,8,CAP)==0);
    assert(nas_open_files[SLOT].position==731);

    copy_reads=0;fail_read_at_call=1;
    uint8_t before=pixels[81];
    assert((int64_t)nas_fs_fd_pwrite(FD,(uint64_t)(uintptr_t)source,1,81)==-EFAULT);
    assert(pixels[81]==before);
    fail_read_at_call=0;
    copy_writes=0;fail_write_at_call=1;
    before=target[0];
    assert((int64_t)nas_fs_fd_pread(FD,(uint64_t)(uintptr_t)target,1,81)==-EFAULT);
    assert(target[0]==before);
    fail_write_at_call=0;

    copy_reads=0;fail_read_at_call=2;
    assert(nas_fs_fd_pwrite(FD,(uint64_t)(uintptr_t)source,1300,190)==1024);
    fail_read_at_call=0;
    assert(!memcmp(pixels+190,source,1024));
    copy_writes=0;fail_write_at_call=2;
    memset(target,0,sizeof(target));
    assert(nas_fs_fd_pread(FD,(uint64_t)(uintptr_t)target,1300,190)==1024);
    fail_write_at_call=0;

    nas_open_files[SLOT].status_flags=0; /* O_RDONLY */
    assert((int64_t)nas_fs_fd_pwrite(FD,(uint64_t)(uintptr_t)source,1,0)==-EBADF);
    nas_open_files[SLOT].status_flags=1; /* O_WRONLY */
    assert((int64_t)nas_fs_fd_pread(FD,(uint64_t)(uintptr_t)target,1,0)==-EBADF);
    nas_open_files[SLOT].status_flags=2;
    assert(nas_fs_fd_pwrite(FD,0,0,0)==0);
    assert(nas_fs_fd_pread(FD,0,0,0)==0);
    nas_open_files[SLOT].ops=&no_positioned_io;
    assert((int64_t)nas_fs_fd_pread(FD,(uint64_t)(uintptr_t)target,1,0)==-ESPIPE);
    assert((int64_t)nas_fs_fd_pwrite(FD,(uint64_t)(uintptr_t)source,1,0)==-ESPIPE);
    nas_open_files[SLOT].ops=&framebuffer;
    assert((int64_t)nas_fs_fd_pwrite(FD,(uint64_t)(uintptr_t)source,1,INT64_MAX+1ULL)==-EINVAL);
    assert((int64_t)nas_fs_fd_pread(FD,(uint64_t)(uintptr_t)target,1,INT64_MAX+1ULL)==-EINVAL);
    assert(nas_open_files[SLOT].position==731);
    puts("[M167 FBDEV PWRITE PASS] actual FileOps + firmware pixels, LP64 offsets, pread, short EOF, 1KiB chunks, usercopy partial, dup position and ESPIPE");
    return 0;
}
