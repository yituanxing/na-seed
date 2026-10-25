/* Read-only process information filesystem: no copied task snapshot and no
 * userspace-address access. All three LP64 ports use the same provider. */
#include "procfs.h"
#include "../task.h"
#include "../mm/frame.h"
#include "../arch/arch.h"
#include "../time/clock.h"
#include "../task/user_stack.h"
#include "../lib/string.h"
#include "../errno.h"
#include <stdint.h>
#include <stdbool.h>
#define LIMIT 1536U
struct writer {char *data; unsigned length,capacity;};
static void ch(struct writer *w,char c) {if(w->length<w->capacity)w->data[w->length++]=c;}
static void word(struct writer *w,const char *s) {while(*s)ch(w,*s++);}
static void decimal(struct writer *w,uint64_t v) {
    char digits[24];unsigned n=0;
    do {digits[n++]=(char)('0'+v%10U);v/=10U;}while(v && n<sizeof(digits));
    while(n)ch(w,digits[--n]);
}
static void number_name(uint64_t value,char name[32]) {
    struct writer w={name,0,31};decimal(&w,value);name[w.length]=0;
}
static const struct nas_task *living(uint64_t pid) {
    struct nas_task *t=nas_task_by_pid(pid);
    return t && t->state!=NAS_TASK_FREE ? t : 0;
}
static bool numeric(const char *s,uint64_t *v,const char **after) {
    uint64_t id=0;unsigned count=0;
    while(*s>='0' && *s<='9') {
        if(id>(UINT64_MAX-(unsigned)(*s-'0'))/10U)return false;
        id=id*10U+(unsigned)(*s-'0');s++;count++;
    }
    if(!count || !id)return false;
    *v=id;*after=s;return true;
}
bool nas_proc_lookup(const char *path,struct nas_proc_node *out) {
    if(!path || !out)return false;
    *out=(struct nas_proc_node){0};
    if(!strcmp(path,"/proc")) {out->kind=NAS_PROC_ROOT;return true;}
    if(!strcmp(path,"/proc/self")) {
        if(!nas_current_task)return false;
        out->kind=NAS_PROC_PID_DIR;out->pid=nas_current_task->tgid;
        return living(out->pid)!=0;
    }
    if(!strcmp(path,"/proc/meminfo")) {out->kind=NAS_PROC_MEMINFO;return true;}
    if(!strcmp(path,"/proc/uptime")) {out->kind=NAS_PROC_UPTIME;return true;}
    if(!strcmp(path,"/proc/version")) {out->kind=NAS_PROC_VERSION;return true;}
    if(!strcmp(path,"/proc/cpuinfo")) {out->kind=NAS_PROC_CPUINFO;return true;}
    if(!strcmp(path,"/proc/stat")) {out->kind=NAS_PROC_STAT;return true;}
    if(!strcmp(path,"/proc/loadavg")) {out->kind=NAS_PROC_LOADAVG;return true;}
    if(!strcmp(path,"/proc/vmstat")) {out->kind=NAS_PROC_VMSTAT;return true;}
    if(strncmp(path,"/proc/",6))return false;
    const char *p=path+6;
    uint64_t pid=0;
    if(!numeric(p,&pid,&p)) {
        if(strncmp(p,"self/",5))return false;
        if(!nas_current_task)return false;
        pid=nas_current_task->tgid;p+=5;
        if(!*p)return false;
        p--; /* same component separator handling as the PID branch */
    }
    if(!living(pid))return false;
    out->pid=pid;
    if(!strcmp(p,"/task")) {
        out->kind=NAS_PROC_TASK_DIR;
        out->tgid=pid;
        return true;
    }
    if(!strncmp(p,"/task/",6)) {
        uint64_t tid=0;
        const char *suffix=0;
        if(!numeric(p+6,&tid,&suffix))return false;
        const struct nas_task *thread=living(tid);
        if(!thread || thread->tgid!=pid)return false;
        out->pid=tid;
        out->tgid=pid;
        p=suffix;
        if(!*p){out->kind=NAS_PROC_TID_DIR;return true;}
        /* Thread-local /proc status/stat/comm are regular files. */
    }
    if(!*p)out->kind=NAS_PROC_PID_DIR;
    else if(!strcmp(p,"/stat"))out->kind=NAS_PROC_PID_STAT;
    else if(!strcmp(p,"/status"))out->kind=NAS_PROC_PID_STATUS;
    else if(!strcmp(p,"/comm"))out->kind=NAS_PROC_PID_COMM;
    else if(!strcmp(p,"/cmdline"))out->kind=NAS_PROC_PID_CMDLINE;
    else if(!strcmp(p,"/statm"))out->kind=NAS_PROC_PID_STATM;
    else return false;
    return true;
}
bool nas_proc_dir_path(struct nas_proc_node node,char path[256]) {
    if(!path || !nas_proc_is_dir(node))return false;
    const char *prefix="/proc";unsigned p=0;
    while(prefix[p]){path[p]=prefix[p];p++;}
    if(node.kind!=NAS_PROC_ROOT) {
        uint64_t leader=node.tgid?node.tgid:node.pid;
        if(!living(leader))return false;
        path[p++]='/';char number[32];number_name(leader,number);
        for(unsigned i=0;number[i];i++)path[p++]=number[i];
        if(node.kind==NAS_PROC_TASK_DIR || node.kind==NAS_PROC_TID_DIR) {
            const char *suffix="/task";
            for(unsigned i=0;suffix[i];i++)path[p++]=suffix[i];
            if(node.kind==NAS_PROC_TID_DIR) {
                if(!living(node.pid) || living(node.pid)->tgid!=leader)return false;
                path[p++]='/';number_name(node.pid,number);
                for(unsigned i=0;number[i];i++)path[p++]=number[i];
            }
        }
    }
    path[p]=0;return true;
}
bool nas_proc_is_dir(struct nas_proc_node node) {
    return node.kind==NAS_PROC_ROOT || node.kind==NAS_PROC_PID_DIR ||
           node.kind==NAS_PROC_TASK_DIR || node.kind==NAS_PROC_TID_DIR;
}
uint32_t nas_proc_mode(struct nas_proc_node node) {
    return nas_proc_is_dir(node) ? 0040555U : 0100444U;
}
uint64_t nas_proc_ino(struct nas_proc_node node) {
    /* Per-task inode identity is distinct from the process-level name;
     * retain stable inodes across dup/fork/readdir and TID traversal. */
    return node.pid ? 80000U+node.pid*128U+(uint64_t)node.kind +
           (node.tgid ? 64U : 0U) : 70000U+(uint64_t)node.kind;
}
bool nas_proc_child(struct nas_proc_node directory,unsigned index,
                    char name[32],struct nas_proc_node *node) {
    static const char *const root[]={"self","meminfo","uptime","version","cpuinfo","stat","loadavg","vmstat"};
    static const char *const process[]={"stat","status","comm","cmdline","statm","task"};
    if(!name || !node)return false;
    if(directory.kind==NAS_PROC_ROOT) {
        if(index<sizeof(root)/sizeof(root[0])) {
            const char *s=root[index];unsigned i=0;
            do{name[i]=s[i];}while(s[i++]);
            char path[80]="/proc/";
            unsigned n=6;for(unsigned j=0;name[j];j++)path[n++]=name[j];
            path[n]=0;
            return nas_proc_lookup(path,node);
        }
        unsigned found=0;
        for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
            const struct nas_task *t=nas_task_at(i);
            if(!t || t->state==NAS_TASK_FREE || t->is_thread)continue;
            if(found++!=index-(sizeof(root)/sizeof(root[0])))continue;
            number_name(t->pid,name);
            *node=(struct nas_proc_node){NAS_PROC_PID_DIR,t->pid,0};
            return true;
        }
        return false;
    }
    if(directory.kind==NAS_PROC_TASK_DIR) {
        const struct nas_task *leader=living(directory.pid);
        if(!leader || leader->is_thread)return false;
        unsigned found=0;
        for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
            const struct nas_task *t=nas_task_at(i);
            if(!t || t->state==NAS_TASK_FREE || t->tgid!=directory.pid)
                continue;
            if(found++!=index)continue;
            number_name(t->pid,name);
            *node=(struct nas_proc_node){NAS_PROC_TID_DIR,t->pid,directory.pid};
            return true;
        }
        return false;
    }
    if(directory.kind!=NAS_PROC_PID_DIR && directory.kind!=NAS_PROC_TID_DIR)
        return false;
    const struct nas_task *task=living(directory.pid);
    if(!task || (directory.kind==NAS_PROC_TID_DIR &&
                 task->tgid!=directory.tgid))return false;
    unsigned count=directory.kind==NAS_PROC_PID_DIR ?
        sizeof(process)/sizeof(process[0]) :
        sizeof(process)/sizeof(process[0])-1;
    if(index>=count)return false;
    const char *s=process[index];unsigned i=0;
    do{name[i]=s[i];}while(s[i++]);
    uint8_t kind[]={NAS_PROC_PID_STAT,NAS_PROC_PID_STATUS,
                    NAS_PROC_PID_COMM,NAS_PROC_PID_CMDLINE,
                    NAS_PROC_PID_STATM,NAS_PROC_TASK_DIR};
    *node=(struct nas_proc_node){kind[index],directory.pid,
        directory.kind==NAS_PROC_TID_DIR?directory.tgid:
        (kind[index]==NAS_PROC_TASK_DIR?directory.pid:0)};
    return true;
}
/* Collect only the VM ranges tracked by the shared anonymous VMA ledger.
 * The kernel currently cannot report full ELF/stack resident PTE counts. */
