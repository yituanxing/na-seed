/* M106: exercise the REAL filesystem syscall adapter and pipe ring, with a
 * simulated user MM and isolated FD reservation edge. This is a host test, not a
 * substitute for the native three-ISA QEMU acceptance.
 *
 * Include the syscall TU so the actual static pipe2/socketpair and pipe
 * provider callbacks are exercised by the PUBLIC read/write FD adapter.
 * Unrelated syscall entrypoints are removed by --gc-sections at link time.
 */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/fs/syscall.c"
#include "../kernel/fs/metadata_syscall.c"
#include "../kernel/fs/socket_syscall.c"
#include "../kernel/mm/fault.h"
#include "../kernel/mm/frame.h"
#include "../kernel/arch/user_pt.h"

enum { TEST_PAGE = 4096, TEST_PAGES = 4, TEST_OFD_START = 16 };
#define TEST_BASE UINT64_C(0x210000)
static uint8_t pages[TEST_PAGES][TEST_PAGE];
static bool present[TEST_PAGES], can_write[TEST_PAGES], cow[TEST_PAGES];
static bool lazy[TEST_PAGES];
static unsigned missing_faults, protection_faults, copied_pages;
static struct nas_mm guest_mm = { .users=1, .root_phys=0x9000 };
static struct nas_task guest_task;
static struct nas_files guest_files;
static struct nas_fs_context guest_fs;
struct nas_task *nas_current_task = &guest_task;
/* This FD/usercopy fixture has no mounted VFS file; A005 exercises real
 * VFS shared-mapping permissions and mtime in separate integration tests. */
bool nas_vfs_can_write(const struct nas_fs_node *node) {
    (void)node;
    assert(!"unexpected mapped VFS write in M106");
    return false;
}
int nas_vfs_touch_shared(const struct nas_fs_node *node) {
    (void)node;
    assert(!"unexpected mapped VFS dirty in M106");
    return -EIO;
}
static unsigned pending_accept_slot=NAS_OPEN_FILE_LIMIT, accepted_count;

/* The real FileOps importer owns its iovec snapshot until completion. The
 * host fixture substitutes malloc/free for the kernel's frame-backed heap,
 * while testing the exact production import/release and provider paths. */
void *nas_heap_alloc(size_t length){return malloc(length);}
bool nas_heap_free(void *ptr){free(ptr);return true;}
/* Only synthetic /dev bootstrap may project a Path to a diagnostic string.
 * A missing root or stale/oversized anchor must never cause a blind lexical
 * path resolver to invent an ordinary filesystem endpoint. */
static const char *bootstrap_test_parent="/";
static bool bootstrap_vfs_shadow,bootstrap_vfs_dangling_link;
static bool bootstrap_pty_present,bootstrap_tty_present;
static bool bootstrap_pty_controlling;
static unsigned bootstrap_vfs_open_calls,bootstrap_vfs_lookup_calls;
static uint8_t bootstrap_inode_storage;
static struct nas_fs_node bootstrap_real_inode={
    .inode=&bootstrap_inode_storage,.ino=9001,.mode=0100644U,.nlink=1,
    .size=123,.filesystem=(const struct nas_vfs_type *)(uintptr_t)1,
    .mount=(const struct nas_vfs_mount *)(uintptr_t)1,.mount_id=1
};
static struct nas_fs_node bootstrap_real_symlink={
    .inode=&bootstrap_inode_storage,.ino=9002,.mode=0120777U,.nlink=1,
    .size=12,.filesystem=(const struct nas_vfs_type *)(uintptr_t)1,
    .mount=(const struct nas_vfs_mount *)(uintptr_t)1,.mount_id=1
};
int nas_vfs_path_string(const struct nas_vfs_path *path,char output[256]) {
    if(!path || !path->dentry)return -ESTALE;
    size_t length=strlen(bootstrap_test_parent);
    if(length>=256)return -ENAMETOOLONG;
    memcpy(output,bootstrap_test_parent,length+1);
    return 0;
}
int nas_pty_path_number(const char *path){
    return path && !strcmp(path,"/dev/pts/3") ? 3 : -1;
}
bool nas_pty_present(unsigned number){return number==3 && bootstrap_pty_present;}
bool nas_pty_controlling(void){return bootstrap_pty_controlling;}
bool nas_tty_controlling(void){return bootstrap_tty_present;}
unsigned nas_pty_number(unsigned slot){(void)slot;return 3;}
int nas_pty_new(unsigned slot){(void)slot;return 0;}
int nas_pty_attach(unsigned number,unsigned slot){
    (void)slot;return number==3 && bootstrap_pty_present ? 0:-ENOENT;
}
int nas_pty_attach_controlling(unsigned slot){(void)slot;return 0;}
static const struct limine_file bootstrap_module={
    .address=NULL,.size=1234,.path="/boot/direct-elf"
};
const struct limine_file *nas_boot_limine_module_path(const char *path) {
    return path && (!strcmp(path,"/bin/direct-elf") ||
                    !strcmp(path,"/lib/direct-elf-alias")) ?
           &bootstrap_module : NULL;
}
/* Rootless-bootstrap integration oracle: Path-first lookup is authoritative
 * even at the exact spelling of a synthetic /dev endpoint. The real VFS
 * fixture below deliberately shadows /dev/null with a regular inode. */
