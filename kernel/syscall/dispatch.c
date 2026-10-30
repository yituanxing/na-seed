#include <stdint.h>
#include <stdbool.h>
#include "dispatch.h"
#include "../syscall/ops.h"
#include "../arch/linux_syscall_abi.h"
#include "../errno.h"
#include "../random.h"
#include "../task.h"
#include "../fd.h"
#include "../arch/arch.h"
#include "../arch/abi.h"
#include "../mm/vm.h"
#include "../mm/frame.h"
#include "../mm/usercopy.h"
#include "../task/exec.h"
#include "../fs/mode.h"
#include "../task/process.h"
#include "../task/ptrace.h"
#include "../task/wait_pid.h"
#include "../task/session.h"
#include "../task/sched.h"
#include "../task/cpu_affinity.h"
#include "../time/clock.h"
#include "../task/signal.h"
#include "../task/user_stack.h"
#include "../fs/io.h"
#include "../fs/syscall.h"
#include "../fs/epoll.h"
#include "../platform/platform.h"
#include "../drivers/serial.h"
#include "../drivers/tty.h"
#include "../panic.h"
#define TASK (*nas_current_task)
#define thread_clear_tid TASK.thread_clear_tid

/* One Linux timespec64 ABI on three LP64 ISAs.
 * CLOCK_MONOTONIC remains independent of the boot RTC. CLOCK_REALTIME
 * requires a validated boot epoch plus the same hardware clocksource. */
static uint64_t nas_time_read_request(uint64_t addr,uint64_t *ns) {
    uint64_t sec,nsec;
    if(!addr || !nas_user_read_u64(addr,&sec) ||
       addr>UINT64_MAX-8 || !nas_user_read_u64(addr+8,&nsec))
        return (uint64_t)-EFAULT;
    return nas_time_duration_ns(sec,nsec,ns) ? 0 : (uint64_t)-EINVAL;
}
/* Linux provides coarse realtime/monotonic views (IDs 5 and 6).
 * glibc time() on AArch64 uses CLOCK_REALTIME_COARSE, discovered by
 * comparing the unchanged Debian BusyBox date applet with Linux QEMU-user.
 * Return an actual timekeeper reading rounded DOWN to 10ms, not a guessed
 * clock value; report the tick-derived coarse clock resolution accurately.
 */
static uint64_t nas_time_read_clock(uint64_t clockid,uint64_t dst,bool resolution) {
    const bool coarse=clockid==5ULL || clockid==6ULL;
    const bool realtime=clockid==0ULL || clockid==5ULL;
    uint64_t now;
    if(clockid==1ULL || clockid==6ULL) {
        now=resolution ? nas_arch_clock_resolution_ns() : nas_arch_monotonic_ns();
    } else if(realtime) {
        if(!nas_time_realtime_available())return (uint64_t)-EOPNOTSUPP;
        if(resolution)now=nas_arch_clock_resolution_ns();
        else if(!nas_time_realtime_ns(&now))return (uint64_t)-EOPNOTSUPP;
    } else return (uint64_t)-EINVAL;
    if(coarse) {
        const uint64_t period=10000000ULL; /* shared 100Hz clockevent */
        now=resolution ? period : now - now%period;
    }
    if(!dst && resolution)return 0; /* clock_getres accepts NULL */
    struct { int64_t sec,nsec; } ts={
        (int64_t)(now/NAS_NSEC_PER_SEC),
        (int64_t)(now%NAS_NSEC_PER_SEC)
    };
    return nas_user_write_bytes(dst,(const uint8_t *)&ts,sizeof(ts))
        ? 0 : (uint64_t)-EFAULT;
}
/* Obsolete timezone output follows Linux's UTC zero-value convention.
 * Avoid returning invented timestamps when firmware omits the boot RTC. */
static uint64_t nas_time_gettimeofday(uint64_t tv,uint64_t tz) {
    uint64_t now;
    if(!nas_time_realtime_ns(&now))return (uint64_t)-EOPNOTSUPP;
    if(tv) {
        struct { int64_t sec,usec; } value={
            (int64_t)(now/NAS_NSEC_PER_SEC),
            (int64_t)((now%NAS_NSEC_PER_SEC)/1000ULL)
        };
        if(!nas_user_write_bytes(tv,(const uint8_t *)&value,sizeof(value)))
            return (uint64_t)-EFAULT;
    }
    if(tz) {
        struct { int32_t minuteswest,dsttime; } utc={0,0};
        if(!nas_user_write_bytes(tz,(const uint8_t *)&utc,sizeof(utc)))
            return (uint64_t)-EFAULT;
    }
    return 0;
}
/* Provider-owned completion. A parked syscall's original C stack resumes
 * here; no syscall-number switch, replay, or device policy in scheduler. */
