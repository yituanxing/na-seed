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
    char dirpath[256]={0};
    assert(nas_proc_dir_path(root,dirpath) && !strcmp(dirpath,"/proc"));
    assert(nas_proc_lookup("/proc/23",&entry));
    assert(nas_proc_dir_path(entry,dirpath) && !strcmp(dirpath,"/proc/23"));
    char name[32];
    assert(nas_proc_child(root,5,name,&entry) && !strcmp(name,"1"));
    assert(nas_proc_child(root,6,name,&entry) && !strcmp(name,"23"));
    assert(!nas_proc_child(root,7,name,&entry));
    assert(nas_proc_lookup("/proc/23",&entry));
    assert(nas_proc_child(entry,1,name,&root) && !strcmp(name,"status"));
    puts("[M65 PROCFS PASS] dynamic per-task namespace, short reads, uptime/memory, thread groups");
    return 0;
}