int nas_fs_user_path_begin(uint64_t dirfd,uint64_t user_pointer,
                           struct nas_fs_path_request *request) {
    (void)dirfd;
    *request=(struct nas_fs_path_request){0};
    int rc=nas_user_read_cstr(user_pointer,request->inline_path,
                              sizeof(request->inline_path));
    if(rc)return rc;
    request->pathname=request->inline_path;
    return 0;
}
void nas_fs_path_end(struct nas_fs_path_request *request){(void)request;}
int nas_fs_lookup_path_at(const struct nas_vfs_path *start,
                          const struct nas_vfs_path *root_clamp,
                          const char *path,bool follow_final,
                          struct nas_vfs_path *out) {
    (void)start;(void)root_clamp;
    ++bootstrap_vfs_lookup_calls;
    *out=(struct nas_vfs_path){0};
    if(strcmp(path,"/dev/null"))return -ENOENT;
    if(bootstrap_vfs_dangling_link && follow_final)return -ENOENT;
    if(!bootstrap_vfs_dangling_link && !bootstrap_vfs_shadow)return -ENOENT;
    out->dentry=(struct nas_vfs_dentry *)(uintptr_t)1;
    return 0;
}
int nas_fs_lookup_node(const char *path,bool follow_final,
                       struct nas_fs_node *out,char resolved[256]) {
    (void)path;(void)follow_final;(void)out;(void)resolved;
    return -ENOENT;
}
int nas_vfs_path_node(const struct nas_vfs_path *path,
                      struct nas_fs_node *out) {
    if(!path || !path->dentry)return -ESTALE;
    *out=bootstrap_vfs_dangling_link ? bootstrap_real_symlink :
                                      bootstrap_real_inode;
    return 0;
}
int nas_vfs_getattr(const struct nas_fs_node *inode,
                    struct nas_fs_node *out) {
    *out=*inode;return 0;
}
uint64_t nas_vfs_node_device(const struct nas_fs_node *node) {
    (void)node;return 777;
}
int nas_vfs_open_path_at(const struct nas_vfs_path *start,
                         const struct nas_vfs_path *root_clamp,
                         const char *path,
                         const struct nas_vfs_open_options *options,
                         struct nas_fs_node *owned,
                         struct nas_vfs_path *owned_path) {
    (void)start;(void)root_clamp;(void)options;
    ++bootstrap_vfs_open_calls;
    if(bootstrap_vfs_dangling_link && !strcmp(path,"/dev/null"))return -ENOENT;
    if(!bootstrap_vfs_shadow || strcmp(path,"/dev/null"))return -ENOENT;
    *owned=bootstrap_real_inode;
    if(owned_path)owned_path->dentry=(struct nas_vfs_dentry *)(uintptr_t)1;
    return 0;
}
static void test_bootstrap_owned_candidate(void) {
    struct nas_fs_path_request request={0};
    char candidate[256];
    request.pathname="/dev/null";
    assert(nas_fs_bootstrap_candidate(&request,candidate));
    assert(!strcmp(candidate,"/dev/null"));
    request.pathname="dev/null";
    assert(!nas_fs_bootstrap_candidate(&request,candidate));
    request.start.dentry=(struct nas_vfs_dentry *)(uintptr_t)1;
    assert(nas_fs_bootstrap_candidate(&request,candidate));
    assert(!strcmp(candidate,"/dev/null"));
    request.pathname="./dev/null";
    assert(!nas_fs_bootstrap_candidate(&request,candidate));
    request.pathname="dev/../dev/null";
    assert(!nas_fs_bootstrap_candidate(&request,candidate));
    request.pathname="/dev//null";
    assert(!nas_fs_bootstrap_candidate(&request,candidate));
    request.pathname="/dev/../dev/null";
    assert(!nas_fs_bootstrap_candidate(&request,candidate));
    bootstrap_test_parent="/tmp";
    request.pathname="../dev/null";
    assert(!nas_fs_bootstrap_candidate(&request,candidate));
    request.pathname="/dev/null";
    assert(nas_fs_bootstrap_candidate(&request,candidate));
    char long_path[4096];
    memset(long_path,'x',sizeof(long_path));
    long_path[0]='/';long_path[sizeof(long_path)-1]=0;
    request.pathname=long_path;
    assert(!nas_fs_bootstrap_candidate(&request,candidate));
    request.pathname="dev/null";
    assert(!nas_fs_bootstrap_candidate(&request,candidate));
    bootstrap_test_parent="/";
    puts("[BOOTSTRAP PATH PASS] owned root/dirfd, literal whitelist, no second namei");
}

static int page_index(uint64_t va) {
    if(va<TEST_BASE || va-TEST_BASE>=TEST_PAGE*TEST_PAGES)return -1;
    return (int)((va-TEST_BASE)/TEST_PAGE);
}
static uint8_t *user_ptr(uint64_t va) {
    int p=page_index(va);
    return p<0 ? NULL : pages[p]+(va-TEST_BASE)%TEST_PAGE;
}
uint8_t *nas_mmu_user_byte(uint64_t va) {
    int p=page_index(va);
    return p<0 || !present[p] ? NULL : user_ptr(va);
}
bool nas_mmu_user_writable(uint64_t va) {
    int p=page_index(va);
    return p>=0 && present[p] && can_write[p] && !cow[p];
}
const struct nas_anon_region *nas_vma_find(const struct nas_anon_regions *r,
                                            uint64_t va) {
    (void)r;(void)va;
    /* The mock hardware rights encode the legacy resident-mapping policy.
     * True per-VMA lazy/COW contract is additionally covered by the
     * production MM regression test-m105-elf-cow.c. */
    return NULL;
}
bool nas_mm_resolve_user(const struct nas_mm *mm,uint64_t va,
                         uint64_t *pa,unsigned *access) {
    assert(mm==&guest_mm);
    int p=page_index(va);
    if(p<0 || !present[p])return false;
    if(pa)*pa=(uint64_t)(p+1)*TEST_PAGE+(va&(TEST_PAGE-1));
    if(access)*access=NAS_UP_READ|(can_write[p]&&!cow[p]?NAS_UP_WRITE:0);
    return true;
}
enum nas_mm_fault_result nas_mm_handle_fault(struct nas_mm *mm,uint64_t va,
                                             unsigned access,bool protection) {
    assert(mm==&guest_mm);
    int p=page_index(va);
    if(p<0)return NAS_MM_FAULT_MAPERR;
    if(!present[p] && lazy[p]) {
        ++missing_faults;
        present[p]=true;
        return NAS_MM_FAULT_RESOLVED;
    }
    if(protection && cow[p] && (access&NAS_UP_WRITE)) {
        ++protection_faults;
        cow[p]=false;
        return NAS_MM_FAULT_RESOLVED;
    }
    return NAS_MM_FAULT_MAPERR;
}
uint32_t nas_frame_refs(uint64_t pa){(void)pa;return 1;}
bool nas_mm_break_cow(struct nas_mm *mm,uint64_t va,unsigned prot,
                       bool preserve_perm) {
    (void)prot;(void)preserve_perm;
    assert(mm==&guest_mm);
    int p=page_index(va);
    if(p<0 || !cow[p])return false;
    cow[p]=false;
    ++copied_pages;
    return true;
}

/* Only the real tested paths survive section GC. Keep output quiet but
 * immediately fail the host fixture if any user-memory error hits a panic. */
void nas_print(const char *msg){(void)msg;}
void nas_dec(uint64_t n){(void)n;}
void nas_putchar(char ch){(void)ch;}
void nas_arch_halt(void){assert(!"user syscall triggered kernel panic");abort();}
void nas_unix_socket_pair_ready(unsigned a,unsigned b){(void)a;(void)b;}
int nas_unix_socket_accept(unsigned listener,unsigned *slot) {
    assert(listener==20);
    if(pending_accept_slot==NAS_OPEN_FILE_LIMIT)return -EAGAIN;
    *slot=pending_accept_slot;
    pending_accept_slot=NAS_OPEN_FILE_LIMIT;
    ++accepted_count;
    return 0;
}
/* A few unrelated public cleanup functions are reachable through the
 * descriptor adapter even when no cleanup is invoked in this fixture. */