static uint64_t finish_sleep(struct nas_task *task) {
    (void)task;
    return 0; /* expired monotonic deadline, signal checked by wait adapter */
}
static uint64_t finish_io(struct nas_task *task) {
    /* The resumed Task owns the same File and imported iovecs as the
     * original syscall. Its fd number may now designate another object. */
    return nas_fs_io_resume(task->wait.pending_io);
}
static uint64_t finish_epoll(struct nas_task *task) {
    const uint64_t *a=task->wait.blocked_args;
    return nas_epoll_wait(a[0],a[1],a[2],a[3]);
}
static uint64_t finish_poll(struct nas_task *task) {
    const uint64_t *a=task->wait.blocked_args;
    return nas_fs_poll(a[0],a[1],a[2]);
}
static uint64_t finish_ppoll(struct nas_task *task) {
    const uint64_t *a=task->wait.blocked_args;
    return nas_fs_ppoll(a[0],a[1],a[2],a[3]);
}
static uint64_t finish_select(struct nas_task *task) {
    const uint64_t *a=task->wait.blocked_args;
    return nas_fs_pselect6(a[0],a[1],a[2],a[3],false);
}
static nas_wait_finish_fn finish_poll_provider(enum nas_syscall_op nr) {
    if(nr==NAS_SC_poll) return finish_poll;
    if(nr==NAS_SC_ppoll) return finish_ppoll;
    if(nr==NAS_SC_select || nr==NAS_SC_pselect6) return finish_select;
    return 0;
}
static uint64_t finish_wait4(struct nas_task *task) {
    const uint64_t *a=task->wait.blocked_args;
    return nas_process_wait4(a[0],a[1],a[2],a[3]);
}
static uint64_t finish_sigsuspend(struct nas_task *task) {
    (void)task;
    return (uint64_t)-EINTR;
}

static uint64_t nas_time_sleep_syscall(uint64_t *frame,enum nas_syscall_op nr,
                                      const uint64_t *args) {
    uint64_t request=nr==NAS_SC_nanosleep ? args[1] : args[3];
    uint64_t duration,rc=nas_time_read_request(request,&duration);
    if((int64_t)rc<0)return rc;
    uint64_t now=nas_arch_monotonic_ns(), deadline;
    if(nr==NAS_SC_clock_nanosleep) {
        const uint64_t clockid=args[1], flags=args[2];
        if(clockid!=0ULL && clockid!=1ULL)return (uint64_t)-EINVAL;
        if(flags&~1ULL)return (uint64_t)-EINVAL; /* TIMER_ABSTIME */
        if((flags&1ULL) && clockid==0ULL) {
            uint64_t wall;
            if(!nas_time_realtime_at(now,&wall))
                return (uint64_t)-EOPNOTSUPP;
            deadline=nas_time_add_deadline(now,duration>wall?duration-wall:0);
        } else
            deadline=(flags&1ULL) ? duration : nas_time_add_deadline(now,duration);
    } else deadline=nas_time_add_deadline(now,duration);
    if(nas_time_deadline_expired(now,deadline))return 0;
    TASK.wait.deadline_ns=deadline;
    TASK.wait.deadline_active=true;
    return nas_task_block(frame,nr,args[1],args[2],args[3],args[4],finish_sleep);
}
static uint64_t nas_time_poll_arm_deadline(enum nas_syscall_op nr,
                                           const uint64_t *args) {
    uint64_t delta=0;
    if(nr==NAS_SC_select) {
        /* Legacy x86 select timeval uses usec, pselect6 uses nsec. */
        if(args[5]){
            uint64_t sec,usec;
            if(!nas_user_read_u64(args[5],&sec) ||
               args[5]>UINT64_MAX-8 ||
               !nas_user_read_u64(args[5]+8,&usec))
                return (uint64_t)-EFAULT;
            if((int64_t)sec<0 || usec>=1000000ULL ||
               sec>UINT64_MAX/NAS_NSEC_PER_SEC)return (uint64_t)-EINVAL;
            delta=sec*NAS_NSEC_PER_SEC+usec*1000ULL;
        }
    } else if(nr==NAS_SC_poll) {
        int32_t ms=(int32_t)(uint32_t)args[3];
        if(ms>0)delta=(uint64_t)(unsigned)ms*1000000ULL;
    } else {
        /* pselect6's timespec is its fifth argument (Linux a5);
         * ppoll uses its third. Reuse one monotonic scheduler deadline. */
        uint64_t timespec=nr==NAS_SC_pselect6?args[5]:args[3];
        if(timespec) {
            uint64_t rc=nas_time_read_request(timespec,&delta);
            if((int64_t)rc<0)return rc;
        }
    }
    if(delta) {
        TASK.wait.deadline_ns=nas_time_add_deadline(nas_arch_monotonic_ns(),delta);
        TASK.wait.deadline_active=true;
    }
    return 0;
}


