/* M65: link the genuine common procfs provider against a deterministic
 * task/frame/clock fixture. Full native QEMU separately checks Linux ELF. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../kernel/task.h"
#include "../kernel/fs/procfs.h"
#include "../kernel/fs/vfs.h"
#include "../kernel/errno.h"
#include "../kernel/mm/vm.h"
#include "../kernel/arch/user_pt.h"
static struct nas_task tasks[3];
static struct nas_files files_fixture;
static struct nas_mm address_spaces[2];
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
struct nas_task *nas_current_task=&tasks[0];
/* The production /proc/mounts must consume the VFS's LIVE mount snapshot,
 * not a second hard-coded procfs table. This fake is deliberately mutable:
 * even an already-open proc inode sees mounts appear/disappear. */
static struct nas_vfs_mount_snapshot fixture_mounts[80];
static unsigned fixture_mount_count;
int nas_vfs_mount_snapshot(unsigned ordinal,struct nas_vfs_mount_snapshot *out) {
    if(!out)return -EINVAL;
    if(ordinal>=fixture_mount_count)return -ENOENT;
    *out=fixture_mounts[ordinal];
    return 0;
}
static void fixture_add_mount(const char *source,const char *target,
                              const char *fstype,bool read_only) {
    assert(fixture_mount_count<80);
    struct nas_vfs_mount_snapshot *m=&fixture_mounts[fixture_mount_count++];
    *m=(struct nas_vfs_mount_snapshot){0};
    assert(strlen(source)<sizeof(m->source));
    assert(strlen(target)<sizeof(m->target));
    assert(strlen(fstype)<sizeof(m->fstype));
    strcpy(m->source,source);strcpy(m->target,target);
    strcpy(m->fstype,fstype);m->read_only=read_only;
}
struct nas_task *nas_task_at(unsigned n) {return n<3?&tasks[n]:0;}
struct nas_task *nas_task_by_pid(uint64_t pid) {
    for(unsigned i=0;i<3;i++)if(tasks[i].state!=NAS_TASK_FREE && tasks[i].pid==pid)return &tasks[i];
    return 0;
}
uint64_t nas_managed_frames(void){return 1024;}
uint64_t nas_free_frames(void){return 512;}
uint64_t nas_arch_monotonic_ns(void){return 12500000000ULL;}
bool nas_vm_walk_task_vmas(const struct nas_task *task,
                            nas_vma_visit callback,void *ctx) {
    (void)task;
    const struct nas_anon_region text={
        .start=0x1000,.end=0x3000,.prot=5,
        .kind=NAS_VMA_ELF_PRIVATE_SNAPSHOT
    };
    const struct nas_anon_region data={
        .start=0x4000,.end=0x5000,.prot=3,.kind=NAS_VMA_HEAP
    };
    const struct nas_anon_region reserved={
        .start=0x6000,.end=0x7000,.prot=0,.kind=NAS_VMA_ANON_PRIVATE
    };
    return callback(&text,ctx) && callback(&data,ctx) &&
           callback(&reserved,ctx);
}
bool nas_time_boot_epoch_seconds(uint64_t *out) {
    if(!out)return false;
    *out=1760000000ULL;return true;
}
void nas_task_cpu_account_snapshot(uint64_t *busy,uint64_t *idle,
                                   uint64_t loads[3],uint64_t *switches) {
    if(busy)*busy=1500000000ULL;
    if(idle)*idle=11000000000ULL;
    if(loads){loads[0]=65536;loads[1]=32768;loads[2]=16384;}
    if(switches)*switches=12;
}
static void check_file(const char *path,const char *wanted) {
    struct nas_proc_node node={0};
    assert(nas_proc_lookup(path,&node) && !nas_proc_is_dir(node));
    uint8_t data[1536]={0};
    int64_t n=nas_proc_read(node,0,data,sizeof(data)-1);
    assert(n>0 && strstr((char *)data,wanted));
    uint8_t split[10]={0};
    assert(nas_proc_read(node,0,split,5)==5);
    assert(!memcmp(split,data,5));
    assert(nas_proc_read(node,(uint64_t)n,data,1)==0);
}
/* Compare exact offset reads, not just a prefix in a 1536-byte stack buffer.
 * Test every byte boundary and a few inhomogeneous small chunks, including
 * offsets after 1536 and beyond EOF. */