void nas_epoll_target_final_close(unsigned slot){(void)slot;}
void nas_vfs_release_flock(const void *owner){(void)owner;}
int nas_vfs_path_put(struct nas_vfs_path *p){(void)p;return 0;}
static unsigned anonymous_factory_calls,anonymous_unpins;
static int anonymous_factory_error;
static uint8_t anonymous_backend_inode;
int nas_vfs_node_unpin(const struct nas_fs_node *node){
    if(node && node->inode==&anonymous_backend_inode)++anonymous_unpins;
    return 0;
}
int nas_vfs_create_anonymous_tmpfs(uint64_t mode,struct nas_fs_node *out) {
    assert(mode==0777U && out);
    ++anonymous_factory_calls;
    if(anonymous_factory_error)return anonymous_factory_error;
    *out=(struct nas_fs_node){
        .inode=&anonymous_backend_inode,.ino=5001,.mode=0100777U
    };
    return 0;
}
void nas_vfs_release_posix_locks(const struct nas_fs_node *node,uint64_t pid) {
    (void)node;(void)pid;
}
/* The production FD table/refcount contract is covered independently by
 * test-fd-entry-model.c. Mock only the thin reservation/publication edge
 * here, while preserving the real pipe ring and syscall copy transaction. */
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
/* Production callbacks, not duplicate host pipe algorithms. The isolated
 * FD-install fixture binds only the exercised kinds so referencing the
 * complete production ops_by_kind[] does not pull unrelated providers into
 * this focused host executable. */
static const struct nas_file_ops m106_pipe_reader={
    .read_plan=pipe_read_plan,.read=pipe_fd_read,.aggregate_readv=true
};
static const struct nas_file_ops m106_pipe_writer={
    .write_plan=pipe_write_plan,.write=pipe_fd_write,
    .atomic_write_limit=4096,.aggregate_writev=true
};
static const struct nas_file_ops m106_stream_socket={
    .read_plan=pipe_read_plan,.write_plan=socket_write_plan,
    .read=pipe_fd_read,.write=pipe_fd_write,
    .aggregate_readv=true,.aggregate_writev=true
};
static const struct nas_file_ops m106_eventfd={
    .read_plan=nas_eventfd_read_plan,.write_plan=nas_eventfd_write_plan,
    .read=nas_eventfd_read,.write=nas_eventfd_write,
    .read_commit_before_copyout=true,.aggregate_readv=true
};
void nas_fd_system_init(void) { memset(nas_open_files,0,sizeof(nas_open_files)); }
unsigned nas_fd_first_unused(const struct nas_task *t,unsigned minimum) {
    for(unsigned i=minimum;i<NAS_FD_LIMIT;i++)
        if(!t->fds->entries[i].kind)return i;
    return NAS_FD_LIMIT;
}
bool nas_fd_attach_owned(struct nas_task *t,unsigned fd,unsigned slot,
                         uint8_t kind,bool cloexec) {
    if(fd>=NAS_FD_LIMIT||slot>=NAS_OPEN_FILE_LIMIT||
       t->fds->entries[fd].kind||!nas_open_files[slot].refcount)return false;
    t->fds->entries[fd]=(struct nas_fd_entry){
        .kind=kind,.slot=(uint8_t)slot,.cloexec=cloexec?1U:0U
    };
    return true;
}
unsigned nas_fd_close(struct nas_task *t,unsigned fd) {
    if(fd>=NAS_FD_LIMIT||!t->fds->entries[fd].kind)return NAS_OPEN_FILE_LIMIT;
    unsigned slot=t->fds->entries[fd].slot;
    t->fds->entries[fd]=(struct nas_fd_entry){0};
    if(!nas_open_files[slot].refcount)return NAS_OPEN_FILE_LIMIT;
    return --nas_open_files[slot].refcount?NAS_OPEN_FILE_LIMIT:slot;
}
int nas_fd_reserve_filtered(const struct nas_task *t,unsigned minimum,
                            unsigned count,unsigned fds[2],unsigned slots[2],
                            const char *operation,
                            bool (*slot_reusable)(unsigned)) {
    (void)operation;
    unsigned fd_count=0,slot_count=0;
    for(unsigned i=minimum;i<NAS_FD_LIMIT&&fd_count<count;i++)
        if(!t->fds->entries[i].kind)fds[fd_count++]=i;
    if(fd_count<count)return -EMFILE;
    /* Exercise user FD != shared OpenFile slot; this fixture validates
     * publication, not the allocator's lowest-available-slot policy. */
    for(unsigned i=TEST_OFD_START;i<NAS_OPEN_FILE_LIMIT&&slot_count<count;i++)
        if(!nas_open_files[i].refcount &&
           (!slot_reusable || slot_reusable(i)))slots[slot_count++]=i;
    return slot_count<count?-ENFILE:0;
}
int nas_fd_reserve(const struct nas_task *t,unsigned minimum,unsigned count,
                   unsigned fds[2],unsigned slots[2],const char *operation) {
    return nas_fd_reserve_filtered(t,minimum,count,fds,slots,operation,NULL);
}
bool nas_fd_hold_description(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount ||
       nas_open_files[slot].refcount==UINT16_MAX)return false;
    ++nas_open_files[slot].refcount;
    return true;
}
/* No live TTY exists in the M106 fake-userspace fixture; the provider wait
 * gate is exercised by separate TTY and real native SocketPort tests. */
bool nas_tty_read_waitable(void){return false;}
bool nas_fd_install_new(struct nas_task *t,unsigned fd,unsigned slot,
                        uint8_t kind,uint64_t status,bool cloexec) {
    if(fd>=NAS_FD_LIMIT||slot>=NAS_OPEN_FILE_LIMIT||
       t->fds->entries[fd].kind||nas_open_files[slot].refcount)return false;
    t->fds->entries[fd]=(struct nas_fd_entry){
        .kind=kind,.slot=(uint8_t)slot,.cloexec=cloexec?1U:0U
    };
    nas_open_files[slot].refcount=1;
    nas_open_files[slot].status_flags=status;
    nas_open_files[slot].ops=kind==FD_PIPE_R ? &m106_pipe_reader :
                             kind==FD_PIPE_W ? &m106_pipe_writer :
                             kind==FD_SOCKET ? &m106_stream_socket :
                             kind==FD_EVENTFD ? &m106_eventfd : NULL;
    return true;
}

static unsigned live_fds(void) {
    unsigned n=0;
    for(unsigned i=0;i<NAS_FD_LIMIT;i++)
        n+=(guest_task.fds->entries[i].kind!=FD_CLOSED);
    return n;
}
static unsigned live_files(void) {
    unsigned n=0;
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
        n+=(nas_open_files[i].refcount!=0);
    return n;
}
static void reset_mapping(void) {
    for(unsigned i=0;i<TEST_PAGES;i++) {
        memset(pages[i],0xa6,TEST_PAGE);
        present[i]=can_write[i]=true;
        cow[i]=lazy[i]=false;
    }
    missing_faults=protection_faults=copied_pages=0;
}
static unsigned pair_fd(uint64_t addr,unsigned index) {
    uint8_t bytes[4];
    assert(nas_user_read_bytes(addr+index*4,bytes,4));
    return (unsigned)bytes[0]|((unsigned)bytes[1]<<8)|
           ((unsigned)bytes[2]<<16)|((unsigned)bytes[3]<<24);
}
static struct nas_linux_stat bootstrap_guest_stat(uint64_t address) {
    struct nas_linux_stat stat;
    assert(nas_user_read_bytes(address,(uint8_t *)&stat,sizeof(stat)));
    return stat;
}
static void bootstrap_set_user_path(uint64_t address,const char *path) {
    size_t length=strlen(path)+1;
    assert(nas_user_write_bytes(address,(const uint8_t *)path,length));
}
/* Keep the real production open/newfstatat/fstat code in this oracle.
 * The only substitutions are the host guest-memory mapping and a VFS
 * provider whose real /dev/null inode can deliberately shadow bootstrap. */