static uint64_t anon_pages(const struct nas_task *t) {
    const struct nas_task *owner=t->vm_owner?t->vm_owner:t;
    uint64_t n=0;
    for(unsigned i=0;i<NAS_ANON_REGION_LIMIT;i++) {
        uint64_t start=owner->anonymous.slots[i].start;
        uint64_t end=owner->anonymous.slots[i].end;
        if(end>start)n+=(end-start)/4096ULL;
    }
    return n;
}
static void load_print(struct writer *w,uint64_t q16) {
    decimal(w,q16/65536ULL);ch(w,'.');
    unsigned cent=(unsigned)((q16%65536ULL)*100ULL/65536ULL);
    ch(w,(char)('0'+cent/10U));ch(w,(char)('0'+cent%10U));
}
int64_t nas_proc_read(struct nas_proc_node node,uint64_t offset,
                      uint8_t *output,unsigned size) {
    if(nas_proc_is_dir(node))return -EISDIR;
    if(size && !output)return -EFAULT;
    char data[LIMIT];struct writer w={data,0,LIMIT};
    const struct nas_task *t=node.pid ? living(node.pid) : 0;
    if(node.pid && !t)return -ENOENT;
    if(node.tgid && (!t || t->tgid!=node.tgid))return -ENOENT;
    switch(node.kind) {
    case NAS_PROC_MEMINFO:
        word(&w,"MemTotal:       ");decimal(&w,nas_managed_frames()*4U);word(&w," kB\n");
        word(&w,"MemFree:        ");decimal(&w,nas_free_frames()*4U);word(&w," kB\n");
        word(&w,"MemAvailable:   ");decimal(&w,nas_free_frames()*4U);word(&w," kB\n");
        word(&w,"Buffers:        0 kB\nCached:         0 kB\nSwapTotal:      0 kB\nSwapFree:       0 kB\n");
        break;
    case NAS_PROC_UPTIME: {
        uint64_t tns=nas_arch_monotonic_ns();
        decimal(&w,tns/NAS_NSEC_PER_SEC);ch(&w,'.');
        unsigned hundredths=(unsigned)((tns%NAS_NSEC_PER_SEC)/10000000U);
        ch(&w,(char)('0'+hundredths/10U));
        ch(&w,(char)('0'+hundredths%10U));
        word(&w," 0.00\n");break;
    }
    case NAS_PROC_STAT: {
        uint64_t busy=0,idle=0,switches=0;
        nas_task_cpu_account_snapshot(&busy,&idle,0,&switches);
        uint64_t bt=busy/10000000ULL,it=idle/10000000ULL;
        /* Monotonic single-CPU task occupancy, 100 ticks/sec. This is
         * coarse elapsed CPU time, not precise user/kernel cycle sampling. */
        for(unsigned row=0;row<2;row++) {
            word(&w,row?"cpu0  ":"cpu  ");
            word(&w,"0 0 ");decimal(&w,bt);ch(&w,' ');
            decimal(&w,it);word(&w," 0 0 0 0 0 0\n");
        }
        word(&w,"ctxt ");decimal(&w,switches);ch(&w,'\n');
        uint64_t boot_epoch=0;
        if(nas_time_boot_epoch_seconds(&boot_epoch)) {
            word(&w,"btime ");decimal(&w,boot_epoch);ch(&w,'\n');
        }
        unsigned run=0,blocked=0;
        for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
            const struct nas_task *t=nas_task_at(i);
            if(!t || t->state==NAS_TASK_FREE)continue;
            if(t->state==NAS_TASK_RUNNING || t->state==NAS_TASK_READY)run++;
            if(t->state==NAS_TASK_BLOCKED)blocked++;
        }
        word(&w,"procs_running ");decimal(&w,run);ch(&w,'\n');
        word(&w,"procs_blocked ");decimal(&w,blocked);ch(&w,'\n');
        break;
    }
    case NAS_PROC_LOADAVG: {
        uint64_t q16[3]={0};
        nas_task_cpu_account_snapshot(0,0,q16,0);
        for(unsigned i=0;i<3;i++){if(i)ch(&w,' ');load_print(&w,q16[i]);}
        unsigned run=0,total=0;uint64_t last=0;
        for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
            const struct nas_task *t=nas_task_at(i);
            if(!t || t->state==NAS_TASK_FREE)continue;
            if(t->state==NAS_TASK_READY || t->state==NAS_TASK_RUNNING)run++;
            total++;if(t->pid>last)last=t->pid;
        }
        ch(&w,' ');decimal(&w,run);ch(&w,'/');decimal(&w,total);
        ch(&w,' ');decimal(&w,last);ch(&w,'\n');break;
    }
    case NAS_PROC_VMSTAT:
        word(&w,"nr_free_pages ");decimal(&w,nas_free_frames());ch(&w,'\n');break;
    case NAS_PROC_VERSION: word(&w,"NaSeed experimental kernel\n");break;
    case NAS_PROC_CPUINFO:
#if defined(__x86_64__)
        word(&w,"processor\t: 0\narchitecture\t: x86_64\n");
#elif defined(__aarch64__)
        word(&w,"processor\t: 0\narchitecture\t: aarch64\n");
#else
        word(&w,"processor\t: 0\narchitecture\t: riscv64\n");
#endif
        break;
    case NAS_PROC_PID_COMM:
        word(&w,t->comm);ch(&w,'\n');break;
    case NAS_PROC_PID_CMDLINE:
        for(unsigned i=0;i<t->commandline_bytes && i<sizeof(t->commandline);i++)
            ch(&w,t->commandline[i]);
        break;
    case NAS_PROC_PID_STATM:
        decimal(&w,anon_pages(t)+NAS_USER_STACK_PAGES);
        word(&w," 0 0 0 0 0 0\n"); /* resident pages not measured */
        break;
    case NAS_PROC_PID_STATUS: {
        word(&w,"Name:\t");word(&w,t->comm);
        word(&w,"\nState:\t");ch(&w,t->state==NAS_TASK_ZOMBIE?'Z':
            t->state==NAS_TASK_STOPPED?'T':
            t->state==NAS_TASK_BLOCKED?'S':'R');
        word(&w,"\nTgid:\t");decimal(&w,t->tgid);
        word(&w,"\nPid:\t");decimal(&w,t->pid);
        word(&w,"\nPPid:\t");decimal(&w,t->ppid);
        word(&w,"\nUid:\t0\t0\t0\t0\nGid:\t0\t0\t0\t0\n");
        unsigned threads=0;
        for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
            const struct nas_task *other=nas_task_at(i);
            if(other && other->state!=NAS_TASK_FREE && other->tgid==t->tgid)threads++;
        }
        word(&w,"Threads:\t");decimal(&w,threads);ch(&w,'\n');
        break;
    }
    case NAS_PROC_PID_STAT: {
        decimal(&w,t->pid);word(&w," (");word(&w,t->comm);
        word(&w,") ");ch(&w,t->state==NAS_TASK_ZOMBIE?'Z':
            t->state==NAS_TASK_STOPPED?'T':
            t->state==NAS_TASK_BLOCKED?'S':'R');
        /* Untracked accounting fields remain zero, not fabricated CPU use. */
        for(unsigned field=4;field<=52;field++) {
            ch(&w,' ');
            if(field==4)decimal(&w,t->ppid);
            else if(field==5)decimal(&w,t->pgid);
            else if(field==6)decimal(&w,t->sid);
            else if(field==14)decimal(&w,t->cpu_runtime_ns/10000000ULL);
            else if(field==20) {
                unsigned threads=0;
                for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
                    const struct nas_task *other=nas_task_at(i);
                    if(other && other->state!=NAS_TASK_FREE && other->tgid==t->tgid)threads++;
                }
                decimal(&w,threads);
            }else if(field==23)decimal(&w,(anon_pages(t)+NAS_USER_STACK_PAGES)*4096ULL);
            else decimal(&w,0);
        }
        ch(&w,'\n');break;
    }
    default:return -ENOENT;
    }
    if(offset>=w.length)return 0;
    uint64_t n=w.length-offset;
    if(n>size)n=size;
    for(unsigned i=0;i<n;i++)output[i]=(uint8_t)data[offset+i];
    return (int64_t)n;
}