/* Linux LP64 struct sysinfo, shared by x86-64, RV64 and AArch64.
 * Report frame allocator accounting rather than invented guest RAM.
 * No swap is provided, and kernel load averages are not yet sampled.
 * mem_unit=4096 keeps physical counts representable without overflow. */
struct nas_linux_sysinfo {
    int64_t uptime;
    uint64_t loads[3];
    uint64_t totalram, freeram, sharedram, bufferram;
    uint64_t totalswap, freeswap;
    uint16_t procs, pad;
    uint64_t totalhigh, freehigh;
    uint32_t mem_unit;
};
_Static_assert(sizeof(struct nas_linux_sysinfo)==112,
               "Linux LP64 sysinfo ABI size");
static uint64_t nas_sysinfo(uint64_t addr) {
    struct nas_linux_sysinfo info={0};
    info.uptime=(int64_t)(nas_arch_monotonic_ns()/NAS_NSEC_PER_SEC);
    info.totalram=nas_managed_frames();
    info.freeram=nas_free_frames();
    info.mem_unit=4096U;
    for (unsigned i=0;i<NAS_TASK_LIMIT;i++) {
        const struct nas_task *t=nas_task_at(i);
        if (t && t->state!=NAS_TASK_FREE) info.procs++;
    }
    return nas_user_write_bytes(addr,(const uint8_t *)&info,sizeof(info))
        ? 0 : (uint64_t)-EFAULT;
}

static uint32_t missing_count[512];
static uint64_t sys_uname_naseed(uint64_t buffer) {
    struct uts {
        char sysname[65], node[65], release[65], version[65];
        char machine[65], domain[65];
    };
    static const struct uts data = {
        "Linux", "naseed", "0.1", "NaSeed experimental",
        NAS_UTS_MACHINE, "localdomain"
    };
    return nas_user_write_bytes(buffer, (const uint8_t *)&data, sizeof(data))
        ? 0 : (uint64_t)-EFAULT;
}


static uint64_t task_pipe_io(uint64_t *frame, uint64_t nr,
                             uint64_t fd, uint64_t buf, uint64_t count) {
    struct nas_io_attempt attempt;
    nas_fs_io_start(nr,fd,buf,count,&attempt);
    uint64_t ret=attempt.result;
    if ((int64_t)ret == -EPIPE &&
        (nr == NAS_SC_write || nr == NAS_SC_writev) &&
        nas_sched_sigpipe_default(attempt.pipe_like)) {
        nas_sched_child_exit(0, NAS_SIGPIPE);
        return nas_task_schedule(frame);
    }
    if ((int64_t)ret == -EPIPE &&
        (nr == NAS_SC_write || nr == NAS_SC_writev))
        nas_signal_broken_pipe(attempt.pipe_like);
    if(attempt.pending) {
        /* Ownership transfers exactly once to this Task. All later
         * readiness and retries use this OpenFile, never the fd table. */
        TASK.wait.pending_io=attempt.pending;
        TASK.wait.pending_io_release=nas_fs_io_dispose;
        TASK.wait.tty_requested_bytes=attempt.tty_requested_bytes;
        if(nas_fs_io_tty_read(attempt.pending))
            nas_tty_blocking_read_start(&TASK);
        return nas_task_block(frame,nr,fd,buf,count,0,finish_io);
    }
    return ret;
}

/* Both ISA-normalized call arguments and the live hardware user trap belong
 * to the current task until the kernel scheduler switches. ptrace must
 * park BEFORE entering the real syscall, then observe the ACTUAL result. */
