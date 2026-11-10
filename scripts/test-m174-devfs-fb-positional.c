/* Regression for mounted /dev/fb0: a character device now owns a real VFS
 * inode AND its original seekable FileOps.  VFS inode identity must never
 * mask the framebuffer FileOps from pread/pwrite.  The actual production
 * file_io.c and fbdev.c supply data-plane behavior, not test reimplementations.
 */
#include "../kernel/drivers/display/fbdev.h"
#include "../kernel/fs/file_ops.h"
#include "../kernel/fs/io.h"
#include "../kernel/fs/lookup.h"
#include "../kernel/fd.h"
#include "../kernel/task.h"
#include "../kernel/errno.h"
#include "../kernel/arch/linux_fcntl_abi.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { M174_FD=6, M174_ALIAS=7, M174_SLOT=19, M174_CAP=8192 };
static struct nas_task task;
static struct nas_fd_table descriptors;
struct nas_task *nas_current_task=&task;
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
static uint8_t screen[M174_CAP];
static unsigned driver_reads,driver_writes;

bool nas_user_read_bytes(uint64_t address,uint8_t *dst,uint64_t count) {
    memcpy(dst,(const void *)(uintptr_t)address,(size_t)count);
    return true;
}
bool nas_user_write_bytes(uint64_t address,const uint8_t *src,uint64_t count) {
    memcpy((void *)(uintptr_t)address,src,(size_t)count);
    return true;
}
/* Should never execute: devfs has NO regular-file positional callbacks.
 * A device with a VFS inode must route through the owned FileOps backend. */
int64_t nas_vfs_read_at(const struct nas_fs_node *node,uint64_t offset,
                        uint8_t *to,uint64_t count) {
    (void)node;(void)offset;(void)to;(void)count;
    assert(!"devfs char pread incorrectly entered VFS regular read_at");
    return -EIO;
}
int64_t nas_vfs_write_at(const struct nas_fs_node *node,uint64_t offset,
                         const uint8_t *from,uint64_t count) {
    (void)node;(void)offset;(void)from;(void)count;
    assert(!"devfs char pwrite incorrectly entered VFS regular write_at");
    return -EIO;
}
bool nas_vfs_can_write(const struct nas_fs_node *node) {
    assert(node && (node->mode&0170000U)==0020000U);
    return true;
}
uint64_t nas_vfs_node_size(const struct nas_fs_node *node) {
    (void)node;
    assert(!"devfs char O_APPEND must not use synthetic inode size");
    return 0;
}
static uint64_t driver_read(unsigned slot,uint64_t offset,
                            uint8_t *data,uint64_t count) {
    assert(slot==M174_SLOT);
    driver_reads++;
    return nas_fbdev_read_at(offset,data,count);
}
static uint64_t driver_write(unsigned slot,uint64_t offset,
                             const uint8_t *data,uint64_t count) {
    assert(slot==M174_SLOT);
    driver_writes++;
    return nas_fbdev_write_at(offset,data,count);
}
static const struct nas_file_ops real_framebuffer={
    .read_at=driver_read,.write_at=driver_write
};
static const struct nas_file_ops nonseekable={0};
static const struct nas_vfs_type real_devfs_type={.name="devfs",.read_only=true};

int main(void) {
    memset(screen,0x65,sizeof(screen));
    const struct nas_fb_boot_info hw={
        .address=screen,.width=512,.height=4,.pitch=2048,.bpp=32,
        .memory_model=1,.red_size=8,.red_shift=16,
        .green_size=8,.green_shift=8,.blue_size=8,.blue_shift=0
    };
    assert(nas_fbdev_boot_init(&hw));
    task.fds=&descriptors;
    descriptors.entries[M174_FD]=(struct nas_fd_entry){
        .kind=FD_FRAMEBUFFER,.slot=M174_SLOT};
    descriptors.entries[M174_ALIAS]=descriptors.entries[M174_FD];
    struct nas_open_file *opened=&nas_open_files[M174_SLOT];
    opened->refcount=2;
    opened->status_flags=2; /* real open("/dev/fb0", O_RDWR) */
    opened->position=731;
    opened->ops=&real_framebuffer;
    opened->vnode_pinned=true;
    opened->vnode=(struct nas_fs_node){
        .filesystem=&real_devfs_type,.ino=65537,
        .rdev=(29U<<8),.mode=0020660U,.nlink=1
    };

    uint8_t payload[1300],readback[1300]={0};
    for(unsigned i=0;i<sizeof(payload);i++)payload[i]=(uint8_t)(i*17U+3U);
    assert(nas_fs_fd_pwrite(M174_FD,(uint64_t)(uintptr_t)payload,
                             sizeof(payload),137)==sizeof(payload));
    assert(!memcmp(screen+137,payload,sizeof(payload)));
    assert(screen[136]==0x65 && screen[1437]==0x65);
    assert(driver_writes>=2 && opened->position==731);
    assert(nas_fs_fd_pread(M174_ALIAS,(uint64_t)(uintptr_t)readback,
                            sizeof(readback),137)==sizeof(readback));
    assert(!memcmp(payload,readback,sizeof(payload)));
    assert(driver_reads>=2 && opened->position==731);
    assert(nas_fs_fd_pwrite(M174_ALIAS,(uint64_t)(uintptr_t)payload,
                             8,M174_CAP-3)==3);
    assert(nas_fs_fd_pread(M174_FD,(uint64_t)(uintptr_t)readback,
                            8,M174_CAP-3)==3);
    assert(!memcmp(payload,readback,3) && opened->position==731);

    opened->ops=&nonseekable;
    assert((int64_t)nas_fs_fd_pread(M174_FD,(uint64_t)(uintptr_t)readback,
                                   1,0)==-ESPIPE);
    assert((int64_t)nas_fs_fd_pwrite(M174_FD,(uint64_t)(uintptr_t)payload,
                                    1,0)==-ESPIPE);
    opened->ops=&real_framebuffer;
    opened->status_flags=NAS_LINUX_O_PATH;
    assert((int64_t)nas_fs_fd_pread(M174_FD,(uint64_t)(uintptr_t)readback,
                                   1,0)==-EBADF);
    assert((int64_t)nas_fs_fd_pwrite(M174_FD,(uint64_t)(uintptr_t)payload,
                                    1,0)==-EBADF);
    puts("[M174 DEVFS FB POSITIONAL PASS] mounted char VFS inode retains original framebuffer FileOps, pread/pwrite/EOF, dup offset and ESPIPE");
    return 0;
}