static void test_bootstrap_metadata_and_vfs_priority(void) {
    const uint64_t path=TEST_BASE+64,stat_address=TEST_BASE+512;
    const uint64_t stat_again=TEST_BASE+768;
    reset_mapping();
    nas_fd_system_init();
    memset(guest_task.fds->entries,0,sizeof(guest_task.fds->entries));
    bootstrap_vfs_shadow=false;
    bootstrap_vfs_dangling_link=false;
    bootstrap_vfs_lookup_calls=bootstrap_vfs_open_calls=0;

    struct nas_fs_bootstrap_info info,alias;
    assert(!nas_fs_bootstrap_describe("/dev/null",&info));
    assert(info.fd_kind==FD_NULL && info.ino==3 && info.rdev==((1U<<8)|3U));
    assert(nas_fs_bootstrap_describe("/dev//null",&info)==-ENOENT);
    assert(!nas_fs_bootstrap_describe("/bin/direct-elf",&info));
    assert(!nas_fs_bootstrap_describe("/lib/direct-elf-alias",&alias));
    assert(info.fd_kind==FD_BOOTFS && info.boot_file==&bootstrap_module &&
           info.ino==alias.ino && info.ino && info.size==1234);

    /* Mounted files are authoritative even if their name is also a
     * synthetic bootstrap device. Both open and stat must see the inode. */
    bootstrap_vfs_shadow=true;
    bootstrap_set_user_path(path,"/dev/null");
    uint64_t fd=sys_open_device(path,0,0);
    assert((int64_t)fd>=0 && fd<NAS_FD_LIMIT);
    struct nas_fd_entry entry=guest_task.fds->entries[fd];
    assert(entry.kind==FD_ARCHIVE &&
           nas_open_files[entry.slot].vnode_pinned &&
           nas_open_files[entry.slot].vnode.ino==9001);
    assert(sys_fstatat_boot((uint64_t)-100,path,stat_address,0)==0);
    struct nas_linux_stat st=bootstrap_guest_stat(stat_address);
    assert(st.ino==9001 && st.dev==777 && st.mode==0100644U);
    assert(bootstrap_vfs_open_calls && bootstrap_vfs_lookup_calls);
    nas_fs_close_fd(fd);
    /* O_CREAT on an existing /dev endpoint must still select its real
     * VFS inode, including with the conflicting bootstrap whitelist. */
    fd=sys_open_device(path,0x40U,0644);
    assert((int64_t)fd>=0 &&
           nas_open_files[guest_task.fds->entries[fd].slot].vnode.ino==9001);
    nas_fs_close_fd(fd);
    bootstrap_vfs_shadow=false;

    /* A dangling real symlink wins over an identically named synthetic
     * endpoint. Follow-final ENOENT must not authorize bootstrap fallback;
     * lstat still sees the mounted symlink itself. */
    bootstrap_vfs_dangling_link=true;
    unsigned link_vfs_opens=bootstrap_vfs_open_calls;
    assert(sys_open_device(path,0,0)==(uint64_t)-ENOENT);
    assert(bootstrap_vfs_open_calls>link_vfs_opens);
    assert(sys_access_path((uint64_t)-100,path,4U,0)==(uint64_t)-ENOENT);
    assert(sys_fstatat_boot((uint64_t)-100,path,stat_address,0)==
           (uint64_t)-ENOENT);
    assert(sys_fstatat_boot((uint64_t)-100,path,stat_address,0x100U)==0);
    st=bootstrap_guest_stat(stat_address);
    assert(st.ino==9002 && st.mode==0120777U);
    link_vfs_opens=bootstrap_vfs_open_calls;
    assert(sys_open_device(path,0x40U,0666)==(uint64_t)-ENOENT);
    assert(bootstrap_vfs_open_calls>link_vfs_opens);
    bootstrap_vfs_dangling_link=false;

    /* If /dev/null is genuinely absent from the mounted namespace,
     * O_CREAT and O_TRUNC may still open the historical synthetic device.
     * No ordinary VFS create transaction is permitted after ENOENT. */
    unsigned vfs_opens=bootstrap_vfs_open_calls;
    fd=sys_open_device(path,0x40U|0x200U,0666);
    assert((int64_t)fd>=0 && guest_task.fds->entries[fd].kind==FD_NULL);
    assert(bootstrap_vfs_open_calls==vfs_opens);
    nas_fs_close_fd(fd);
    assert(sys_open_device(path,0x40U|0x80U,0666)==(uint64_t)-EEXIST);
    assert(bootstrap_vfs_open_calls==vfs_opens);

    /* A read-only stat of a genuinely synthetic device is NOT open(2).
     * Exhausting all 64 process FDs must not make stat fail EMFILE. */
    for(unsigned i=0;i<NAS_FD_LIMIT;i++)
        guest_task.fds->entries[i]=(struct nas_fd_entry){FD_NULL,0,0};
    assert(sys_fstatat_boot((uint64_t)-100,path,stat_address,0)==0);
    st=bootstrap_guest_stat(stat_address);
    assert(st.ino==3 && st.dev==2 && st.mode==0020666U);
    assert(live_fds()==NAS_FD_LIMIT);
    memset(guest_task.fds->entries,0,sizeof(guest_task.fds->entries));

    fd=sys_open_device(path,0,0);
    uint64_t second=sys_open_device(path,0,0);
    assert((int64_t)fd>=0 && (int64_t)second>=0 && fd!=second);
    assert(sys_fstat_boot(fd,stat_address)==0);
    assert(sys_fstat_boot(second,stat_again)==0);
    st=bootstrap_guest_stat(stat_address);
    struct nas_linux_stat st2=bootstrap_guest_stat(stat_again);
    assert(st.dev==st2.dev && st.ino==st2.ino && st.ino==3);
    unsigned duplicate=NAS_FD_LIMIT-1;
    assert(!guest_task.fds->entries[duplicate].kind);
    guest_task.fds->entries[duplicate]=guest_task.fds->entries[fd];
    ++nas_open_files[guest_task.fds->entries[fd].slot].refcount;
    assert(sys_fstat_boot(duplicate,stat_again)==0);
    st2=bootstrap_guest_stat(stat_again);
    assert(st.dev==st2.dev && st.ino==st2.ino);
    nas_fs_close_fd(duplicate);
    nas_fs_close_fd(fd);
    nas_fs_close_fd(second);

    /* PTY names exist only while their live number is registered. The
     * slave inode is number-specific, while Linux ptmx has fixed path
     * identity across separate master open descriptions. */
    bootstrap_set_user_path(path,"/dev/pts/3");
    assert(sys_fstatat_boot((uint64_t)-100,path,stat_address,0)==
           (uint64_t)-ENOENT);
    bootstrap_pty_present=true;
    assert(sys_fstatat_boot((uint64_t)-100,path,stat_address,0)==0);
    st=bootstrap_guest_stat(stat_address);
    assert(st.ino==2003 && st.dev==6);
    fd=sys_open_device(path,0,0);
    assert((int64_t)fd>=0 && guest_task.fds->entries[fd].kind==FD_PTY_SLAVE);
    assert(sys_fstat_boot(fd,stat_again)==0);
    st2=bootstrap_guest_stat(stat_again);
    assert(st.ino==st2.ino && st.dev==st2.dev);
    nas_fs_close_fd(fd);
    bootstrap_pty_present=false;

    bootstrap_set_user_path(path,"/dev/ptmx");
    assert(sys_fstatat_boot((uint64_t)-100,path,stat_address,0)==0);
    st=bootstrap_guest_stat(stat_address);
    fd=sys_open_device(path,0,0);
    assert((int64_t)fd>=0);
    assert(sys_fstat_boot(fd,stat_again)==0);
    st2=bootstrap_guest_stat(stat_again);
    assert(st.ino==2000 && st.ino==st2.ino);
    nas_fs_close_fd(fd);

    bootstrap_set_user_path(path,"/dev/tty");
    assert(sys_fstatat_boot((uint64_t)-100,path,stat_address,0)==0);
    /* Linux host oracle: access and stat work without a controlling TTY;
     * only opening /dev/tty reports ENXIO in that state. */
    assert(sys_access_path((uint64_t)-100,path,4U,0)==0);
    assert(sys_open_device(path,0,0)==(uint64_t)-ENXIO);
    bootstrap_tty_present=true;
    fd=sys_open_device(path,0,0);
    assert((int64_t)fd>=0 && guest_task.fds->entries[fd].kind==FD_TTY);
    nas_fs_close_fd(fd);
    bootstrap_tty_present=false;

    bootstrap_set_user_path(path,"/bin/direct-elf");
    assert(sys_fstatat_boot((uint64_t)-100,path,stat_address,0)==0);
    st=bootstrap_guest_stat(stat_address);
    fd=sys_open_device(path,0,0);
    assert((int64_t)fd>=0 && guest_task.fds->entries[fd].kind==FD_BOOTFS);
    assert(sys_fstat_boot(fd,stat_again)==0);
    st2=bootstrap_guest_stat(stat_again);
    assert(st.dev==st2.dev && st.ino==st2.ino && st.size==1234);
    nas_fs_close_fd(fd);
    bootstrap_set_user_path(path,"/lib/direct-elf-alias");
    assert(sys_fstatat_boot((uint64_t)-100,path,stat_again,0)==0);
    st2=bootstrap_guest_stat(stat_again);
    assert(st.ino==st2.ino);

    assert(!live_fds() && !live_files());
    puts("[BOOTSTRAP VFS/STAT PASS] real inode priority, FD-full stat, OFD identity, PTY/TTY, Limine aliases");
}