uint64_t nas_ptrace_syscall_dispatch(uint64_t *args) {
    struct nas_task *original=nas_current_task;
    /* A stop inside a syscall that started untraced (notably TRACEME's
     * raise(SIGSTOP)) may cause the tracer to enable PTRACE_SYSCALL before
     * this SAME native C continuation returns. Do not manufacture an exit
     * stop without a corresponding entry event: it shifts strace's syscall
     * entry/exit pairing for the rest of the trace. */
    const bool trace_at_entry=original && original->ptrace_trace_syscalls &&
                              original->ptrace_tracer_pid;
    if(nas_ptrace_syscall_enter(original,args)) {
        /* Native continuation resumes the SAME syscall C stack. An entry
         * trace stop must re-enter userspace at the saved, rewound opcode
         * so tracer-modified registers/nr are actually decoded by hardware.
         * The one-shot skip-entry guard prevents a duplicate ptrace stop. */
        (void)nas_task_schedule(args);
        nas_arch_load_user_frame(original,args);
        return nas_arch_saved_return(original);
    }
    uint64_t result=syscall_dispatch(args);
    /* ptrace fork/exec event logic has already emitted its required
     * syscall-exit stop. The suspended native C continuation now returns
     * through this original gate: do not manufacture a second exit stop. */
    bool event_exit_published=original->ptrace_event_exit_published;
    original->ptrace_event_exit_published=false;
    if(trace_at_entry && !event_exit_published &&
       nas_current_task==original && original->state==NAS_TASK_RUNNING &&
       nas_ptrace_syscall_exit(original,args,result)) {
        /* Exit stop resumes after the actual syscall, preserving any GPR
         * changes made by the tracer while retaining this original frame. */
        (void)nas_task_schedule(args);
        nas_arch_load_user_frame(original,args);
        return nas_arch_saved_return(original);
    }
    return result;
}

/* Strict single-program ABI diagnostic dispatcher, for a real syscall gate. */
#if defined(__x86_64__)
extern uint64_t nas_x86_syscall_return_rip;
/* Bounded read-only diagnostic of exact native syscall arguments.
 * The tracer PID can crash after receiving an invalid ptrace/wait result;
 * record which native syscall was actually entered, not a guessed Linux ABI.
 * No guest synthetic success data is injected. */
