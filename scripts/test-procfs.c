/* M65: link the genuine common procfs provider against a deterministic
 * task/frame/clock fixture. Full native QEMU separately checks Linux ELF. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../kernel/task.h"
#include "../kernel/fs/procfs.h"
static struct nas_task tasks[3];
struct nas_task *nas_current_task=&tasks[0];
struct nas_task *nas_task_at(unsigned n) {return n<3?&tasks[n]:0;}
struct nas_task *nas_task_by_pid(uint64_t pid) {
    for(unsigned i=0;i<3;i++)if(tasks[i].state!=NAS_TASK_FREE && tasks[i].pid==pid)return &tasks[i];
    return 0;
}
uint64_t nas_managed_frames(void){return 1024;}
uint64_t nas_free_frames(void){return 512;}
uint64_t nas_arch_monotonic_ns(void){return 12500000000ULL;}
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
int main(void) {
    tasks[0].state=NAS_TASK_RUNNING;tasks[0].pid=1;tasks[0].tgid=1;
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
    assert(nas_proc_lookup("/proc/self/status",&entry) && entry.pid==1);
    check_file("/proc/23/status","Threads:\t2");
    check_file("/proc/23/stat","23 (sleep) S 1");
    check_file("/proc/meminfo","MemTotal:       4096 kB");
    check_file("/proc/uptime","12.50");
    check_file("/proc/stat","cpu  0 0 150 1100");
    check_file("/proc/stat","btime 1760000000");
    check_file("/proc/loadavg","1.00 0.50 0.25");
    check_file("/proc/vmstat","nr_free_pages 512");
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
    char name[32];
    assert(nas_proc_child(root,8,name,&entry) && !strcmp(name,"1"));
    assert(nas_proc_child(root,9,name,&entry) && !strcmp(name,"23"));
    assert(!nas_proc_child(root,10,name,&entry));
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