static void a008_iov(uint64_t iov,unsigned index,uint64_t source,uint64_t size) {
    /* Each descriptor is 16-byte aligned and fits inside one mock page. */
    uint64_t pos=iov+index*16ULL;
    assert((pos&4095ULL)<=4096-16);
    uint8_t *entry=user_ptr(pos);
    assert(entry);
    memcpy(entry,&source,8);
    memcpy(entry+8,&size,8);
}

static void a008_vectors(unsigned rd,unsigned wr) {
    const uint64_t iov=TEST_BASE;
    uint64_t src=TEST_BASE+TEST_PAGE+40;
    uint64_t dst=TEST_BASE+2*TEST_PAGE+128;
    unsigned pending=0;

    /* Linux validates the shared File and O_ACCMODE even for iovcnt=0. */
    reset_mapping();
    assert((int64_t)nas_fs_fd_readv(NAS_FD_LIMIT,iov,0)==-EBADF);
    assert((int64_t)nas_fs_fd_writev(NAS_FD_LIMIT,iov,0)==-EBADF);
    assert((int64_t)nas_fs_fd_readv(wr,iov,0)==-EBADF);
    assert((int64_t)nas_fs_fd_writev(rd,iov,0)==-EBADF);
    assert(nas_fs_fd_readv(rd,iov,0)==0);
    assert(nas_fs_fd_writev(wr,iov,0)==0);

    /* Linux IOV_MAX is 1024, not a userspace-visible 64-element ceiling.
     * Exactly 1024 16-byte descriptors fit our four simulated user pages. */
    memset(pages,0,sizeof(pages));
    assert(nas_fs_fd_readv(rd,iov,1024)==0);
    assert(nas_fs_fd_writev(wr,iov,1024)==0);
    assert((int64_t)nas_fs_fd_readv(rd,iov,1025)==-EINVAL);
    assert((int64_t)nas_fs_fd_writev(wr,iov,1025)==-EINVAL);
    assert((int64_t)nas_fs_fd_readv(NAS_FD_LIMIT,iov,1025)==-EBADF);
    assert((int64_t)nas_fs_fd_writev(NAS_FD_LIMIT,iov,1025)==-EBADF);
    assert((int64_t)nas_fs_fd_readv(wr,iov,1025)==-EBADF);
    assert((int64_t)nas_fs_fd_writev(rd,iov,1025)==-EBADF);
    assert((int64_t)nas_fs_fd_readv(NAS_FD_LIMIT,USER_LIMIT-8,1)==-EBADF);
    assert((int64_t)nas_fs_fd_writev(NAS_FD_LIMIT,USER_LIMIT-8,1)==-EBADF);
    assert((int64_t)nas_fs_fd_readv(rd,USER_LIMIT-8,1)==-EFAULT);
    assert((int64_t)nas_fs_fd_writev(wr,USER_LIMIT-8,1)==-EFAULT);

    /* Linux lib/iov_iter.c fast path clamps a lone huge iovec to
     * MAX_RW_COUNT, whereas multi-iov import validates every original
     * address range and every signed length before any I/O. /dev/null
     * avoids allocating or copying a 2-GiB test payload. */
    reset_mapping();
    a008_iov(iov,0,0,INT64_MAX);
    assert(nas_fs_fd_readv(28,iov,1)==0);
    assert(nas_fs_fd_writev(28,iov,1)==UINT64_C(0x7ffff000));
    a008_iov(iov,0,0,UINT64_C(1)<<63);
    assert((int64_t)nas_fs_fd_readv(28,iov,1)==-EINVAL);
    assert((int64_t)nas_fs_fd_writev(28,iov,1)==-EINVAL);
    a008_iov(iov,0,0,UINT64_C(0x7ffff000));
    a008_iov(iov,1,0,INT64_MAX);
    assert((int64_t)nas_fs_fd_writev(28,iov,2)==-EFAULT);
    a008_iov(iov,0,USER_LIMIT-1,8);
    a008_iov(iov,1,0,UINT64_C(1)<<63);
    assert((int64_t)nas_fs_fd_writev(28,iov,2)==-EINVAL);
    a008_iov(iov,0,0,UINT64_C(0x7ffff000));
    a008_iov(iov,1,0,8);
    assert(nas_fs_fd_writev(28,iov,2)==UINT64_C(0x7ffff000));

    /* Import all vector metadata and atomic <=PIPE_BUF sources before a
     * provider commits. A bad LATER metadata page or source page must not
     * append a good first segment into a real pipe. */
    reset_mapping();
    memcpy(user_ptr(src+TEST_PAGE),"ok",2);
    a008_iov(TEST_BASE+TEST_PAGE-16,0,src+TEST_PAGE,2);
    present[1]=false;lazy[1]=false;
    assert((int64_t)nas_fs_fd_writev(wr,TEST_BASE+TEST_PAGE-16,2)==-EFAULT);
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==0);
    reset_mapping();
    memcpy(user_ptr(src),"ok",2);
    a008_iov(iov,0,src,2);
    a008_iov(iov,1,USER_LIMIT-2,4);
    assert((int64_t)nas_fs_fd_writev(wr,iov,2)==-EFAULT);
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==0);
    a008_iov(iov,1,TEST_BASE+3*TEST_PAGE+13,2);
    present[3]=false;lazy[3]=false;
    assert((int64_t)nas_fs_fd_writev(wr,iov,2)==-EFAULT);
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==0);

    /* A single 4000-byte write leaves 96 bytes in the 4096-byte pipe.
     * writev(50+100) must fail as ONE atomic nonblocking transaction;
     * writev(50+40) must then succeed and preserve byte order. */
    reset_mapping();
    nas_open_files[guest_task.fds->entries[wr].slot].status_flags|=0x800;
    memset(user_ptr(src),'X',4000);
    assert(nas_fs_fd_write(wr,src,4000)==4000);
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==4000);
    memset(user_ptr(TEST_BASE+2*TEST_PAGE+50),'A',50);
    memset(user_ptr(TEST_BASE+2*TEST_PAGE+150),'B',100);
    a008_iov(iov,0,TEST_BASE+2*TEST_PAGE+50,50);
    a008_iov(iov,1,TEST_BASE+2*TEST_PAGE+150,100);
    assert((int64_t)nas_fs_fd_writev(wr,iov,2)==-EAGAIN);
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==4000);
    a008_iov(iov,1,TEST_BASE+2*TEST_PAGE+150,40);
    assert(nas_fs_fd_writev(wr,iov,2)==90);
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==4090);
    uint64_t pipe_dst=TEST_BASE+3*TEST_PAGE;
    assert(nas_fs_fd_read(rd,pipe_dst,4090)==4090);
    assert(memcmp(user_ptr(pipe_dst),user_ptr(src),4000)==0);
    for(unsigned i=4000;i<4050;i++)assert(*user_ptr(pipe_dst+i)=='A');
    for(unsigned i=4050;i<4090;i++)assert(*user_ptr(pipe_dst+i)=='B');
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==0);

    /* Linux eventfd implements read_iter (4+4 is one 8B consumption), but
     * its write_iter falls back to scalar segment semantics, notably the
     * leading empty segment -> EINVAL and two 8B writes -> total 16B. */
    reset_mapping();
    uint64_t event=nas_eventfd_create(7,0x800);
    assert(event<NAS_FD_LIMIT);
    assert(guest_task.fds->entries[event].slot!=event);
    a008_iov(iov,0,dst,4);
    a008_iov(iov,1,dst+4,4);
    assert(nas_fs_fd_readv(event,iov,2)==8);
    uint64_t value=0;
    memcpy(&value,user_ptr(dst),8);
    assert(value==7);
    assert((int64_t)nas_fs_fd_read(event,dst,8)==-EAGAIN);

    uint64_t seven=7,eleven=11;
    memcpy(user_ptr(src),&seven,8);
    memcpy(user_ptr(src+16),&eleven,8);
    a008_iov(iov,0,src,0);
    a008_iov(iov,1,src,8);
    assert((int64_t)nas_fs_fd_writev(event,iov,2)==-EINVAL);
    assert((int64_t)nas_fs_fd_read(event,dst,8)==-EAGAIN);
    a008_iov(iov,0,src,8);
    a008_iov(iov,1,src,0);
    assert(nas_fs_fd_writev(event,iov,2)==8);
    assert(nas_fs_fd_read(event,dst,8)==8);
    memcpy(&value,user_ptr(dst),8);
    assert(value==7);
    a008_iov(iov,0,src,8);
    a008_iov(iov,1,src+16,8);
    assert(nas_fs_fd_writev(event,iov,2)==16);
    assert(nas_fs_fd_read(event,dst,8)==8);
    memcpy(&value,user_ptr(dst),8);
    assert(value==18);
    a008_iov(iov,0,src,4);
    a008_iov(iov,1,src+4,4);
    assert((int64_t)nas_fs_fd_writev(event,iov,2)==-EINVAL);
    puts("[NAS-A008 M106 PASS] real FileOps pipe/eventfd: IOV_MAX, bad FD precedence, metadata/source rollback, aggregate PIPE_BUF, read_iter/write fallback");
}

