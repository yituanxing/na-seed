/* Read-only process information filesystem: no copied task snapshot and no
 * userspace-address access. All three LP64 ports use the same provider. */
#include "procfs.h"
#include "../task.h"
#include "../mm/frame.h"
#include "../arch/arch.h"
#include "../time/clock.h"
#include "../task/user_stack.h"
#include "../mm/vm.h"
#include "../arch/user_pt.h"
#include "vfs.h"
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
    if(!strcmp(path,"/proc/mounts")) {out->kind=NAS_PROC_MOUNTS;return true;}
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
    if(!strcmp(p,"/fd")) {
        if(living(pid)->is_thread)return false;
        out->kind=NAS_PROC_FD_DIR;return true;
    }
    if(!strcmp(p,"/fdinfo")) {
        if(living(pid)->is_thread)return false;
        out->kind=NAS_PROC_FDINFO_DIR;return true;
    }
    if(!strncmp(p,"/fd/",4) || !strncmp(p,"/fdinfo/",8)) {
        const bool info=!strncmp(p,"/fdinfo/",8);
        const char *suffix=p+(info?8:4);
        uint64_t fd=0;
        if(*suffix=='0' && suffix[1]==0){fd=0;suffix++;}
        else if(!numeric(suffix,&fd,&suffix))return false;
        if(*suffix ||
           fd>=NAS_FD_LIMIT || !living(pid)->fds ||
           living(pid)->fds->entries[fd].kind==FD_CLOSED)return false;
        out->kind=info?NAS_PROC_FDINFO_FILE:NAS_PROC_FD_LINK;
        out->fd=(uint16_t)fd;return true;
    }
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
    else if(!strcmp(p,"/maps"))out->kind=NAS_PROC_PID_MAPS;
    else if(!strcmp(p,"/mounts"))out->kind=NAS_PROC_MOUNTS;
    else return false;
    return true;
}
bool nas_proc_node_from_ino(uint64_t ino,struct nas_proc_node *out) {
    if(!out)return false;
    struct nas_proc_node node={0};
    if(ino>=70000U && ino<80000U) {
        uint64_t kind=ino-70000U;
        switch(kind) {
        case NAS_PROC_ROOT:case NAS_PROC_MEMINFO:case NAS_PROC_UPTIME:
        case NAS_PROC_VERSION:case NAS_PROC_CPUINFO:case NAS_PROC_STAT:
        case NAS_PROC_LOADAVG:case NAS_PROC_VMSTAT:case NAS_PROC_MOUNTS:
            node.kind=(uint8_t)kind;break;
        default:return false;
        }
    } else if(ino>=80000U) {
        uint64_t encoded=ino-80000U,pid=encoded/2048U,tag=encoded%2048U;
        const struct nas_task *task=living(pid);
        if(!pid || !task)return false;
        node.pid=pid;
        /* FD 63 tags straddle 1024. Decode FD identities before
         * thread-group tags and validate against the actual FD table. */
        if(tag>=NAS_PROC_FD_LINK &&
           (tag-NAS_PROC_FD_LINK)%16U==0 &&
           (tag-NAS_PROC_FD_LINK)/16U<NAS_FD_LIMIT) {
            node.kind=NAS_PROC_FD_LINK;
            node.fd=(uint16_t)((tag-NAS_PROC_FD_LINK)/16U);
        } else if(tag>=NAS_PROC_FDINFO_FILE &&
                  (tag-NAS_PROC_FDINFO_FILE)%16U==0 &&
                  (tag-NAS_PROC_FDINFO_FILE)/16U<NAS_FD_LIMIT) {
            node.kind=NAS_PROC_FDINFO_FILE;
            node.fd=(uint16_t)((tag-NAS_PROC_FDINFO_FILE)/16U);
        } else if(tag>=1024U) {
            uint64_t kind=tag-1024U;
            switch(kind) {
            case NAS_PROC_TASK_DIR:
                if(task->is_thread)return false;
                node.kind=(uint8_t)kind;node.tgid=pid;break;
            case NAS_PROC_TID_DIR:
            case NAS_PROC_PID_STAT:case NAS_PROC_PID_STATUS:
            case NAS_PROC_PID_COMM:case NAS_PROC_PID_CMDLINE:
            case NAS_PROC_PID_STATM:
                node.kind=(uint8_t)kind;node.tgid=task->tgid;break;
            default:return false;
            }
        } else {
            switch(tag) {
            case NAS_PROC_PID_DIR:case NAS_PROC_PID_STAT:
            case NAS_PROC_PID_STATUS:case NAS_PROC_PID_COMM:
            case NAS_PROC_PID_CMDLINE:case NAS_PROC_PID_STATM:
            case NAS_PROC_TASK_DIR:case NAS_PROC_FD_DIR:
            case NAS_PROC_FDINFO_DIR:case NAS_PROC_PID_MAPS:
            case NAS_PROC_MOUNTS:
                node.kind=(uint8_t)tag;break;
            default:return false;
            }
        }
        if((node.kind==NAS_PROC_FD_LINK ||
            node.kind==NAS_PROC_FDINFO_FILE) &&
           (!task->fds ||
            task->fds->entries[node.fd].kind==FD_CLOSED))
            return false;
    } else return false;
    if(nas_proc_ino(node)!=ino)return false;
    *out=node;return true;
}
bool nas_proc_directory_from_ino(uint64_t dir_ino,
                                 struct nas_proc_node *out) {
    if(!out)return false;
    struct nas_proc_node candidate;
    if(!nas_proc_node_from_ino(dir_ino,&candidate) ||
       !nas_proc_is_dir(candidate))return false;
    *out=candidate;return true;
}
bool nas_proc_find_child(uint64_t dir_ino,const char *name,
                         struct nas_proc_node *out) {
    if(!name || !name[0] || !out || strchr(name,'/'))return false;
    struct nas_proc_node parent;
    if(!nas_proc_directory_from_ino(dir_ino,&parent))return false;
    unsigned limit=NAS_TASK_LIMIT+NAS_FD_LIMIT+16U;
    for(unsigned i=0;i<limit;i++) {
        char candidate[32];
        struct nas_proc_node child;
        if(!nas_proc_child(parent,i,candidate,&child))break;
        if(!strcmp(candidate,name)){*out=child;return true;}
    }
    return false;
}
bool nas_proc_fd_link_from_ino(uint64_t ino,struct nas_proc_node *out) {
    if(!out)return false;
    struct nas_proc_node candidate;
    if(!nas_proc_node_from_ino(ino,&candidate) ||
       candidate.kind!=NAS_PROC_FD_LINK)return false;
    *out=candidate;return true;
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
        if(node.kind==NAS_PROC_FD_DIR || node.kind==NAS_PROC_FDINFO_DIR) {
            const char *suffix=node.kind==NAS_PROC_FD_DIR?"/fd":"/fdinfo";
            for(unsigned i=0;suffix[i];i++)path[p++]=suffix[i];
        }
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
           node.kind==NAS_PROC_TASK_DIR || node.kind==NAS_PROC_TID_DIR ||
           node.kind==NAS_PROC_FD_DIR || node.kind==NAS_PROC_FDINFO_DIR;
}
uint32_t nas_proc_mode(struct nas_proc_node node) {
    if(node.kind==NAS_PROC_FD_LINK)return 0120777U;
    return nas_proc_is_dir(node) ? 0040555U : 0100444U;
}
uint64_t nas_proc_ino(struct nas_proc_node node) {
    /* Per-task inode identity is distinct from the process-level name;
     * retain stable inodes across dup/fork/readdir and TID traversal. */
    return node.pid ? 80000U+node.pid*2048U+
           (uint64_t)node.fd*16U+(uint64_t)node.kind +
           (node.tgid ? 1024U : 0U) : 70000U+(uint64_t)node.kind;
}
bool nas_proc_child(struct nas_proc_node directory,unsigned index,
                    char name[32],struct nas_proc_node *node) {
    static const char *const root[]={"self","meminfo","uptime","version","cpuinfo","stat","loadavg","vmstat",
                                    "mounts"};
    static const char *const process[]={"stat","status","comm","cmdline","statm","task","fd","fdinfo","maps","mounts"};
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
            *node=(struct nas_proc_node){NAS_PROC_PID_DIR,t->pid,0,0};
            return true;
        }
        return false;
    }
    if(directory.kind==NAS_PROC_FD_DIR ||
       directory.kind==NAS_PROC_FDINFO_DIR) {
        const struct nas_task *target=living(directory.pid);
        if(!target || !target->fds)return false;
        unsigned found=0;
        for(unsigned fd=0;fd<NAS_FD_LIMIT;fd++) {
            if(target->fds->entries[fd].kind==FD_CLOSED)continue;
            if(found++!=index)continue;
            number_name(fd,name);
            *node=(struct nas_proc_node){
                directory.kind==NAS_PROC_FD_DIR?NAS_PROC_FD_LINK:
                                                    NAS_PROC_FDINFO_FILE,
                directory.pid,0,(uint16_t)fd};
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
            *node=(struct nas_proc_node){NAS_PROC_TID_DIR,t->pid,directory.pid,0};
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
        5U; /* TID-local stat/status/comm/cmdline/statm only */
    if(index>=count)return false;
    const char *s=process[index];unsigned i=0;
    do{name[i]=s[i];}while(s[i++]);
    uint8_t kind[]={NAS_PROC_PID_STAT,NAS_PROC_PID_STATUS,
                    NAS_PROC_PID_COMM,NAS_PROC_PID_CMDLINE,
                    NAS_PROC_PID_STATM,NAS_PROC_TASK_DIR,
                    NAS_PROC_FD_DIR,NAS_PROC_FDINFO_DIR,
                    NAS_PROC_PID_MAPS,NAS_PROC_MOUNTS};
    *node=(struct nas_proc_node){kind[index],directory.pid,
        directory.kind==NAS_PROC_TID_DIR?directory.tgid:
        (kind[index]==NAS_PROC_TASK_DIR?directory.pid:0),0};
    return true;
}
/* Sum registered ELF/heap/stack/mmap VMAs, including uncommitted PROT_NONE.
 * The kernel currently cannot report full ELF/stack resident PTE counts. */
static uint64_t anon_pages(const struct nas_task *t) {
    const struct nas_mm *mm=t->mm;
    uint64_t n=0;
    for(unsigned i=0;i<NAS_ANON_REGION_LIMIT;i++) {
        uint64_t start=mm->mmap_vmas.slots[i].start;
        uint64_t end=mm->mmap_vmas.slots[i].end;
        if(end>start)n+=(end-start)/4096ULL;
    }
    return n;
}
static void load_print(struct writer *w,uint64_t q16) {
    decimal(w,q16/65536ULL);ch(w,'.');
    unsigned cent=(unsigned)((q16%65536ULL)*100ULL/65536ULL);
    ch(w,(char)('0'+cent/10U));ch(w,(char)('0'+cent%10U));
}
/* A procfd link follows the target process's *live* FD ownership.
 * Open-description identity is preserved by dup/fork, never copied into
 * independent per-process pathname records. A pseudo target does not
 * pretend to be a reopenable regular file. */
int64_t nas_proc_readlink(struct nas_proc_node node,char *out,unsigned cap) {
    if(node.kind!=NAS_PROC_FD_LINK)return -EINVAL;
    const struct nas_task *task=living(node.pid);
    if(!task || !task->fds || node.fd>=NAS_FD_LIMIT)return -ENOENT;
    struct nas_fd_entry e=task->fds->entries[node.fd];
    if(e.kind==FD_CLOSED || e.slot>=NAS_OPEN_FILE_LIMIT)return -ENOENT;
    const struct nas_open_file *f=&nas_open_files[e.slot];
    const char *path=f->origin_path;
    char temporary[64];
    if(!path[0]) {
        switch(e.kind) {
        case FD_UART: case FD_TTY: path="/dev/console";break;
        case FD_NULL:path="/dev/null";break;
        case FD_ZERO:path="/dev/zero";break;
        case FD_PIPE_R:case FD_PIPE_W:path="pipe:[";break;
        case FD_SOCKET:path="socket:[";break;
        case FD_EPOLL:path="anon_inode:[eventpoll]";break;
        case FD_EVENTFD:path="anon_inode:[eventfd]";break;
        case FD_PTY_MASTER:path="/dev/ptmx";break;
        case FD_PTY_SLAVE:path="/dev/pts/0";break;
        default:path="anon_inode:[unknown]";break;
        }
    }
    if(!strcmp(path,"pipe:[") || !strcmp(path,"socket:[")) {
        struct writer w={temporary,0,sizeof(temporary)-1};
        word(&w,path);decimal(&w,e.slot+1U);ch(&w,']');
        temporary[w.length]=0;path=temporary;
    }
    unsigned n=0;
    while(path[n] && n<cap){if(out)out[n]=path[n];n++;}
    return n;
}
/* Linux /proc/pid/maps reports logical VMA ranges, not only present
 * physical PTEs. A PROT_NONE reservation belongs here; COW/demand paging
 * will not change the output merely because pages are nonresident.
 * Every read(offset) traverses the selected Task's actual MM metadata. */
struct nas_map_stream {
    uint64_t skip,position,start,end;
    unsigned access,produced,cap;
    uint8_t *output;
    bool active;
};
static void map_ch(struct nas_map_stream *s,char c) {
    if(s->position>=s->skip && s->produced<s->cap)
        s->output[s->produced++]=(uint8_t)c;
    s->position++;
}
static void map_word(struct nas_map_stream *s,const char *p) {
    while(*p)map_ch(s,*p++);
}
static void map_hex(struct nas_map_stream *s,uint64_t n) {
    for(unsigned i=0;i<16;i++) {
        unsigned j=(unsigned)((n>>((15U-i)*4U))&15ULL);
        map_ch(s,(char)(j<10?'0'+j:'a'+j-10));
    }
}
static void map_flush(struct nas_map_stream *s) {
    if(!s->active)return;
    map_hex(s,s->start);map_ch(s,'-');map_hex(s,s->end);
    map_ch(s,' ');
    map_ch(s,(s->access&NAS_UP_READ)?'r':'-');
    map_ch(s,(s->access&NAS_UP_WRITE)?'w':'-');
    map_ch(s,(s->access&NAS_UP_EXEC)?'x':'-');
    map_word(s,"p 00000000 00:00 0\n");
    s->active=false;
}
static bool map_visit_vma(const struct nas_anon_region *vma,void *ctx) {
    struct nas_map_stream *s=(struct nas_map_stream*)ctx;
    map_flush(s);
    s->start=vma->start;
    s->end=vma->end;
    s->access=vma->prot;
    s->active=true;
    return true;
}
static int64_t proc_maps_read(const struct nas_task *task,uint64_t offset,
                              uint8_t *output,unsigned capacity) {
    struct nas_map_stream s={.skip=offset,.output=output,.cap=capacity};
    if(!nas_vm_walk_task_vmas(task,map_visit_vma,&s))return -EIO;
    map_flush(&s);
    return (int64_t)s.produced;
}
/* /proc/mounts is a view of the live VFS mount registry, never a second
 * filesystem-specific list. In particular a real ext4/GPT mount can appear
 * after procfs initialization and a detached mount must disappear. Stream
 * by byte offset: the VFS supports more mounts than the fixed-size buffer
 * used by the small /proc scalar files, and short reads must not truncate
 * the list at that buffer's boundary. */
struct nas_mount_stream {
    uint64_t skip,position;
    unsigned produced,capacity;
    uint8_t *output;
};
static void mount_ch(struct nas_mount_stream *s,char c) {
    if(s->position>=s->skip && s->produced<s->capacity)
        s->output[s->produced++]=(uint8_t)c;
    ++s->position;
}
static void mount_word(struct nas_mount_stream *s,const char *word) {
    while(*word)mount_ch(s,*word++);
}
/* Linux /proc/mounts (fstab/mntent) escapes whitespace and backslash in
 * every pathname-like field. Do not emit malformed extra columns when a
 * user-created mountpoint contains such characters. */
static void mount_field(struct nas_mount_stream *s,const char *field) {
    for(;*field;field++) {
        switch(*field) {
        case ' ':mount_word(s,"\\040");break;
        case '\t':mount_word(s,"\\011");break;
        case '\n':mount_word(s,"\\012");break;
        case '\\':mount_word(s,"\\134");break;
        default:mount_ch(s,*field);break;
        }
    }
}
static int64_t proc_mounts_read(uint64_t offset,uint8_t *output,unsigned size) {
    if(!size)return 0;
    struct nas_mount_stream stream={
        .skip=offset,.output=output,.capacity=size
    };
    for(unsigned ordinal=0;;ordinal++) {
        struct nas_vfs_mount_snapshot mount;
        int rc=nas_vfs_mount_snapshot(ordinal,&mount);
        if(rc==-ENOENT)return (int64_t)stream.produced;
        if(rc)return stream.produced ? (int64_t)stream.produced : rc;
        mount_field(&stream,mount.source);mount_ch(&stream,' ');
        mount_field(&stream,mount.target);mount_ch(&stream,' ');
        mount_field(&stream,mount.fstype);
        mount_word(&stream,mount.read_only?" ro 0 0\n":" rw 0 0\n");
        if(stream.produced==size)return (int64_t)size;
    }
}
int64_t nas_proc_read(struct nas_proc_node node,uint64_t offset,
                      uint8_t *output,unsigned size) {
    if(nas_proc_is_dir(node))return -EISDIR;
    if(size && !output)return -EFAULT;
    const struct nas_task *t=node.pid ? living(node.pid) : 0;
    if(node.pid && !t)return -ENOENT;
    if(node.tgid && (!t || t->tgid!=node.tgid))return -ENOENT;
    if(node.kind==NAS_PROC_PID_MAPS)
        return proc_maps_read(t,offset,output,size);
    if(node.kind==NAS_PROC_MOUNTS)
        return proc_mounts_read(offset,output,size);
    char data[LIMIT];struct writer w={data,0,LIMIT};
    if(node.kind==NAS_PROC_FD_LINK) return -EINVAL;
    if(node.kind==NAS_PROC_FDINFO_FILE) {
        if(!t || !t->fds || node.fd>=NAS_FD_LIMIT ||
           t->fds->entries[node.fd].kind==FD_CLOSED)return -ENOENT;
        unsigned slot=t->fds->entries[node.fd].slot;
        word(&w,"pos:\t");decimal(&w,nas_open_files[slot].position);ch(&w,'\n');
        word(&w,"flags:\t");
        uint64_t flags=nas_open_files[slot].status_flags |
                       (t->fds->entries[node.fd].cloexec?02000000ULL:0ULL);
        char digits[25];unsigned count=0;
        do {digits[count++]=(char)('0'+(flags&7U));flags>>=3;}while(flags && count<sizeof(digits));
        while(count)ch(&w,digits[--count]);
        ch(&w,'\n');
        word(&w,"mnt_id:\t0\nino:\t");
        decimal(&w,slot+1U);ch(&w,'\n');
        goto done;
    }
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
        decimal(&w,anon_pages(t));
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
done:
    if(offset>=w.length)return 0;
    uint64_t n=w.length-offset;
    if(n>size)n=size;
    for(unsigned i=0;i<n;i++)output[i]=(uint8_t)data[offset+i];
    return (int64_t)n;
}
