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
struct nas_task *nas_current_task = &guest_task;
static unsigned pending_accept_slot=NAS_OPEN_FILE_LIMIT, accepted_count;

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
int nas_vfs_node_unpin(const struct nas_fs_node *node){(void)node;return 0;}
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
    .read_plan=pipe_read_plan,.read=pipe_fd_read
};
static const struct nas_file_ops m106_pipe_writer={
    .write_plan=pipe_write_plan,.write=pipe_fd_write,
    .atomic_write_limit=4096
};
static const struct nas_file_ops m106_stream_socket={
    .read_plan=pipe_read_plan,.write_plan=pipe_write_plan,
    .read=pipe_fd_read,.write=pipe_fd_write,.atomic_write_limit=4096
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
int nas_fd_reserve(const struct nas_task *t,unsigned minimum,unsigned count,
                   unsigned fds[2],unsigned slots[2],const char *operation) {
    (void)operation;
    unsigned fd_count=0,slot_count=0;
    for(unsigned i=minimum;i<NAS_FD_LIMIT&&fd_count<count;i++)
        if(!t->fds->entries[i].kind)fds[fd_count++]=i;
    if(fd_count<count)return -EMFILE;
    /* Exercise user FD != shared OpenFile slot; this fixture validates
     * publication, not the allocator's lowest-available-slot policy. */
    for(unsigned i=TEST_OFD_START;i<NAS_OPEN_FILE_LIMIT&&slot_count<count;i++)
        if(!nas_open_files[i].refcount)slots[slot_count++]=i;
    return slot_count<count?-ENFILE:0;
}
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
                             kind==FD_SOCKET ? &m106_stream_socket : NULL;
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

int main(void) {
    guest_task.mm=&guest_mm;
    guest_files.refs=1;
    guest_task.files=&guest_files;
    guest_task.fds=&guest_files.table;
    nas_fd_system_init();
    reset_mapping();

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
    assert((int64_t)sys_unix_accept(30,family,length_ptr,0)==-EFAULT);
    assert(!accepted_count && pending_accept_slot==21);
    can_write[0]=true;
    can_write[3]=false;
    assert((int64_t)sys_unix_accept(30,family,length_ptr,0)==-EFAULT);
    assert(!accepted_count && pending_accept_slot==21);
    can_write[3]=true;cow[3]=true;
    uint64_t accepted=sys_unix_accept(30,family,length_ptr,0x80000ULL);
    assert(accepted<NAS_FD_LIMIT && accepted_count==1);
    assert(pending_accept_slot==NAS_OPEN_FILE_LIMIT);
    assert(guest_task.fds->entries[accepted].kind==FD_SOCKET);
    assert(guest_task.fds->entries[accepted].slot==21);
    assert(guest_task.fds->entries[accepted].cloexec);
    assert(protection_faults>=2);
    assert(pages[3][13]==1 && pages[3][14]==0);
    assert(memcmp(pages[0]+23,&size,sizeof(size))==0);

    puts("[M106 USER-COPY SYSCALL PASS] real pipe + syscall adapters: RO atomicity, lazy/COW, no pipe loss, socketpair publication, accept retains pending peer until writable");
    return 0;
}