static void a012_stream_socket_vectors(unsigned sender,unsigned receiver) {
    /* The same ring backs this socketpair and an anonymous pipe, but only
     * the latter has PIPE_BUF atomic admission. A stream socket accepts
     * the available 96 bytes of a 150-byte request as short progress. */
    const uint64_t iov=TEST_BASE;
    const uint64_t fill=TEST_BASE+TEST_PAGE;
    const uint64_t first=TEST_BASE+2*TEST_PAGE+40;
    const uint64_t second=TEST_BASE+2*TEST_PAGE+140;
    const uint64_t dst=TEST_BASE+3*TEST_PAGE;
    reset_mapping();
    nas_open_files[guest_task.fds->entries[sender].slot].status_flags|=0x800;
    memset(user_ptr(fill),'X',4000);
    memset(user_ptr(first),'A',50);
    memset(user_ptr(second),'B',100);
    a008_iov(iov,0,first,50);
    a008_iov(iov,1,second,100);
    assert(nas_fs_fd_write(sender,fill,4000)==4000);
    assert(nas_fs_fd_writev(sender,iov,2)==96);
    assert((int64_t)nas_fs_fd_writev(sender,iov,2)==-EAGAIN);
    assert(nas_fs_fd_read(receiver,dst,4096)==4096);
    for(unsigned i=0;i<4000;i++)assert(*user_ptr(dst+i)=='X');
    for(unsigned i=4000;i<4050;i++)assert(*user_ptr(dst+i)=='A');
    for(unsigned i=4050;i<4096;i++)assert(*user_ptr(dst+i)=='B');
    assert(nas_fs_fd_writev(sender,iov,2)==150);
    assert(nas_fs_fd_read(receiver,dst,150)==150);
    for(unsigned i=0;i<50;i++)assert(*user_ptr(dst+i)=='A');
    for(unsigned i=50;i<150;i++)assert(*user_ptr(dst+i)=='B');
    *user_ptr(first)='Z';
    assert(nas_fs_fd_write(receiver,first,1)==1);
    assert(nas_fs_fd_read(sender,dst,1)==1);
    assert(*user_ptr(dst)=='Z');
    puts("[NAS-A012 M106 PASS] AF_UNIX stream accepts partial writev without PIPE_BUF atomic admission; duplex endpoints preserved");
}