struct nas_x86_call_history {
    uint64_t pid,nr,arg1,arg2,arg3,entry_pc;
};
static struct nas_x86_call_history nas_x86_call_ring[64];
static unsigned nas_x86_call_next;
void nas_x86_debug_recent_syscalls(uint64_t pid) {
    const unsigned end=nas_x86_call_next;
    unsigned shown=0;
    for(unsigned i=0;i<64 && shown<16;i++) {
        const struct nas_x86_call_history *c=
            &nas_x86_call_ring[(end-1U-i)%64U];
        if(c->pid!=pid)continue;
        nas_print("[X86_CALL] pid=");nas_dec(c->pid);
        nas_print(" nr=");nas_dec(c->nr);
        nas_print(" arg1=");nas_hex(c->arg1);
        nas_print(" arg2=");nas_hex(c->arg2);
        nas_print(" arg3=");nas_hex(c->arg3);
        nas_print(" pc=");nas_hex(c->entry_pc);nas_print("\n");
        shown++;
    }
}
#endif
uint64_t syscall_dispatch(const uint64_t *args) {
#if defined(__x86_64__)
    struct nas_x86_call_history *h=&nas_x86_call_ring[nas_x86_call_next++%64U];
    h->pid=nas_current_task?nas_current_task->pid:0;
    h->nr=args[0];h->arg1=args[1];h->arg2=args[2];h->arg3=args[3];
    h->entry_pc=nas_x86_syscall_return_rip;
#endif
    nas_task_cpu_account_switch(nas_current_task);
    const uint64_t raw_nr = args[0];
    enum nas_syscall_op nr = nas_linux_decode_call(args);
#if defined(__riscv) && __riscv_xlen == 64
    /* Native BusyBox ash may invoke clone with Linux flags other than
     * SIGCHLD. Do not pretend this is fork: show the actual ABI contract
     * before implementing vfork/clone semantics in the existing task core. */
    if (raw_nr == 220U && nr == NAS_SC_NONE) {
        nas_print("[RV64 CLONE] unsupported flags=");
        nas_hex(args[1]);
        nas_print(" child_stack="); nas_hex(args[2]);
        nas_print(" parent_tid="); nas_hex(args[3]);
        nas_print(" tls="); nas_hex(args[4]);
        nas_print(" child_tid="); nas_hex(args[5]);
        nas_print("\n");
    }
#endif
    /* Real-program execution trace: syscall identity, callsite and args.
     * Capped to avoid serial storms; not needed in exception context.
     */
#ifdef NASEED_SYSCALL_TRACE
    static unsigned trace_count, uring_focus;
    /* After a generic missing io_uring_setup request, capture a bounded
     * sequence of *all* Linux syscalls to identify real libuv fallback
     * I/O waits. Compiled only with the explicit trace flag. */
    if(raw_nr==425)uring_focus=1; /* fresh bounded trace per attempt */
    bool diagnostic=trace_count++<220 ||
        (uring_focus && uring_focus++<1000);
    if (diagnostic) {
        nas_print("[TRACE] pid="); nas_dec(TASK.pid);
        nas_print(" linux_nr="); nas_dec(raw_nr);
        nas_print(" decoded="); nas_dec(nr);
        nas_print(" rip="); nas_hex(args[7]);
        nas_print(" rsp="); nas_hex(nas_arch_signal_user_sp());
        nas_print(" a1="); nas_hex(args[1]);
        nas_print(" a2="); nas_hex(args[2]);
        nas_print(" a3="); nas_hex(args[3]);
        nas_print("\n");
    }
#endif
    if (nr == NAS_SC_ptrace) return nas_ptrace_request(args[1],args[2],args[3],args[4]);
    if (nr == NAS_SC_arch_prctl) return nas_linux_arch_prctl(args[1], args[2]);
    if (nr == NAS_SC_getrandom) return nas_random_getrandom(args[1],args[2],args[3]);
    if (nr == NAS_SC_set_tid_address) { thread_clear_tid = args[1]; return TASK.pid; }
    if (nr == NAS_SC_getpid) return TASK.tgid;
    if (nr == NAS_SC_gettid) return TASK.pid;
    if(nr==NAS_SC_sched_getaffinity || nr==NAS_SC_sched_setaffinity) {
        uint64_t pid=args[1] ? args[1] : TASK.pid;
        if(!nas_task_by_pid(pid))return (uint64_t)-ESRCH;
        if(!nas_cpu_affinity_valid_size(args[2]))return (uint64_t)-EINVAL;
        if(nr==NAS_SC_sched_getaffinity) {
            uint64_t online=NAS_CPU_AFFINITY_MASK;
            return nas_user_write_bytes(args[3],(const uint8_t *)&online,
                                        sizeof(online)) ?
                   (uint64_t)sizeof(online) : (uint64_t)-EFAULT;
        }
        uint64_t requested=0;
        if(!nas_user_read_u64(args[3],&requested))return (uint64_t)-EFAULT;
        return nas_cpu_affinity_allowed(requested) ? 0 : (uint64_t)-EINVAL;
    }
    if (nr == NAS_SC_getppid) return TASK.ppid;
    if (nr == NAS_SC_getrlimit)
        return nas_process_prlimit(0,args[1],0,args[2]);
    if (nr == NAS_SC_prlimit64)
        return nas_process_prlimit(args[1],args[2],args[3],args[4]);
    if (nr == NAS_SC_prctl)
        return nas_process_prctl(args[1],args[2]);
    if(nr==NAS_SC_clock_gettime)
        return nas_time_read_clock(args[1],args[2],false);
    if(nr==NAS_SC_clock_getres)
        return nas_time_read_clock(args[1],args[2],true);
    if(nr==NAS_SC_timer_create)return nas_time_timer_create(args[1],args[2],args[3]);
    if(nr==NAS_SC_timer_settime)return nas_time_timer_settime(args[1],args[2],args[3],args[4]);
    if(nr==NAS_SC_timer_gettime)return nas_time_timer_gettime(args[1],args[2]);
    if(nr==NAS_SC_timer_getoverrun)return nas_time_timer_getoverrun(args[1]);
    if(nr==NAS_SC_timer_delete)return nas_time_timer_delete(args[1]);
    if(nr==NAS_SC_gettimeofday)
        return nas_time_gettimeofday(args[1],args[2]);
    if(nr==NAS_SC_nanosleep || nr==NAS_SC_clock_nanosleep)
        return nas_time_sleep_syscall((uint64_t *)(uintptr_t)args,nr,args);
    if (nr == NAS_SC_getuid || nr == NAS_SC_getgid || nr == NAS_SC_geteuid || nr == NAS_SC_getegid) return 0; /* root credentials */
    if (nr == NAS_SC_mprotect) {
        uint64_t rc=nas_vm_mprotect(args[1], args[2], args[3]);
#ifdef NASEED_SYSCALL_TRACE
        if ((int64_t)rc<0) {
            nas_print("[VM REJECT] mprotect errno=");nas_dec((uint64_t)(-(int64_t)rc));
            nas_print(" addr=");nas_hex(args[1]);nas_print(" len=");nas_hex(args[2]);
            nas_print(" prot=");nas_hex(args[3]);nas_print("\n");
        }
#endif
        return rc;
    }
    if (nr == NAS_SC_mmap) {
        uint64_t result=(args[4] & 0x20ULL) ?
            nas_vm_anon_mmap(args[1],args[2],args[3],
                             args[4],args[5],args[6]) :
            nas_vm_file_mmap(args[1],args[2],args[3],
                             args[4],args[5],args[6]);
#ifdef NASEED_SYSCALL_TRACE
        if ((int64_t)result<0) {
            nas_print("[VM REJECT] mmap errno="); nas_dec((uint64_t)(-(int64_t)result));
            nas_print(" hint="); nas_hex(args[1]);
            nas_print(" len="); nas_hex(args[2]);
            nas_print(" prot="); nas_hex(args[3]);
            nas_print(" flags="); nas_hex(args[4]);
            nas_print(" fd="); nas_hex(args[5]);
            nas_print(" offset="); nas_hex(args[6]); nas_print("\n");
        }
#endif
        return result;
    }
    if (nr == NAS_SC_munmap) return nas_vm_munmap(args[1], args[2]);
    if (nr == NAS_SC_brk) {
        uint64_t result=nas_vm_brk(args[1]);
#ifdef NASEED_SYSCALL_TRACE
        if (args[1] && args[1]!=result) {
            nas_print("[VM REJECT] brk wanted="); nas_hex(args[1]);
            nas_print(" retained="); nas_hex(result); nas_print("\n");
        }
#endif
        return result;
    }
    if(nr==NAS_SC_epoll_wait || nr==NAS_SC_epoll_pwait) {
        /* Linux epoll_pwait's optional signal mask is installed only for
         * the wait, using the same common mechanism as ppoll/pselect6. */
        if(nr==NAS_SC_epoll_pwait && args[5]) {
            if(args[6]!=8ULL)return (uint64_t)-EINVAL;
            uint64_t mask;
            if(!nas_user_read_u64(args[5],&mask))return (uint64_t)-EFAULT;
            TASK.ppoll_saved_mask=TASK.blocked_signals;
            TASK.ppoll_mask_active=true;
            TASK.blocked_signals=mask & ~((1ULL<<8)|(1ULL<<18));
        }
        uint64_t result=nas_epoll_wait(args[1],args[2],args[3],args[4]);
        if((int64_t)result==-EAGAIN) {
            if(nas_signal_user_pending(&TASK)) {
                nas_signal_ppoll_restore(&TASK,true);
                return (uint64_t)-EINTR;
            }
            int32_t ms=(int32_t)(uint32_t)args[4];
            if(ms>0 && !TASK.wait.deadline_active) {
                TASK.wait.deadline_ns=nas_time_add_deadline(
                    nas_arch_monotonic_ns(),(uint64_t)(unsigned)ms*1000000ULL);
                TASK.wait.deadline_active=true;
            }
            nas_epoll_arm_current_task(args[1]);
            return nas_task_block((uint64_t *)(uintptr_t)args,nr,
                                  args[1],args[2],args[3],args[4],finish_epoll);
        }
        nas_signal_ppoll_restore(&TASK,false);
        return result;
    }
    if(nr==NAS_SC_poll || nr==NAS_SC_ppoll || nr==NAS_SC_pselect6 || nr==NAS_SC_select) {
        /* ppoll and pselect6 install one optional temporary signal mask.
         * pselect6's sixth argument points to {sigset_ptr, size_t}; the
         * syscall number and ABI argument locations are target-decoded. */
        uint64_t sigmask=0, sigsize=0;
        if(nr==NAS_SC_ppoll) {
            sigmask=args[4];sigsize=args[5];
        } else if(nr==NAS_SC_pselect6 && args[6]) {
            if(!nas_user_read_u64(args[6],&sigmask) ||
               args[6]>UINT64_MAX-8 ||
               !nas_user_read_u64(args[6]+8,&sigsize))
                return (uint64_t)-EFAULT;
        }
        if(sigmask) {
            if(sigsize!=8ULL)return (uint64_t)-EINVAL;
            uint64_t mask;
            if(!nas_user_read_u64(sigmask,&mask))
                return (uint64_t)-EFAULT;
            TASK.ppoll_saved_mask=TASK.blocked_signals;
            TASK.ppoll_mask_active=true;
            TASK.blocked_signals=mask & ~((1ULL<<8)|(1ULL<<18));
        }
        bool immediate=false;
        if(nr==NAS_SC_select && args[5]){
            uint64_t sec,usec;
            if(!nas_user_read_u64(args[5],&sec) ||
               args[5]>UINT64_MAX-8 ||
               !nas_user_read_u64(args[5]+8,&usec)){
                nas_signal_ppoll_restore(&TASK,false);return (uint64_t)-EFAULT;
            }
            if((int64_t)sec<0 || usec>=1000000ULL){
                nas_signal_ppoll_restore(&TASK,false);return (uint64_t)-EINVAL;
            }
            immediate=!sec && !usec;
        }
        if(nr==NAS_SC_pselect6 && args[5]) {
            uint64_t sec=0,nsec=0;
            if(!nas_user_read_u64(args[5],&sec) ||
               args[5]>UINT64_MAX-8 ||
               !nas_user_read_u64(args[5]+8,&nsec)) {
                nas_signal_ppoll_restore(&TASK,false);
                return (uint64_t)-EFAULT;
            }
            uint64_t duration;
            if(!nas_time_duration_ns(sec,nsec,&duration)) {
                nas_signal_ppoll_restore(&TASK,false);
                return (uint64_t)-EINVAL;
            }
            immediate=duration==0;
        }
        uint64_t result=nr==NAS_SC_poll ?
            nas_fs_poll(args[1],args[2],args[3]) :
            nr==NAS_SC_ppoll ?
            nas_fs_ppoll(args[1],args[2],args[3],args[4]) :
            nas_fs_pselect6(args[1],args[2],args[3],args[4],immediate);
        if((int64_t)result==-EAGAIN) {
            if(nas_signal_user_pending(&TASK)) {
                nas_signal_ppoll_restore(&TASK,true);
                return (uint64_t)-EINTR;
            }
            uint64_t rc=nas_time_poll_arm_deadline(nr,args);
            if((int64_t)rc<0) {
                nas_signal_ppoll_restore(&TASK,false);
                return rc;
            }
            bool has_io=(nr==NAS_SC_pselect6 || nr==NAS_SC_select) ?
                nas_fs_pselect_arm(args[1],args[2],args[3],args[4]) :
                nas_fs_poll_arm(args[1],args[2]);
            if(!has_io && !TASK.wait.deadline_active) {
                nas_signal_ppoll_restore(&TASK,false);
                return (uint64_t)-EOPNOTSUPP;
            }
            return nas_task_block((uint64_t *)(uintptr_t)args,nr,
                                  args[1],args[2],args[3],args[4],finish_poll_provider(nr));
        }
        nas_signal_ppoll_restore(&TASK,false);
        return result;
    }
    /* True Linux controlling-terminal job policy is independent from
     * physical COM1/OpenSBI/PL011 transport. Use the common Signal/Task
     * machinery to stop a background PGID, never fake successful I/O.
     * Raw diagnostic FD_UART remains deliberately outside job control. */
    if((nr==NAS_SC_read || nr==NAS_SC_readv ||
        nr==NAS_SC_write || nr==NAS_SC_writev ||
        (nr==NAS_SC_ioctl && args[2]==0x5410ULL)) &&
       nas_tty_background_fd(args[1])) {
        unsigned sig=(nr==NAS_SC_read || nr==NAS_SC_readv)
                     ? NAS_SIGTTIN : NAS_SIGTTOU;
        if(sig==NAS_SIGTTIN || nr==NAS_SC_ioctl || nas_tty_tostop()) {
            bool handled=false;
            uint64_t result=nas_signal_background_job(sig,
                            (uint64_t *)(uintptr_t)args,&handled);
            if(handled)return result;
        }
    }
    if (nr == NAS_SC_read) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3]);
    if (nr == NAS_SC_readv) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3]);
    if (nr == NAS_SC_write) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3]);
    if (nr == NAS_SC_writev) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3]);
    struct nas_fs_result fs_result = nas_fs_syscall(nr, args);
    if (fs_result.handled) return fs_result.value;
    if (nr == NAS_SC_rt_sigaction) return nas_signal_action(args[1], args[2], args[3], args[4]);
    if (nr == NAS_SC_rt_sigpending) return nas_signal_pending(args[1], args[2]);
    if (nr == NAS_SC_rt_sigreturn) return nas_signal_sigreturn((uint64_t *)(uintptr_t)args);
    if (nr == NAS_SC_rt_sigsuspend) {
        /* Temporary, atomic replacement of the caller's mask while asleep.
         * The existing Task wait/replay + signal trampoline own wakeup;
         * never return success, never poll or fake a child-exit event.
         * Reuse the one temporary-mask owner also used by ppoll/pselect6. */
        if (args[2]!=8ULL) return (uint64_t)-EINVAL;
        uint64_t mask=0;
        if (!nas_user_read_u64(args[1],&mask)) return (uint64_t)-EFAULT;
        if(TASK.ppoll_mask_active)return (uint64_t)-EINVAL;
        TASK.ppoll_saved_mask=TASK.blocked_signals;
        TASK.ppoll_mask_active=true;
        TASK.blocked_signals=mask & ~((1ULL<<8)|(1ULL<<18));
        if(nas_signal_user_pending(&TASK)) {
            nas_signal_ppoll_restore(&TASK,true);
            return (uint64_t)-EINTR;
        }
        return nas_task_block((uint64_t *)(uintptr_t)args,nr,0,0,0,0,
                               finish_sigsuspend);
    }
    if (nr == NAS_SC_rt_sigprocmask) return nas_signal_procmask(args[1], args[2], args[3], args[4],
                                                                (uint64_t *)(uintptr_t)args);
    if (nr == NAS_SC_kill) return nas_signal_kill(args[1], args[2], (uint64_t *)(uintptr_t)args);
    if (nr == NAS_SC_tkill) return nas_signal_tkill(args[1],args[2],(uint64_t *)(uintptr_t)args);
    if (nr == NAS_SC_tgkill) return nas_signal_tgkill(args[1],args[2],args[3],(uint64_t *)(uintptr_t)args);
    if (nr == NAS_SC_umask) {
        uint64_t previous=TASK.fs->umask;
        TASK.fs->umask=nas_fs_umask(args[1]);
        return previous;
    }
    if (nr == NAS_SC_uname) return sys_uname_naseed(args[1]);
    if (nr == NAS_SC_sysinfo) return nas_sysinfo(args[1]);

    if (nr == NAS_SC_futex)
        return nas_process_futex((uint64_t *)(uintptr_t)args,args[1],args[2],
                                 args[3],args[4],args[5],args[6]);
    if (nr == NAS_SC_set_robust_list)
        return nas_process_set_robust_list(args[1],args[2]);
    if (nr == NAS_SC_get_robust_list)
        return nas_process_get_robust_list(args[1],args[2],args[3]);
    if (nr == NAS_SC_clone_thread)
        return nas_process_clone_thread((uint64_t *)(uintptr_t)args,
                                        args[2],args[3],
                                        nas_linux_clone_child_tid(args),
                                        nas_linux_clone_tls(args));
    if (nr == NAS_SC_vfork)
        return nas_process_vfork((uint64_t *)(uintptr_t)args,
                                  nas_linux_vfork_child_stack(args));
    if (nr == NAS_SC_fork)
        return nas_process_fork((uint64_t *)(uintptr_t)args);
    if (nr == NAS_SC_fork_child_tid)
        return nas_process_fork_child_tid((uint64_t *)(uintptr_t)args,
                                          nas_linux_clone_child_tid(args));

    if (nr == NAS_SC_wait4) {
        uint64_t ret = nas_process_wait4(args[1], args[2], args[3], args[4]);
        /* WUNTRACED/WCONTINUED are real blocking wait selectors too.
         * Do not treat them as unsupported when a selected child has
         * not reached a reportable event yet. Keep WNOHANG nonblocking. */
        if ((int64_t)ret == -EOPNOTSUPP && args[4] == 0 &&
            !(args[3] & NAS_WAIT_NOHANG))
            return nas_task_block((uint64_t *)(uintptr_t)args, nr,
                              args[1], args[2], args[3], args[4],finish_wait4);
        return ret;
    }
    if (nr == NAS_SC_execve) {
        uint64_t result=nas_task_execve(args[1],args[2],args[3],
                                        (uint64_t *)(uintptr_t)args);
        if ((int64_t)result < 0) {
            static unsigned exec_fail_logs;
            if (exec_fail_logs++ < 12) {
                nas_print("[EXEC] Linux execve rejected errno=");
                nas_dec((uint64_t)(-(int64_t)result));
                nas_print("\n");
            }
        }
        return result;
    }
    if (nr == NAS_SC_getpgid) return nas_session_getpgid(args[1]);
    if (nr == NAS_SC_getpgrp) return TASK.pgid;
    if (nr == NAS_SC_getsid) return nas_session_getsid(args[1]);
    if (nr == NAS_SC_setsid) return nas_session_setsid();
    if (nr == NAS_SC_setpgid) return nas_session_setpgid(args[1],args[2]);
    if (nr == NAS_SC_exit || nr == NAS_SC_exit_group) { /* exit / exit_group */
        /* PID1 is a process, not a syscall special case: all exits now
         * pass through Task/FD/group ownership cleanup. Its final group
         * completion is observed by the boot-policy owner in init/main.c. */
        if (TASK.tgid!=1 &&
            nas_ptrace_exit_event(nas_current_task,args,args[1]))
            return nas_task_schedule((uint64_t *)(uintptr_t)args);
        if(nr==NAS_SC_exit_group)
            nas_sched_thread_group_exit((uint32_t)args[1],0);
        else
            nas_sched_child_exit((uint32_t)args[1],0);
        return nas_task_schedule((uint64_t *)(uintptr_t)args);
    }
    if (raw_nr >= sizeof(missing_count) / sizeof(missing_count[0]) ||
        ++missing_count[raw_nr] <= 3) {
        nas_print("[MISS] syscall nr="); nas_dec(raw_nr); nas_print("\n");
    }
    return (uint64_t)-ENOSYS;
}