static size_t proc_mount_read_all(struct nas_proc_node node,unsigned chunk,
                                  char *out,size_t capacity) {
    size_t offset=0;
    assert(chunk && capacity);
    while(offset<capacity-1) {
        unsigned take=chunk;
        if(take>capacity-1-offset)take=(unsigned)(capacity-1-offset);
        int64_t n=nas_proc_read(node,offset,(uint8_t *)out+offset,take);
        assert(n>=0 && (unsigned)n<=take);
        if(!n)break;
        offset+=(size_t)n;
    }
    assert(offset<capacity-1);
    out[offset]=0;
    assert(nas_proc_read(node,offset,(uint8_t *)out,1)==0);
    assert(nas_proc_read(node,offset+12345,(uint8_t *)out,1)==0);
    return offset;
}
static void check_live_mounts(void) {
    struct nas_proc_node global={0},per_pid={0};
    assert(nas_proc_lookup("/proc/mounts",&global));
    assert(nas_proc_lookup("/proc/23/mounts",&per_pid));
    assert(!nas_proc_is_dir(global) && !nas_proc_is_dir(per_pid));
    uint8_t small[8]={0};
    fixture_mount_count=0;
    assert(nas_proc_read(global,0,small,1)==0);
    assert(nas_proc_read(per_pid,0,small,1)==0);
    fixture_add_mount("rootfs","/","rootfs",true);
    fixture_add_mount("tmpfs","/tmp","tmpfs",false);
    fixture_add_mount("proc","/proc","proc",true);
    fixture_add_mount("virtio0","/mnt","lwext4",false);
    fixture_add_mount("virtio0p1","/gpt","lwext4",false);
    fixture_add_mount("littlefs0","/flash","littlefs",false);
    fixture_add_mount("ramfs","/ramfs","j-kent-ramfs",false);
    fixture_add_mount("virtio device","/space tab\tline\nslash\\",
                      "ext4 test",false);
    static const char escaped[]=
        "virtio\\040device /space\\040tab\\011line\\012slash\\134 "
        "ext4\\040test rw 0 0\n";
    static const char basics[]=
        "rootfs / rootfs ro 0 0\n"
        "tmpfs /tmp tmpfs rw 0 0\n"
        "proc /proc proc ro 0 0\n"
        "virtio0 /mnt lwext4 rw 0 0\n"
        "virtio0p1 /gpt lwext4 rw 0 0\n"
        "littlefs0 /flash littlefs rw 0 0\n"
        "ramfs /ramfs j-kent-ramfs rw 0 0\n";
    char expected[24000]={0};
    strcpy(expected,basics);strcat(expected,escaped);
    /* More than 1536 bytes: the old procfs scalar writer silently clipped
     * this real-capacity VFS list. Real 256-mount VFS capacity is larger. */
    for(unsigned i=0;i<52;i++) {
        char source[64],target[256],line[400];
        assert(snprintf(source,sizeof(source),"block%02u",i)>0);
        assert(snprintf(target,sizeof(target),"/many/other/mount%02u",i)>0);
        fixture_add_mount(source,target,"ext4",i%2==0);
        int n=snprintf(line,sizeof(line),"%s %s ext4 %s 0 0\n",
                       source,target,i%2==0?"ro":"rw");
        assert(n>0 && (size_t)n<sizeof(line));
        assert(strlen(expected)+(size_t)n<sizeof(expected));
        strcat(expected,line);
    }
    const size_t wanted=strlen(expected);
    assert(wanted>1536);
    char all[24000],fragmented[24000],pid_view[24000];
    assert(proc_mount_read_all(global,4096,all,sizeof(all))==wanted);
    assert(!memcmp(all,expected,wanted+1));
    assert(proc_mount_read_all(global,7,fragmented,sizeof(fragmented))==wanted);
    assert(!memcmp(fragmented,expected,wanted+1));
    assert(proc_mount_read_all(per_pid,13,pid_view,sizeof(pid_view))==wanted);
    assert(!memcmp(pid_view,expected,wanted+1));
    assert(nas_proc_read(global,0,small,0)==0);
    assert(nas_proc_read(global,1535,small,4)==4);
    assert(!memcmp(small,expected+1535,4));
    assert(nas_proc_read(global,1537,small,3)==3);
    assert(!memcmp(small,expected+1537,3));
    /* An open /proc/mounts inode must not keep a stale synthetic snapshot. */
    fixture_mount_count=3;
    const size_t now=strlen("rootfs / rootfs ro 0 0\n"
                            "tmpfs /tmp tmpfs rw 0 0\n"
                            "proc /proc proc ro 0 0\n");
    assert(proc_mount_read_all(global,1,all,sizeof(all))==now);
    assert(!memcmp(all,basics,now));
    assert(proc_mount_read_all(per_pid,3,pid_view,sizeof(pid_view))==now);
    assert(!memcmp(all,pid_view,now+1));
    puts("[M65 MOUNTS PASS] live VFS mount snapshot, six fields, escapes, >1536, split reads, offset and PID parity");
}
int main(void) {
    tasks[0].state=NAS_TASK_RUNNING;tasks[0].pid=1;tasks[0].tgid=1;
    tasks[0].mm=&address_spaces[0];
    tasks[1].mm=&address_spaces[1];
    tasks[2].mm=&address_spaces[1]; /* worker shares process MM */
    files_fixture.refs=1;
    tasks[0].files=&files_fixture;
    tasks[0].fds=&files_fixture.table;
    tasks[0].fds->entries[1]=(struct nas_fd_entry){FD_NULL,4,0};
    nas_open_files[4].refcount=1;
    strcpy(nas_open_files[4].origin_path,"/dev/null");
    nas_open_files[4].position=27;
    nas_open_files[4].status_flags=0;
    tasks[0].ppid=0;strcpy(tasks[0].comm,"init");
    tasks[1].state=NAS_TASK_BLOCKED;tasks[1].pid=23;tasks[1].tgid=23;
    tasks[1].ppid=1;strcpy(tasks[1].comm,"sleep");
    tasks[2].state=NAS_TASK_READY;tasks[2].pid=24;tasks[2].tgid=23;
    tasks[2].is_thread=true;tasks[2].ppid=1;strcpy(tasks[2].comm,"worker");
    struct nas_proc_node root={0},entry={0};
    assert(nas_proc_lookup("/proc",&root) && nas_proc_is_dir(root));
    assert(!nas_proc_lookup("/procmeminfo",&entry));
    assert(!nas_proc_lookup("/proc/999/status",&entry));
    assert(nas_proc_lookup("/proc/self",&entry) && entry.pid==1);
    struct nas_proc_node fd_dir={0},fd_info={0};
    char fd_path[256];
    assert(nas_proc_lookup("/proc/1/fd",&fd_dir) && nas_proc_is_dir(fd_dir));
    assert(nas_proc_dir_path(fd_dir,fd_path) && !strcmp(fd_path,"/proc/1/fd"));
    assert(nas_proc_child(fd_dir,0,fd_path,&fd_info) &&
           !strcmp(fd_path,"1") && fd_info.kind==NAS_PROC_FD_LINK);
    assert(nas_proc_lookup("/proc/self/fd/1",&fd_info));
    struct nas_proc_node fd_by_ino={0};
    uint64_t owned_link_ino=nas_proc_ino(fd_info);
    assert(nas_proc_fd_link_from_ino(owned_link_ino,&fd_by_ino));
    assert(fd_by_ino.pid==1 && fd_by_ino.fd==1);
    /* Inherited inode identity follows the original process even when
     * current_task changes and 'self' acquires a different meaning. */
    nas_current_task=&tasks[1];
    assert(nas_proc_fd_link_from_ino(owned_link_ino,&fd_by_ino));
    assert(fd_by_ino.pid==1);
    nas_current_task=&tasks[0];
    assert(!nas_proc_fd_link_from_ino(owned_link_ino+1,&fd_by_ino));
    /* Failed typed inode decodes cannot change an already-owned path
     * identity. Particularly, FDINFO has the neighboring inode number. */
    assert(fd_by_ino.kind==NAS_PROC_FD_LINK &&
           fd_by_ino.pid==1 && fd_by_ino.fd==1);
    assert(!nas_proc_directory_from_ino(owned_link_ino,&fd_by_ino));
    assert(fd_by_ino.kind==NAS_PROC_FD_LINK &&
           fd_by_ino.pid==1 && fd_by_ino.fd==1);
    char link[64]={0};
    assert(nas_proc_readlink(fd_by_ino,link,sizeof(link))==9);
    assert(!memcmp(link,"/dev/null",9));
    assert(nas_proc_lookup("/proc/1/fdinfo/1",&fd_info));
    uint8_t info[128]={0};
    assert(nas_proc_read(fd_info,0,info,sizeof(info)-1)>0);
    assert(strstr((char *)info,"pos:\t27"));
    assert(strstr((char *)info,"flags:\t0"));
    tasks[0].fds->entries[1].kind=FD_CLOSED;
    assert(!nas_proc_fd_link_from_ino(owned_link_ino,&fd_by_ino));
    assert(!nas_proc_lookup("/proc/1/fd/1",&fd_info));
    assert(nas_proc_lookup("/proc/self/status",&entry) && entry.pid==1);
    struct nas_proc_node original_status,decoded_status;
    assert(nas_proc_lookup("/proc/23/status",&original_status));
    assert(nas_proc_node_from_ino(nas_proc_ino(original_status),
                                   &decoded_status));
    assert(decoded_status.pid==23 &&
           decoded_status.kind==NAS_PROC_PID_STATUS);
    uint8_t inode_data[192]={0};
    assert(nas_proc_read(decoded_status,0,inode_data,sizeof(inode_data)-1)>0);
    assert(strstr((const char *)inode_data,"Threads:"));
    assert(!nas_proc_node_from_ino(nas_proc_ino(original_status)+99,
                                   &decoded_status));
    check_file("/proc/23/status","Threads:\t2");
    check_file("/proc/23/stat","23 (sleep) S 1");
    check_file("/proc/23/maps","0000000000001000-0000000000003000 r-xp");
    check_file("/proc/23/maps","0000000000004000-0000000000005000 rw-p");
    check_file("/proc/23/maps","0000000000006000-0000000000007000 ---p");
    struct nas_proc_node mapnode={0};
    assert(nas_proc_lookup("/proc/23/maps",&mapnode));
    uint8_t allmaps[256]={0},half[256]={0};
    int64_t mapslen=nas_proc_read(mapnode,0,allmaps,sizeof(allmaps));
    assert(mapslen>60);
    assert(nas_proc_read(mapnode,25,half,sizeof(half))==mapslen-25);
    assert(!memcmp(half,allmaps+25,(size_t)(mapslen-25)));
    check_file("/proc/meminfo","MemTotal:       4096 kB");
    check_file("/proc/uptime","12.50");
    check_file("/proc/stat","cpu  0 0 150 1100");
    check_file("/proc/stat","btime 1760000000");
    check_file("/proc/loadavg","1.00 0.50 0.25");
    check_file("/proc/vmstat","nr_free_pages 512");
    check_live_mounts();
    check_file("/proc/23/statm"," 0 0 0 0 0 0");
    tasks[1].commandline_bytes=12;
    memcpy(tasks[1].commandline,"sleep\0three\0",12);
    assert(nas_proc_lookup("/proc/23/cmdline",&entry));
    uint8_t cmdline[32]={0};
    assert(nas_proc_read(entry,0,cmdline,32)==12);
    assert(!memcmp(cmdline,"sleep\0three\0",12));
    char dirpath[256]={0};
    assert(nas_proc_dir_path(root,dirpath) && !strcmp(dirpath,"/proc"));
    assert(nas_proc_lookup("/proc/23",&entry));
    assert(nas_proc_dir_path(entry,dirpath) && !strcmp(dirpath,"/proc/23"));
    /* The stable PID directory identity is inherited even if the
     * caller's /proc/self alias changes to the child process. */
    struct nas_proc_node from_inode={0};
    assert(nas_proc_directory_from_ino(nas_proc_ino(entry),&from_inode));
    nas_current_task=&tasks[0];
    assert(nas_proc_dir_path(from_inode,dirpath) &&
           !strcmp(dirpath,"/proc/23"));
    nas_current_task=&tasks[1];
    assert(nas_proc_dir_path(from_inode,dirpath) &&
           !strcmp(dirpath,"/proc/23"));
    nas_current_task=&tasks[0];
    char name[32];
    /* The root /proc static entries can grow: find the first real PID
     * without assuming an obsolete, fixed root getdents64 index. */
    unsigned root_pid_index=0;
    for(;root_pid_index<64;root_pid_index++) {
        assert(nas_proc_child(root,root_pid_index,name,&entry));
        if(!strcmp(name,"1"))break;
    }
    assert(root_pid_index<64);
    assert(nas_proc_child(root,root_pid_index+1,name,&entry) &&
           !strcmp(name,"23"));
    assert(!nas_proc_child(root,root_pid_index+2,name,&entry));
    assert(nas_proc_lookup("/proc/23",&entry));
    assert(nas_proc_child(entry,1,name,&root) && !strcmp(name,"status"));
    /* Native ptrace -f requires this genuine Task-backed traversal:
     * threads never appear as separate top-level processes. */
    struct nas_proc_node group={0},thread={0},threadfile={0};
    assert(nas_proc_lookup("/proc/23/task",&group) && nas_proc_is_dir(group));
    assert(group.pid==23 && group.tgid==23);
    assert(nas_proc_child(group,0,name,&thread) && !strcmp(name,"23"));
    assert(thread.kind==NAS_PROC_TID_DIR && thread.pid==23 && thread.tgid==23);
    assert(nas_proc_child(group,1,name,&thread) && !strcmp(name,"24"));
    assert(thread.kind==NAS_PROC_TID_DIR && thread.pid==24 && thread.tgid==23);
    assert(!nas_proc_child(group,2,name,&thread));
    assert(!nas_proc_lookup("/proc/1/task/24",&thread));
    assert(nas_proc_lookup("/proc/23/task/24",&thread));
    assert(nas_proc_dir_path(group,dirpath) &&
           !strcmp(dirpath,"/proc/23/task"));
    assert(nas_proc_dir_path(thread,dirpath) &&
           !strcmp(dirpath,"/proc/23/task/24"));
    assert(nas_proc_child(thread,0,name,&threadfile) && !strcmp(name,"stat"));
    assert(threadfile.pid==24 && threadfile.tgid==23);
    assert(nas_proc_lookup("/proc/23/task/24/stat",&threadfile));
    assert(nas_proc_node_from_ino(nas_proc_ino(threadfile),
                                   &decoded_status));
    assert(decoded_status.pid==24 && decoded_status.tgid==23 &&
           decoded_status.kind==NAS_PROC_PID_STAT);
    uint8_t record[128]={0};
    assert(nas_proc_read(threadfile,0,record,sizeof(record)-1)>0);
    assert(strstr((char *)record,"24 (worker) R 1"));
    assert(nas_proc_lookup("/proc/self/task/1/status",&threadfile));
    assert(threadfile.pid==1 && threadfile.tgid==1);
    assert(nas_proc_lookup("/proc/23/task/24/comm",&threadfile));
    assert(nas_proc_read(threadfile,0,record,sizeof(record)-1)==7);
    assert(!memcmp(record,"worker\n",7));
    tasks[2].state=NAS_TASK_FREE;
    assert(!nas_proc_lookup("/proc/23/task/24",&thread));
    assert(!nas_proc_lookup("/proc/23/task/24/stat",&threadfile));
    puts("[M65 PROCFS PASS] dynamic per-task namespace, short reads, uptime/memory, thread groups");
    return 0;
}