int main(void) {
    test_bootstrap_owned_candidate();
    guest_task.mm=&guest_mm;
    guest_files.refs=1;
    guest_task.files=&guest_files;
    guest_task.fds=&guest_files.table;
    guest_task.fs=&guest_fs;
    nas_fd_system_init();
    reset_mapping();
    test_bootstrap_metadata_and_vfs_priority();

    /* A valid mapped RX destination must NOT publish descriptors. */
    can_write[0]=false;
    assert((int64_t)sys_pipe2(TEST_BASE,0)==-EFAULT);
    assert(live_fds()==0 && live_files()==0);
    assert((int64_t)sys_socketpair(1,1,0,TEST_BASE)==-EFAULT);
    assert(live_fds()==0 && live_files()==0);
    /* An eight-byte int[2] may straddle an actual page boundary. Fail the
     * whole pair if the second page cannot be written, with no stray FD. */
    reset_mapping();
    can_write[1]=false;
    assert((int64_t)sys_pipe2(TEST_BASE+TEST_PAGE-4,0)==-EFAULT);
    assert(live_fds()==0 && live_files()==0);

    /* A permitted but not-yet-present destination may fault in, then pipe2
     * must expose BOTH descriptors exactly once, with valid returned ints. */
    reset_mapping();
    present[1]=false;lazy[1]=true;
    uint64_t pipe_out=TEST_BASE+TEST_PAGE;
    assert(sys_pipe2(pipe_out,0)==0);
    assert(missing_faults>=1 && live_fds()==2 && live_files()==2);
    unsigned rd=pair_fd(pipe_out,0),wr=pair_fd(pipe_out,1);
    assert(rd!=wr && guest_task.fds->entries[rd].kind==FD_PIPE_R);
    assert(guest_task.fds->entries[wr].kind==FD_PIPE_W);
    unsigned pipe_slot=guest_task.fds->entries[rd].slot;
    assert(pipe_slot!=rd && guest_task.fds->entries[wr].slot!=wr);
    /* snapshot(0) below is the first PIPE OBJECT index, not an OFD slot. */

    /* The reader rejects a true RX destination without consuming bytes.
     * A subsequent COW/lazy read succeeds and returns the SAME payload. */
    const char text[]="long-lived-pipe-payload";
    memcpy(pages[2]+21,text,sizeof(text));
    assert(nas_fs_fd_write(wr,TEST_BASE+2*TEST_PAGE+21,sizeof(text))==sizeof(text));
    unsigned pending=0;
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==sizeof(text));
    can_write[3]=false;
    assert((int64_t)nas_fs_fd_read(rd,TEST_BASE+3*TEST_PAGE+1,sizeof(text))==-EFAULT);
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==sizeof(text));
    can_write[3]=true;cow[3]=true;
    assert(nas_fs_fd_read(rd,TEST_BASE+3*TEST_PAGE+1,sizeof(text))==sizeof(text));
    assert(protection_faults>=1);
    assert(memcmp(pages[3]+1,text,sizeof(text))==0);
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==0);

    /* Broken source address must not append garbage to the pipe. */
    assert((int64_t)nas_fs_fd_write(wr,USER_LIMIT-2,4)==-EFAULT);
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==0);
    present[2]=false;lazy[2]=true;
    assert(nas_fs_fd_write(wr,TEST_BASE+2*TEST_PAGE+21,sizeof(text))==sizeof(text));
    assert(missing_faults>=1);
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==sizeof(text));
    assert(nas_fs_fd_read(rd,TEST_BASE+1,sizeof(text))==sizeof(text));
    assert(memcmp(pages[0]+1,text,sizeof(text))==0);

    /* A large source crosses an unmapped but lazily resolvable page. The
     * pipe write stages both pages before committing. A read into an RX
     * second page may return a valid short count, retaining the remaining
     * bytes in the REAL pipe ring; a COW retry drains exactly that tail. */
    unsigned char expected[700];
    const uint64_t source=TEST_BASE+TEST_PAGE-256;
    for(unsigned i=0;i<sizeof(expected);i++) {
        expected[i]=(uint8_t)((i*41U+9U)&255U);
        *user_ptr(source+i)=expected[i];
    }
    present[1]=false;lazy[1]=true;
    assert(nas_fs_fd_write(wr,source,sizeof(expected))==sizeof(expected));
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==sizeof(expected));
    const uint64_t dest=TEST_BASE+3*TEST_PAGE-256;
    can_write[3]=false;
    assert(nas_fs_fd_read(rd,dest,sizeof(expected))==256);
    assert(memcmp(pages[2]+TEST_PAGE-256,expected,256)==0);
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==444);
    can_write[3]=true;cow[3]=true;
    assert(nas_fs_fd_read(rd,TEST_BASE+3*TEST_PAGE,444)==444);
    assert(memcmp(pages[3],expected+256,444)==0);
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==0);

    /* Linux PIPE_BUF admission is one transaction: if a short atomic
     * write spans a valid source page and a genuinely unmapped second
     * page, the provider must not publish the first 256 bytes. This
     * exercises source preflight before any ring write commit. */
    present[1]=false;lazy[1]=false;
    assert((int64_t)nas_fs_fd_write(wr,source,sizeof(expected))==-EFAULT);
    assert(nas_pipe_snapshot(0,&pending,NULL,NULL)&&pending==0);

    /* COW argument block must also allow socketpair, without spurious
     * all-or-nothing false EFAULT due to its read-only hardware PTE. */
    reset_mapping();
    cow[2]=true;
    uint64_t sock_out=TEST_BASE+2*TEST_PAGE+100;
    assert(sys_socketpair(1,1|0x80000,0,sock_out)==0);
    assert(protection_faults>=1);
    unsigned a=pair_fd(sock_out,0),b=pair_fd(sock_out,1);
    assert(a!=b && guest_task.fds->entries[a].kind==FD_SOCKET);
    assert(guest_task.fds->entries[b].kind==FD_SOCKET);
    assert(guest_task.fds->entries[a].cloexec && guest_task.fds->entries[b].cloexec);
    assert(live_fds()==4 && live_files()==4);

    /* accept4 must leave the pending peer owned by the listener when either
     * of its userspace outputs is RX. It may be published only after both
     * socklen and sockaddr have been made writable. */
    guest_task.fds->entries[30]=(struct nas_fd_entry){
        .kind=FD_SOCKET,.slot=20
    };
    nas_open_files[20].refcount=1;
    pending_accept_slot=21;
    nas_open_files[21].refcount=1;
    uint64_t family=TEST_BASE+3*TEST_PAGE+13;
    uint64_t length_ptr=TEST_BASE+23;
    uint32_t size=2;
    memcpy(pages[0]+23,&size,sizeof(size));
    can_write[0]=false;
    assert((int64_t)sys_unix_accept_slot(20,family,length_ptr,0)==-EFAULT);
    assert(!accepted_count && pending_accept_slot==21);
    can_write[0]=true;
    can_write[3]=false;
    assert((int64_t)sys_unix_accept_slot(20,family,length_ptr,0)==-EFAULT);
    assert(!accepted_count && pending_accept_slot==21);
    can_write[3]=true;cow[3]=true;
    uint64_t accepted=sys_unix_accept_slot(20,family,length_ptr,0x80000ULL);
    assert(accepted<NAS_FD_LIMIT && accepted_count==1);
    assert(pending_accept_slot==NAS_OPEN_FILE_LIMIT);
    assert(guest_task.fds->entries[accepted].kind==FD_SOCKET);
    assert(guest_task.fds->entries[accepted].slot==21);
    assert(guest_task.fds->entries[accepted].cloexec);
    assert(protection_faults>=2);
    assert(pages[3][13]==1 && pages[3][14]==0);
    assert(memcmp(pages[0]+23,&size,sizeof(size))==0);

    /* Linux /dev/null and /dev/zero discard writes without dereferencing
     * the source. An unmapped, in-range user pointer works; an address
     * RANGE outside user space is still rejected by VFS access_ok. */
    assert(nas_fd_install_new(&guest_task,28,22,FD_NULL,2,false));
    assert(nas_fd_install_new(&guest_task,29,23,FD_ZERO,2,false));
    nas_open_files[22].ops=&ops_null;
    nas_open_files[23].ops=&ops_zero;
    assert(nas_fs_fd_write(28,0,32)==32);
    assert(nas_fs_fd_write(29,0,32)==32);
    assert(nas_fs_fd_read(28,0,32)==0);
    assert((int64_t)nas_fs_fd_write(28,USER_LIMIT-2,32)==-EFAULT);
    assert((int64_t)nas_fs_fd_write(29,USER_LIMIT-2,32)==-EFAULT);
    assert((int64_t)nas_fs_fd_read(28,USER_LIMIT-2,32)==-EFAULT);
    assert((int64_t)nas_fs_fd_write(28,USER_LIMIT,0)==-EFAULT);
    assert((int64_t)nas_fs_fd_read(28,USER_LIMIT,0)==-EFAULT);

    /* Linux O_PATH FDs are metadata-only, including when the backend
     * would ordinarily discard a write without touching its source. */
    nas_open_files[22].status_flags=NAS_LINUX_O_PATH;
    assert((int64_t)nas_fs_fd_write(28,TEST_BASE,1)==-EBADF);
    nas_open_files[22].status_flags=2;

    /* Linux /dev/random and /dev/urandom share getrandom(2)'s real seeded
     * byte-stream backend, not a fixed test sequence or fake EOF. The new
     * character File uses the standard descriptor's owned read/copyout path. */
    const uint64_t random_seed[4]={
        UINT64_C(0x0102030405060708),UINT64_C(0x1112131415161718),
        UINT64_C(0x2122232425262728),UINT64_C(0x3132333435363738)
    };
    assert(nas_random_seed(random_seed,4));
    assert(nas_fd_install_new(&guest_task,26,24,FD_RANDOM,0,false));
    /* Other FileOps are intentionally not linked into this isolated host TU.
     * Exercise the production random FileOps callbacks directly. */
    nas_open_files[24].ops=&ops_random;
    assert(nas_open_files[24].ops && nas_open_files[24].ops->read);
    assert(nas_fs_fd_read(26,TEST_BASE+80,64)==64);
    uint8_t first_random[64];
    memcpy(first_random,user_ptr(TEST_BASE+80),sizeof(first_random));
    assert(nas_fs_fd_read(26,TEST_BASE+208,64)==64);
    assert(memcmp(first_random,user_ptr(TEST_BASE+208),64)!=0);
    assert(nas_random_seed(random_seed,4));
    assert(nas_random_getrandom(TEST_BASE+336,64,0)==64);
    assert(memcmp(first_random,user_ptr(TEST_BASE+336),64)==0);
    assert((int64_t)nas_fs_fd_write(26,TEST_BASE+80,1)==-EBADF);
    /* O_RDWR permits writes but the unimplemented entropy-injection
     * operation must be rejected, rather than claim false success. */
    nas_open_files[24].status_flags=2;
    assert((int64_t)nas_fs_fd_write(26,TEST_BASE+80,1)==-EOPNOTSUPP);

    a008_vectors(rd,wr);
    a012_stream_socket_vectors(a,b);
    /* Real syscall File/FD publication: an anonymous tmpfs inode must
     * transfer its sole VFS pin into the existing OpenFile, not create a
     * private memfd registry or publish a path-backed stand-in. */
    unsigned before_fds=live_fds(),before_files=live_files();
    uint64_t anonymous_fd=nas_fs_open_anon_tmpfs("abc/def",true);
    assert(anonymous_fd<NAS_FD_LIMIT && anonymous_factory_calls==1);
    struct nas_fd_entry anonymous_entry=guest_task.fds->entries[anonymous_fd];
    assert(anonymous_entry.kind==FD_ARCHIVE && anonymous_entry.cloexec);
    struct nas_open_file *anonymous_open=&nas_open_files[anonymous_entry.slot];
    assert(anonymous_open->refcount==1 && anonymous_open->vnode_pinned &&
           anonymous_open->vnode.inode==&anonymous_backend_inode &&
           anonymous_open->status_flags==2ULL &&
           !strcmp(anonymous_open->origin_path,"/memfd:abc/def (deleted)"));
    assert(live_fds()==before_fds+1 && live_files()==before_files+1);
    nas_fs_close_fd(anonymous_fd);
    assert(anonymous_unpins==1 && live_fds()==before_fds &&
           live_files()==before_files);
    char longest_memfd_name[250];
    memset(longest_memfd_name,'m',sizeof(longest_memfd_name)-1);
    longest_memfd_name[sizeof(longest_memfd_name)-1]=0;
    anonymous_fd=nas_fs_open_anon_tmpfs(longest_memfd_name,false);
    assert(anonymous_fd<NAS_FD_LIMIT);
    anonymous_entry=guest_task.fds->entries[anonymous_fd];
    anonymous_open=&nas_open_files[anonymous_entry.slot];
    assert(!strncmp(anonymous_open->origin_path,"/memfd:",7));
    assert(strlen(anonymous_open->origin_path)==255);
    assert(!strcmp(anonymous_open->origin_path+245," (deleted)"));
    nas_fs_close_fd(anonymous_fd);
    assert(anonymous_unpins==2);
    anonymous_factory_error=-ENOSPC;
    assert((int64_t)nas_fs_open_anon_tmpfs("failure",false)==-ENOSPC);
    assert(anonymous_factory_calls==3 && anonymous_unpins==2 &&
           live_fds()==before_fds && live_files()==before_files);
    anonymous_factory_error=0;
    puts("[MEMFD OPENFILE TRANSACTION PASS] pinned unnamed VFS inode, OFD/FD close rollback, procfd label bounds");
    puts("[M106 USER-COPY SYSCALL PASS] real pipe + syscall adapters: RO atomicity, lazy/COW, no pipe loss, socketpair publication, accept retains pending peer, null/zero and random char File parity, O_PATH write EBADF");
    return 0;
}
