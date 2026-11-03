/* M174: the real syscall/OpenFile/FD/SCM_RIGHTS code must interpret a
 * mounted character inode as a driver, not silently downgrade it to an
 * archive file.  The imported M106 fixture intentionally mocks namei; the
 * separate test-m174-devfs-vfs.c links the real VFS and devfs provider.
 * No local reimplementation of syscall, fdtable or rights transfer exists.
 */
#define main m174_existing_m106_fixture_main
#include "test-m106-usercopy-syscalls.c"
#undef main

uint64_t nas_vfs_node_size(const struct nas_fs_node *node) {
    (void)node;
    assert(!"mounted fbdev seek length must come from DisplayDevice");
    return 0;
}

static void m174_init(void) {
    assert(nas_misc_chrdev_init()==0);
    for(unsigned i=0;i<sizeof(bootstrap_test_endpoints)/
                         sizeof(bootstrap_test_endpoints[0]);i++)
        assert(nas_chrdev_node_register(&bootstrap_test_endpoints[i])==0);
    guest_task.mm=&guest_mm;
    guest_files.refs=1;
    guest_task.files=&guest_files;
    guest_task.fds=&guest_files.table;
    guest_task.fs=&guest_fs;
    nas_fd_system_init();
    reset_mapping();
    bootstrap_vfs_shadow=true;
    bootstrap_vfs_dangling_link=false;
    /* This fixture's VFS returns a controlled inode. Its attributes model
     * devfs's real mounted result, and therefore bypass the bootstrap path. */
    bootstrap_real_inode.mode=0020666U;
    bootstrap_real_inode.ino=65539;
    bootstrap_real_inode.rdev=(1U<<8)|3U;
    bootstrap_real_inode.nlink=1;
}
static void m174_transmit_file_right(unsigned original,unsigned *received) {
    const uint64_t socket_fds=TEST_BASE+64;
    assert(sys_socketpair(1,1,0,socket_fds)==0);
    unsigned sender=pair_fd(socket_fds,0),receiver=pair_fd(socket_fds,1);
    const uint64_t bytes=TEST_BASE+TEST_PAGE+64;
    const uint64_t send_iov=TEST_BASE+320,send_msg=TEST_BASE+400;
    const uint64_t send_ctl=TEST_BASE+500;
    const uint64_t recv_iov=TEST_BASE+620,recv_msg=TEST_BASE+700;
    const uint64_t recv_ctl=TEST_BASE+800,recv_bytes=TEST_BASE+TEST_PAGE+300;
    memcpy(user_ptr(bytes),"D",1);
    a008_iov(send_iov,0,bytes,1);
    uint8_t control[20]={0};
    const uint64_t length=20;
    const uint32_t level=1,type=1;
    int32_t passed=(int32_t)original;
    memcpy(control,&length,8);memcpy(control+8,&level,4);
    memcpy(control+12,&type,4);memcpy(control+16,&passed,4);
    assert(nas_user_write_bytes(send_ctl,control,sizeof(control)));
    struct nas_msghdr outgoing={.iov=send_iov,.iovlen=1,
                                  .control=send_ctl,.controllen=20};
    assert(nas_user_write_bytes(send_msg,(uint8_t *)&outgoing,sizeof(outgoing)));
    unsigned slot=guest_task.fds->entries[original].slot;
    const unsigned before=nas_open_files[slot].refcount;
    assert(sys_unix_sendmsg(sender,send_msg,0)==1);
    assert(nas_open_files[slot].refcount==before+1);
    a008_iov(recv_iov,0,recv_bytes,1);
    struct nas_msghdr incoming={.iov=recv_iov,.iovlen=1,
                                 .control=recv_ctl,.controllen=24};
    assert(nas_user_write_bytes(recv_msg,(uint8_t *)&incoming,sizeof(incoming)));
    assert(sys_unix_recvmsg(receiver,recv_msg,0)==1);
    assert(*user_ptr(recv_bytes)=='D');
    int32_t installed=-1;
    assert(nas_user_read_bytes(recv_ctl+16,(uint8_t *)&installed,4));
    assert(installed>=3 && installed<NAS_FD_LIMIT);
    assert(guest_task.fds->entries[installed].slot==slot &&
           nas_open_files[slot].refcount==before+1);
    *received=(unsigned)installed;
    nas_fs_close_fd(sender);
    nas_fs_close_fd(receiver);
}
int main(void) {
    m174_init();
    const uint64_t path=TEST_BASE+128;
    const uint64_t stat1=TEST_BASE+512,stat2=TEST_BASE+768;
    bootstrap_set_user_path(path,"/dev/null");
    const uint64_t fd=sys_open_device(path,2,0);
    assert((int64_t)fd>=0 && fd<NAS_FD_LIMIT);
    const unsigned slot=guest_task.fds->entries[fd].slot;
    assert(guest_task.fds->entries[fd].kind==FD_NULL);
    assert(nas_open_files[slot].vnode_pinned &&
           nas_open_files[slot].vnode.rdev==((1U<<8)|3U) &&
           nas_open_files[slot].chrdev_node);
    assert(!nas_open_files[slot].boot_identity.valid);
    assert(sys_fstatat_boot((uint64_t)-100,path,stat1,0)==0);
    assert(sys_fstat_boot(fd,stat2)==0);
    struct nas_linux_stat named=bootstrap_guest_stat(stat1);
    struct nas_linux_stat opened=bootstrap_guest_stat(stat2);
    assert(named.mode==0020666U && named.rdev==((1U<<8)|3U));
    assert(opened.rdev==named.rdev && opened.ino==named.ino &&
           opened.dev==named.dev && opened.mode==named.mode);

    /* dup and AF_UNIX rights all refer to the original, shared OpenFile.
     * Releasing only the sender's FD cannot revoke the live receiver. */
    struct nas_chrdev_node *lease=nas_open_files[slot].chrdev_node;
    assert(nas_chrdev_node_unregister(lease)==-EBUSY);
    const unsigned alias=NAS_FD_LIMIT-1;
    assert(!guest_task.fds->entries[alias].kind);
    guest_task.fds->entries[alias]=guest_task.fds->entries[fd];
    ++nas_open_files[slot].refcount;
    unsigned transferred=0;
    m174_transmit_file_right((unsigned)fd,&transferred);
    assert(nas_open_files[slot].refcount==3);
    nas_fs_close_fd((unsigned)fd);
    assert(nas_chrdev_node_unregister(lease)==-EBUSY);
    nas_fs_close_fd(alias);
    assert(nas_chrdev_node_unregister(lease)==-EBUSY);
    assert(sys_fstat_boot(transferred,stat2)==0);
    nas_fs_close_fd(transferred);
    assert(nas_chrdev_node_unregister(lease)==0);
    assert(nas_chrdev_node_register(lease)==0);
    assert(!live_fds() && !live_files());

    /* O_PATH gives metadata without invoking the driver; the separate real
     * VFS fixture owns O_EXCL/O_TRUNC inode semantics, while this path
     * verifies successful flag-to-FileOps classification. */
    uint64_t pathfd=sys_open_device(path,NAS_LINUX_O_PATH,0);
    assert((int64_t)pathfd>=0 &&
           nas_open_files[guest_task.fds->entries[pathfd].slot].vnode_pinned);
    nas_fs_close_fd((unsigned)pathfd);
    uint64_t truncfd=sys_open_device(path,2U|0x200U,0);
    assert((int64_t)truncfd>=0 && guest_task.fds->entries[truncfd].kind==FD_NULL);
    nas_fs_close_fd((unsigned)truncfd);
    assert(!live_fds() && !live_files());

    /* Mounted framebuffer remains a real seekable character device. Its
     * display length is owned by fbdev, not by devfs's inode.read_at hook. */
    static uint8_t framebuffer[128];
    struct nas_fb_boot_info display={
        .address=framebuffer,.width=8,.height=4,.pitch=32,.bpp=32,
        .memory_model=1,.red_size=8,.red_shift=16,
        .green_size=8,.green_shift=8,.blue_size=8,.blue_shift=0
    };
    assert(nas_fbdev_boot_init(&display));
    const unsigned fbfd=12,fbslot=21;
    assert(!guest_task.fds->entries[fbfd].kind);
    guest_task.fds->entries[fbfd]=(struct nas_fd_entry){
        .kind=FD_FRAMEBUFFER,.slot=fbslot};
    nas_open_files[fbslot]=(struct nas_open_file){
        .refcount=1,.status_flags=2,.position=17,.vnode_pinned=true,
        .vnode={.filesystem=(const struct nas_vfs_type *)(uintptr_t)1,
                .inode=&bootstrap_inode_storage,.ino=65550,
                .rdev=(29U<<8),.mode=0020660U,.nlink=1}
    };
    assert(sys_lseek_boot(fbfd,0,2)==sizeof(framebuffer));
    assert(sys_lseek_boot(fbfd,(uint64_t)-1,2)==sizeof(framebuffer)-1);
    assert(sys_lseek_boot(fbfd,0,1)==sizeof(framebuffer)-1);
    assert(sys_lseek_boot(fbfd,0,0)==0);
    nas_fs_close_fd(fbfd);
    assert(!live_fds() && !live_files());
    puts("[M174 DEVFS OPEN PASS] mounted character inode FileOps, stat/rdev, O_PATH/O_TRUNC, dup/SCM_RIGHTS shared final close, EBUSY and mounted fbdev lseek");
    return 0;
}
