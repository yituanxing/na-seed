#include <stdint.h>
#include <stdbool.h>
#include "dispatch.h"
#include "../ipc/sysv.h"
#include "../syscall/ops.h"
#include "poll_wait_policy.h"
#include "../arch/linux_syscall_abi.h"
#if defined(__riscv) && __riscv_xlen == 64
#include "../arch/riscv64/hwprobe.h"
#endif
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
#include "../task/credentials.h"
#include "../task/capability.h"
#include "../task/ptrace.h"
#include "../task/wait_pid.h"
#include "../task/session.h"
#include "../task/sched.h"
#include "../task/wait.h"
#include "../task/cpu_affinity.h"
#include "../time/clock.h"
#include "../task/signal.h"
#include "../task/user_stack.h"
#include "../init/main.h"
#include "reboot_policy.h"
#include "../fs/io.h"
#include "../fs/syscall.h"
#include "../fs/user_broker.h"
#include "../fs/socket_syscall.h"
#include "../fs/memfd.h"
#include "../../ports/lwip/socket_port.h"
#include "../fs/epoll.h"
#include "../platform/platform.h"
#include "../drivers/serial.h"
#include "../drivers/tty.h"
#include "../panic.h"
#include "../uts.h"
#define TASK (*nas_current_task)
#define thread_clear_tid TASK.thread_clear_tid

/* Linux gid_t is 32 bits on all our LP64 target ABIs.  Snapshot a complete
 * bounded user group list BEFORE changing Task credentials; EFAULT, EPERM
 * and EINVAL must leave the previous vector untouched. */
static uint64_t nas_sys_setgroups(uint64_t count,uint64_t addr) {
#ifdef NAS_ENABLE_M253_NONROOT_DAC
    if(TASK.credentials.euid!=0)return (uint64_t)-EPERM;
#endif
    if(count>NAS_CREDENTIAL_GROUP_LIMIT)return (uint64_t)-EINVAL;
    uint32_t groups[NAS_CREDENTIAL_GROUP_LIMIT];
    if(count && !nas_user_read_bytes(addr,(uint8_t *)groups,
                                     count*sizeof(groups[0])))
        return (uint64_t)-EFAULT;
    return nas_credentials_setgroups(&TASK.credentials,count,groups);
}
static uint64_t nas_sys_getgroups(uint64_t count,uint64_t addr) {
    const struct nas_credentials *cred=&TASK.credentials;
    uint32_t groups[NAS_CREDENTIAL_GROUP_LIMIT];
    uint64_t actual=nas_credentials_getgroups(cred,count,groups);
    if((int64_t)actual<0)return actual;
    if(actual && count &&
       !nas_user_write_bytes(addr,(const uint8_t *)groups,
                             actual*sizeof(groups[0])))
        return (uint64_t)-EFAULT;
    return actual;
}
/* Linux uid_t/gid_t pointers are 32-bit on every supported LP64 ABI.
 * Copy all three genuine task IDs, preserving existing root-only behavior
 * when M253 is disabled. */
static uint64_t nas_sys_getresid(uint64_t real,uint64_t effective,
                                 uint64_t saved,uint32_t r,uint32_t e,uint32_t s) {
    if(!nas_user_write_bytes(real,(const uint8_t *)&r,sizeof(r)) ||
       !nas_user_write_bytes(effective,(const uint8_t *)&e,sizeof(e)) ||
       !nas_user_write_bytes(saved,(const uint8_t *)&s,sizeof(s)))
        return (uint64_t)-EFAULT;
    return 0;
}

/* Raw Linux capget/capset ABI. Linux first probes ONLY the 32-bit version;
 * unknown versions publish v3 back to the caller, including capget(NULL)
 * version probing. Do not read the pid or data until the version is valid.
 * Capability masks reflect and change Task credentials, and currently
 * represent only the enforced CAP_NET_RAW privilege. */
static uint64_t nas_sys_capabilities(bool set,uint64_t hdrp,uint64_t datap) {
    uint32_t version;
    if(!nas_user_read_bytes(hdrp,(uint8_t *)&version,sizeof(version)))
        return (uint64_t)-EFAULT;
    const unsigned words=nas_cap_version_words(version);
    if(!words) {
        uint64_t result=nas_cap_negotiate_version(&version,set,!datap);
        if(!nas_user_write_bytes(hdrp,(const uint8_t *)&version,sizeof(version)))
            return (uint64_t)-EFAULT;
        return result;
    }
    if(!set && !datap)return 0;

    int32_t pid;
    if(!nas_user_read_bytes(hdrp+sizeof(version),(uint8_t *)&pid,sizeof(pid)))
        return (uint64_t)-EFAULT;
    uint64_t valid_target=nas_cap_validate_target(set,pid,(uint32_t)TASK.pid);
    if((int64_t)valid_target<0)return valid_target;
    if(set) {
        struct nas_cap_user_data requested[2]={{0},{0}};
        if(!nas_user_read_bytes(datap,(uint8_t *)requested,
                                words*sizeof(requested[0])))
            return (uint64_t)-EFAULT;
        return nas_cap_set(&TASK.credentials,words,requested);
    }
    struct nas_task *target=pid ? nas_task_by_pid((uint32_t)pid) : &TASK;
    if(!target || target->state==NAS_TASK_ZOMBIE)return (uint64_t)-ESRCH;
#ifdef NAS_ENABLE_M253_NONROOT_DAC
    /* Cross-process capabilities are sensitive process credentials. */
    if(target!=&TASK && TASK.credentials.euid!=0)return (uint64_t)-EPERM;
#endif
    struct nas_cap_user_data observed[2];
    nas_cap_get(&target->credentials,observed);
    return nas_user_write_bytes(datap,(const uint8_t *)observed,
                                words*sizeof(observed[0])) ? 0 : (uint64_t)-EFAULT;
}

/* Linux sched_attr v0 (48 bytes) plus utilization clamps (56 bytes).
 * Read-only export of the scheduler's actual SCHED_OTHER-only policy. */
struct nas_linux_sched_attr {
    uint32_t size,policy;
    uint64_t flags;
    int32_t nice;
    uint32_t priority;
    uint64_t runtime,deadline,period;
    uint32_t util_min,util_max;
};
_Static_assert(sizeof(struct nas_linux_sched_attr)==56,
               "Linux sched_attr LP64 ABI size");

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
static uint64_t finish_netlink_receive(struct nas_task *task) {
    return nas_fs_netlink_receive_resume(task->wait.netlink_receive);
}
static uint64_t finish_socket_accept(struct nas_task *task) {
    const uint64_t *a=task->wait.blocked_args;
    return nas_fs_socket_accept_description(task->wait.description_slot,
                                             a[1],a[2],a[3]);
}
/* A control-plane Netconn request belongs to the held OpenFile, not to an
 * integer FD that another CLONE_FILES sibling could close and recycle. */
static uint64_t finish_socket_control(struct nas_task *task) {
    return nas_fs_socket_control_description(task->wait.description_slot);
}
/* TCP_INFO completion belongs to the held OpenFile. The original user
 * address/optlen are resolved only by the resumed Task, never scheduler. */
static uint64_t finish_socket_tcp_info(struct nas_task *task) {
    const uint64_t *a=task->wait.blocked_args;
    return nas_fs_socket_tcp_info_description(task->wait.description_slot,
                                               a[3],a[4]);
}
static uint64_t finish_socket_name(struct nas_task *task) {
    const uint64_t *a=task->wait.blocked_args;
    return nas_fs_socket_name_description(task->wait.description_slot,
                                           a[1],a[2],
                                           task->wait.blocked_op==NAS_SC_getpeername);
}
static uint64_t finish_epoll(struct nas_task *task) {
    const uint64_t *a=task->wait.blocked_args;
    return nas_epoll_wait_description(task->wait.description_slot,
                                      a[1],a[2],a[3]);
}
static uint64_t finish_poll(struct nas_task *task) {
    return nas_fs_poll_resume(task);
}
static uint64_t finish_ppoll(struct nas_task *task) {
    return nas_fs_poll_resume(task);
}
static uint64_t finish_select(struct nas_task *task) {
    return nas_fs_pselect_resume(task);
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
static uint64_t finish_waitid(struct nas_task *task) {
    const uint64_t *a=task->wait.blocked_args;
    return nas_process_waitid(a[0],a[1],a[2],a[3],a[4]);
}
static uint64_t finish_sigsuspend(struct nas_task *task) {
    (void)task;
    return (uint64_t)-EINTR;
}
static uint64_t consume_sigtimedwait(struct nas_task *task,uint64_t mask,
                                     uint64_t info) {
    uint64_t result=nas_signal_timedwait_consume(mask,info);
    /* PID1 may request a platform shutdown only after the original init
     * process ACTUALLY consumed its masked signal; a mere kill(1) success
     * is not proof that init ran its own shutdown state machine. Cover
     * both immediate and scheduler-resumed sigtimedwait paths. */
    if(task->tgid==1 && (result==NAS_SIGUSR1 ||
                         result==NAS_SIGUSR2 || result==NAS_SIGTERM))
        nas_init_note_pid1_shutdown_consumed((unsigned)result);
    return result;
}
static uint64_t finish_sigtimedwait(struct nas_task *task) {
    const uint64_t *a=task->wait.blocked_args;
    /* A matching signal takes priority if it arrived at the deadline.
     * Timeout without a matching pending signal is EAGAIN, not EINTR. */
    return consume_sigtimedwait(task,a[0],a[1]);
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
static uint64_t sys_sethostname_naseed(uint64_t addr,uint64_t length) {
    if(length>NAS_UTS_NODENAME_MAX)return (uint64_t)-EINVAL;
    char hostname[NAS_UTS_NODENAME_MAX+1]={0};
    if(length && !nas_user_read_bytes(addr,(uint8_t *)hostname,length))
        return (uint64_t)-EFAULT;
    /* Only the root primary credential is currently representable. */
    return nas_uts_set_hostname(hostname,(size_t)length)
        ? 0 : (uint64_t)-EINVAL;
}
static uint64_t sys_uname_naseed(uint64_t buffer) {
    struct uts {
        char sysname[65], node[65], release[65], version[65];
        char machine[65], domain[65];
    };
    struct uts data = {
        "Linux", "", "0.1", "NaSeed experimental",
        NAS_UTS_MACHINE, "localdomain"
    };
    nas_uts_copy_hostname(data.node);
    return nas_user_write_bytes(buffer, (const uint8_t *)&data, sizeof(data))
        ? 0 : (uint64_t)-EFAULT;
}


static uint64_t task_pipe_io(uint64_t *frame, uint64_t nr,
                             uint64_t fd, uint64_t buf, uint64_t count,
                             bool suppress_sigpipe) {
    struct nas_io_attempt attempt;
    nas_fs_io_start(nr,fd,buf,count,&attempt);
    uint64_t ret=attempt.result;
    if ((int64_t)ret == -EPIPE &&
        (nr == NAS_SC_write || nr == NAS_SC_writev) &&
        !suppress_sigpipe && nas_sched_sigpipe_default(attempt.pipe_like)) {
        nas_sched_child_exit(0, NAS_SIGPIPE);
        return nas_task_schedule(frame);
    }
    if ((int64_t)ret == -EPIPE &&
        (nr == NAS_SC_write || nr == NAS_SC_writev) && !suppress_sigpipe)
        nas_signal_broken_pipe(attempt.pipe_like);
    if(attempt.pending) {
        /* Ownership transfers exactly once to this Task. All later
         * readiness and retries use this OpenFile, never the fd table. */
        TASK.wait.pending_io=attempt.pending;
        TASK.wait.pending_io_release=nas_fs_io_dispose;
        TASK.wait.suppress_sigpipe=suppress_sigpipe;
        TASK.wait.tty_requested_bytes=attempt.tty_requested_bytes;
        nas_fs_io_read_wait_start(attempt.pending,&TASK);
        return nas_task_block(frame,nr,fd,buf,count,0,finish_io);
    }
    return ret;
}
/* Tinyproxy and other POSIX daemons call send(MSG_NOSIGNAL), not write().
 * The internal TCP FileOps EOPNOTSUPP means "blocking TX still in flight";
 * it must reach the same held-description Task wait used by write(), while
 * MSG_NOSIGNAL suppresses SIGPIPE without changing the shared File flags.
 * Do not use this path for UDP, addressed sendto or any other MSG flags. */
static bool native_tcp_send_nosignal_wait(enum nas_syscall_op nr,
                                         const uint64_t *args) {
    if(nr!=NAS_SC_sendto || args[4]!=0x4000ULL || args[5] || args[6] ||
       args[1]>=NAS_FD_LIMIT ||
       TASK.fds->entries[args[1]].kind!=FD_SOCKET)return false;
    unsigned slot=TASK.fds->entries[args[1]].slot;
    return nas_lwip_socket_is_slot(slot) &&
           nas_lwip_socket_type(slot)==NAS_LWIP_BRIDGE_TCP;
}
static uint64_t task_socket_accept(uint64_t *frame,enum nas_syscall_op nr,
                                   const uint64_t *args,uint64_t result) {
    if((int64_t)result!=-EAGAIN)return result;
    const uint64_t fd=args[1];
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind!=FD_SOCKET)
        return result;
    unsigned slot=TASK.fds->entries[fd].slot;
    if(slot>=NAS_OPEN_FILE_LIMIT ||
       (nas_open_files[slot].status_flags&0x800ULL)) {
        /* No asynchronous lease may remain owned by a nonblocking Task
         * that returns to userspace without parking its completion. */
        if(TASK.wait.accept_lease_cancel)nas_task_wait_dispose(&TASK);
        return result; /* SOCK_NONBLOCK must observe EAGAIN */
    }
    if(!nas_fd_hold_description(slot)) {
        if(TASK.wait.accept_lease_cancel)nas_task_wait_dispose(&TASK);
        return (uint64_t)-EBADF;
    }
    TASK.wait.description_slot=slot;
    TASK.wait.description_release=nas_fs_drop_kernel_description;
    nas_wait_io_clear(&TASK);
    nas_wait_io_arm(&TASK,slot,0x0001U,false);
    return nas_task_block(frame,nr,args[1],args[2],args[3],
                          nr==NAS_SC_accept4?args[4]:0,finish_socket_accept);
}
static uint64_t task_socket_control(uint64_t *frame,enum nas_syscall_op nr,
                                    const uint64_t *args,uint64_t result) {
    if((int64_t)result!=-EAGAIN)return result;
    unsigned fd=(unsigned)args[1];
    if(args[1]>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind!=FD_SOCKET)
        return (uint64_t)-EBADF;
    unsigned slot=TASK.fds->entries[fd].slot;
    if(!nas_lwip_socket_is_slot(slot))return result;
    if(!nas_fd_hold_description(slot))return (uint64_t)-EBADF;
    /* Reuse the established pinned-socket wait record; its release hook
     * tears down the pin on success, caught signal and Task termination. */
    TASK.wait.description_slot=slot;
    TASK.wait.description_release=nas_fs_drop_kernel_description;
    if(nr==NAS_SC_getsockopt)
        return nas_task_block5(frame,nr,args[1],args[2],args[3],
                               args[4],args[5],finish_socket_tcp_info);
    return nas_task_block(frame,nr,args[1],args[2],args[3],0,
                          nr==NAS_SC_getsockname || nr==NAS_SC_getpeername ?
                          finish_socket_name:finish_socket_control);
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
#ifdef NASEED_SYSCALL_TRACE
    /* M110 diagnostic: log the actual rejected Linux clone ABI on all three
     * ISAs, without broadening accepted flags or changing the -ENOSYS path.
     * Eight attempts suffice to compare original Go and pthread callers. */
    static unsigned rejected_clone_count;
    if (raw_nr == nas_linux_raw_clone_nr() && nr == NAS_SC_NONE &&
        rejected_clone_count++ < 8U) {
        nas_print("[CLONE REJECT] pid="); nas_dec(nas_current_task->pid);
        nas_print(" flags=");
        nas_hex(args[1]);
        nas_print(" stack="); nas_hex(args[2]);
        nas_print(" parent_tid="); nas_hex(args[3]);
        nas_print(" arg4="); nas_hex(args[4]);
        nas_print(" arg5="); nas_hex(args[5]);
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
#if defined(__riscv) && __riscv_xlen == 64
    if (nr == NAS_SC_riscv_hwprobe)
        return nas_riscv_hwprobe(args[1],args[2],args[3],args[4],args[5]);
#endif
    if (nr == NAS_SC_getrandom) return nas_random_getrandom(args[1],args[2],args[3]);
    if (nr == NAS_SC_set_tid_address) { thread_clear_tid = args[1]; return TASK.pid; }
    if (nr == NAS_SC_getpid) return TASK.tgid;
    if (nr == NAS_SC_gettid) return TASK.pid;
    if (nr == NAS_SC_capget) return nas_sys_capabilities(false,args[1],args[2]);
    if (nr == NAS_SC_capset) return nas_sys_capabilities(true,args[1],args[2]);
    if(nr==NAS_SC_shmget)return nas_sysvipc_call(NAS_SYSVIPC_SHM,0,args[1],args[2],args[3],0);
    if(nr==NAS_SC_shmctl)return nas_sysvipc_call(NAS_SYSVIPC_SHM,1,args[1],args[2],args[3],0);
    if(nr==NAS_SC_shmat || nr==NAS_SC_shmdt)return (uint64_t)-ENOSYS;
    if(nr==NAS_SC_semget)return nas_sysvipc_call(NAS_SYSVIPC_SEM,0,args[1],args[2],args[3],0);
    if(nr==NAS_SC_semctl)return nas_sysvipc_call(NAS_SYSVIPC_SEM,1,args[1],args[2],args[3],args[4]);
    if(nr==NAS_SC_semop)return (uint64_t)-ENOSYS;
    if(nr==NAS_SC_msgget)return nas_sysvipc_call(NAS_SYSVIPC_MSG,0,args[1],args[2],0,0);
    if(nr==NAS_SC_msgctl)return nas_sysvipc_call(NAS_SYSVIPC_MSG,1,args[1],args[2],args[3],0);
    if(nr==NAS_SC_msgsnd || nr==NAS_SC_msgrcv)return (uint64_t)-ENOSYS;
    if(nr==NAS_SC_ioprio_set || nr==NAS_SC_ioprio_get) {
        /* Linux IOPRIO_WHO_PROCESS: task-specific priority, inherited across
         * fork and retained across exec. Other scope selectors require group
         * fanout/aggregation and are deliberately not advertised yet. */
        if(!args[1] || args[1]>3ULL)return (uint64_t)-EINVAL;
        if(args[1]!=1ULL)return (uint64_t)-EOPNOTSUPP;
        struct nas_task *target=args[2] ? nas_task_by_pid(args[2]) : &TASK;
        if(!target || target->state==NAS_TASK_ZOMBIE)
            return (uint64_t)-ESRCH;
        if(nr==NAS_SC_ioprio_get)return target->ioprio;
        const uint64_t value=args[3];
        const uint64_t cls=value>>13;
        const uint64_t level=value&0x1fffULL;
        if(value>0xffffULL || cls>3ULL ||
           (cls==0 && level!=0) || (cls!=0 && level>7))
            return (uint64_t)-EINVAL;
        target->ioprio=(uint16_t)value;
        return 0;
    }
    if(nr==NAS_SC_sched_getscheduler || nr==NAS_SC_sched_getparam ||
       nr==NAS_SC_sched_getattr) {
        /* NaSeed presently schedules every task in its one non-realtime
         * Linux-compatible policy: SCHED_OTHER, static priority zero.
         * Queries validate the target task and user output independently. */
        uint64_t pid=args[1] ? args[1] : TASK.pid;
        if(!nas_task_by_pid(pid))return (uint64_t)-ESRCH;
        if(nr==NAS_SC_sched_getscheduler)return 0; /* SCHED_OTHER */
        if(nr==NAS_SC_sched_getattr) {
            uint64_t size=args[3];
            if(args[4] || size<48 || size>sizeof(struct nas_linux_sched_attr))
                return (uint64_t)-EINVAL;
            const struct nas_linux_sched_attr policy={
                .size=(uint32_t)sizeof(struct nas_linux_sched_attr),
                .policy=0,.flags=0,.nice=0,.priority=0,
                .runtime=0,.deadline=0,.period=0,.util_min=0,.util_max=0
            };
            return nas_user_write_bytes(args[2],(const uint8_t *)&policy,
                                        size) ? 0 : (uint64_t)-EFAULT;
        }
        const uint32_t param=0; /* struct sched_param.sched_priority */
        return nas_user_write_bytes(args[2],(const uint8_t *)&param,
                                    sizeof(param)) ? 0 : (uint64_t)-EFAULT;
    }
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
    if(nr==NAS_SC_getitimer)return nas_time_getitimer(args[1],args[2]);
    if(nr==NAS_SC_setitimer)return nas_time_setitimer(args[1],args[2],args[3]);
    if(nr==NAS_SC_gettimeofday)
        return nas_time_gettimeofday(args[1],args[2]);
    if(nr==NAS_SC_nanosleep || nr==NAS_SC_clock_nanosleep)
        return nas_time_sleep_syscall((uint64_t *)(uintptr_t)args,nr,args);
#ifdef NAS_ENABLE_M253_NONROOT_DAC
    if (nr == NAS_SC_getuid) return TASK.credentials.ruid;
    if (nr == NAS_SC_geteuid) return TASK.credentials.euid;
    if (nr == NAS_SC_getgid) return TASK.credentials.rgid;
    if (nr == NAS_SC_getegid) return TASK.credentials.egid;
    if (nr == NAS_SC_getresuid)
        return nas_sys_getresid(args[1],args[2],args[3],
                               TASK.credentials.ruid,TASK.credentials.euid,TASK.credentials.suid);
    if (nr == NAS_SC_getresgid)
        return nas_sys_getresid(args[1],args[2],args[3],
                               TASK.credentials.rgid,TASK.credentials.egid,TASK.credentials.sgid);
    if (nr == NAS_SC_setuid) return nas_cred_setuid(&TASK.credentials,args[1]);
    if (nr == NAS_SC_setgid) return nas_cred_setgid(&TASK.credentials,args[1]);
    if (nr == NAS_SC_setreuid) return nas_cred_setreuid(&TASK.credentials,args[1],args[2]);
    if (nr == NAS_SC_setregid) return nas_cred_setregid(&TASK.credentials,args[1],args[2]);
    if (nr == NAS_SC_setresuid)
        return nas_cred_setresuid(&TASK.credentials,args[1],args[2],args[3]);
    if (nr == NAS_SC_setresgid)
        return nas_cred_setresgid(&TASK.credentials,args[1],args[2],args[3]);
    if (nr == NAS_SC_setfsuid) return nas_cred_setfsuid(&TASK.credentials,args[1]);
    if (nr == NAS_SC_setfsgid) return nas_cred_setfsgid(&TASK.credentials,args[1]);
#else
    if (nr == NAS_SC_getuid || nr == NAS_SC_getgid || nr == NAS_SC_geteuid || nr == NAS_SC_getegid) return 0; /* legacy root */
    if (nr == NAS_SC_getresuid || nr == NAS_SC_getresgid)
        return nas_sys_getresid(args[1],args[2],args[3],0,0,0);
    if (nr == NAS_SC_setuid || nr == NAS_SC_setgid)
        return nas_credentials_set_id(args[1]);
    if (nr == NAS_SC_setreuid || nr == NAS_SC_setregid)
        return nas_credentials_set_root_ids(args[1],args[2],UINT32_MAX);
    if (nr == NAS_SC_setresuid || nr == NAS_SC_setresgid)
        return nas_credentials_set_root_ids(args[1],args[2],args[3]);
    if (nr == NAS_SC_setfsuid || nr == NAS_SC_setfsgid)
        return nas_credentials_set_fs_id(args[1]);
#endif
    if (nr == NAS_SC_getgroups) return nas_sys_getgroups(args[1],args[2]);
    if (nr == NAS_SC_setgroups)
        return nas_sys_setgroups(args[1],args[2]);
    if (nr == NAS_SC_mincore)
        return nas_vm_mincore(args[1],args[2],args[3]);
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
    if (nr == NAS_SC_msync)
        return nas_vm_msync(args[1],args[2],args[3]);
    if (nr == NAS_SC_madvise)
        return nas_vm_madvise(args[1],args[2],args[3]);
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
            unsigned instance;
            int held=nas_epoll_hold_instance(args[1],&instance);
            if(held) {
                nas_signal_ppoll_restore(&TASK,false);
                TASK.wait.deadline_active=false;
                return (uint64_t)(int64_t)held;
            }
            TASK.wait.description_slot=instance;
            TASK.wait.description_release=nas_fs_drop_kernel_description;
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
            int armed=(nr==NAS_SC_pselect6 || nr==NAS_SC_select) ?
                nas_fs_pselect_arm(args[1],args[2],args[3],args[4]) :
                nas_fs_poll_arm(args[1],args[2]);
            if(armed<0) {
                nas_signal_ppoll_restore(&TASK,false);
                TASK.wait.deadline_active=false;
                return (uint64_t)(int64_t)armed;
            }
            if(!armed && !TASK.wait.deadline_active &&
               !nas_poll_signal_only_infinite(nr,args)) {
                nas_task_wait_dispose(&TASK);
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
    /* M180: only a genuine worker_io VFS owner enters this branch. Linux
     * usercopy/FD reservation stays on the original Task continuation;
     * upstream lwext4 executes on one dedicated parkable kernel worker. */
    struct nas_fs_user_broker_answer fs_broker=nas_fs_user_broker_enter(
        (uint64_t *)(uintptr_t)args,nr,args);
    if(fs_broker.handled)return fs_broker.value;
    if (nr == NAS_SC_read) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3],false);
    if (nr == NAS_SC_readv) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3],false);
    if (nr == NAS_SC_write) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3],false);
    if (nr == NAS_SC_writev) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3],false);
    /* MSG_PEEK must preserve the same shared socket RX buffer. Route the
     * pure no-sockaddr form through the existing held-File blocking read
     * mechanism, with the nonconsuming callback on the same FileOps. */
    if(nr==NAS_SC_recvfrom && args[4]==0x2ULL &&
       !args[5] && !args[6] && args[1]<NAS_FD_LIMIT &&
       TASK.fds->entries[args[1]].kind==FD_SOCKET &&
       nas_lwip_socket_is_slot(TASK.fds->entries[args[1]].slot) &&
       nas_lwip_socket_type(TASK.fds->entries[args[1]].slot)==
           NAS_LWIP_BRIDGE_TCP)
        return task_pipe_io((uint64_t *)(uintptr_t)args,NAS_SC_recvfrom,
                            args[1],args[2],args[3],false);
    /* Connected send()/recv() with zero flags, plus TCP MSG_NOSIGNAL send,
     * use the held-OpenFile wait path. Unsupported per-call flags and all
     * addressed sendto requests stay on the strict socket ABI path. */
    const bool send_nosignal_wait=native_tcp_send_nosignal_wait(nr,args);
    if((nr==NAS_SC_sendto || nr==NAS_SC_recvfrom) &&
       (!args[4] || send_nosignal_wait) &&
       args[1]<NAS_FD_LIMIT && TASK.fds->entries[args[1]].kind==FD_SOCKET &&
       nas_lwip_socket_is_slot(TASK.fds->entries[args[1]].slot)
       ) {
        if(nr==NAS_SC_sendto && !args[5] && !args[6])
            return task_pipe_io((uint64_t *)(uintptr_t)args,NAS_SC_write,
                                args[1],args[2],args[3],send_nosignal_wait);
        if(nr==NAS_SC_recvfrom && !args[5])
            return task_pipe_io((uint64_t *)(uintptr_t)args,NAS_SC_read,
                                args[1],args[2],args[3],false);
    }
    /* Connected TCP sendmsg() is a stream writev with a Linux msghdr
     * wrapper. The File layer imports and retains every iovec once, and
     * the Task continuation parks/retries on real TX backpressure even if
     * another thread closes or reuses this integer fd meanwhile. */
    if(nr==NAS_SC_sendmsg && args[1]<NAS_FD_LIMIT &&
       TASK.fds->entries[args[1]].kind==FD_SOCKET) {
        uint64_t iov=0,count=0;
        int prepared=nas_fs_socket_tcp_sendmsg_iov(
            args[1],args[2],args[3],&iov,&count);
        if(prepared<0)return (uint64_t)(int64_t)prepared;
        if(prepared==1)return task_pipe_io((uint64_t *)(uintptr_t)args,
            NAS_SC_writev,args[1],iov,count,(args[3]&0x4000ULL)!=0);
    }
    if(nr==NAS_SC_recvmsg || nr==NAS_SC_recvfrom) {
        struct nas_netlink_receive_attempt attempt;
        nas_fs_netlink_receive_start(nr,args,&attempt);
        if(attempt.applicable) {
            if(!attempt.pending)return attempt.result;
            TASK.wait.netlink_receive=attempt.pending;
            TASK.wait.netlink_receive_release=nas_fs_netlink_receive_release;
            TASK.wait.description_slot=attempt.slot;
            TASK.wait.description_release=nas_fs_drop_kernel_description;
            nas_wait_io_clear(&TASK);
            nas_wait_io_arm(&TASK,attempt.slot,0x0001U,false);
            return nas_task_block((uint64_t *)(uintptr_t)args,nr,
                                  0,0,0,0,finish_netlink_receive);
        }
    }
    struct nas_fs_result fs_result = nas_fs_syscall(nr, args);
    if (fs_result.handled) {
#ifdef NASEED_SYSCALL_TRACE
        /* Native ncat/iperf3 diagnostics: syscall result BEFORE Task parking
         * or FileOps completion. This is an opt-in trace, never a socket
         * implementation branch, and does not inspect guest pointers. */
        if(nr==NAS_SC_socket || nr==NAS_SC_bind || nr==NAS_SC_listen ||
           nr==NAS_SC_connect || nr==NAS_SC_accept || nr==NAS_SC_accept4 ||
           nr==NAS_SC_setsockopt || nr==NAS_SC_getsockopt ||
           nr==NAS_SC_getsockname || nr==NAS_SC_getpeername ||
           nr==NAS_SC_sendto || nr==NAS_SC_recvfrom ||
           nr==NAS_SC_sendmsg || nr==NAS_SC_recvmsg ||
           nr==NAS_SC_shutdown) {
            nas_print("[NET SYS] pid=");nas_dec(TASK.pid);
            nas_print(" op=");nas_dec(nr);
            nas_print(" a1=");nas_hex(args[1]);
            nas_print(" a2=");nas_hex(args[2]);
            nas_print(" a3=");nas_hex(args[3]);
            nas_print(" a4=");nas_hex(args[4]);
            nas_print(" a5=");nas_hex(args[5]);
            nas_print(" a6=");nas_hex(args[6]);
            nas_print(" status=");
            if((int64_t)fs_result.value<0) {
                nas_print("-");nas_dec((uint64_t)(-(int64_t)fs_result.value));
            } else nas_dec(fs_result.value);
            nas_print("\n");
        }
#endif
        if(nr==NAS_SC_bind || nr==NAS_SC_connect || nr==NAS_SC_listen ||
           nr==NAS_SC_setsockopt || nr==NAS_SC_getsockopt ||
           nr==NAS_SC_shutdown ||
           nr==NAS_SC_getsockname || nr==NAS_SC_getpeername)
            return task_socket_control((uint64_t *)(uintptr_t)args,nr,args,
                                       fs_result.value);
        if(nr==NAS_SC_accept || nr==NAS_SC_accept4)
            return task_socket_accept((uint64_t *)(uintptr_t)args,nr,args,
                                      fs_result.value);
        return fs_result.value;
    }
    if (nr == NAS_SC_rt_sigaction) return nas_signal_action(args[1], args[2], args[3], args[4]);
    if (nr == NAS_SC_sigaltstack) return nas_signal_sigaltstack(args[1], args[2]);
    if (nr == NAS_SC_rt_sigpending) return nas_signal_pending(args[1], args[2]);
    if (nr == NAS_SC_rt_sigtimedwait) {
        /* Linux LP64 sigsetsize=8 on all three native ISAs. Read the set
         * exactly once before parking; no foreign address-space inspection
         * is necessary in the scheduler readiness pass. */
        if(args[4]!=8ULL)return (uint64_t)-EINVAL;
        uint64_t mask=0;
        if(!nas_user_read_u64(args[1],&mask))return (uint64_t)-EFAULT;
        uint64_t delta=0;
        if(args[3]) {
            uint64_t rc=nas_time_read_request(args[3],&delta);
            if((int64_t)rc<0)return rc;
        }
        mask &= ~((1ULL<<8)|(1ULL<<18));
        if(nas_signal_timedwait_ready(&TASK,mask))
            return consume_sigtimedwait(&TASK,mask,args[2]);
        if(args[3] && !delta)return (uint64_t)-EAGAIN;
        if(args[3]) {
            TASK.wait.deadline_ns=nas_time_add_deadline(
                nas_arch_monotonic_ns(),delta);
            TASK.wait.deadline_active=true;
        }
        return nas_task_block((uint64_t *)(uintptr_t)args,nr,
                              mask,args[2],0,0,finish_sigtimedwait);
    }
    if (nr == NAS_SC_rt_sigreturn) return nas_signal_sigreturn((uint64_t *)(uintptr_t)args);
    if (nr == NAS_SC_pause) {
        /* Unlike sigsuspend, pause retains the calling thread's mask.
         * Completion only occurs after a caught, unmasked signal. */
        if(nas_signal_user_pending(&TASK))return (uint64_t)-EINTR;
        return nas_task_block((uint64_t *)(uintptr_t)args,nr,0,0,0,0,
                              finish_sigsuspend);
    }
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
    if (nr == NAS_SC_kill)
        return nas_signal_kill(args[1],args[2],(uint64_t *)(uintptr_t)args);
    if (nr == NAS_SC_reboot) {
        uint32_t command=0;
        uint64_t result=nas_linux_reboot_validate(args[1],args[2],args[3],
                                                  &command);
        if(result)return result;
        /* Original BusyBox PID1 invokes reboot from a vfork child and then
         * sleeps forever; a successful Linux reboot(2) never returns to
         * userspace. Only AFTER validation, retire this child through its
         * normal exit_group/vfork-release path. The independent scheduler
         * stack then retires PID1 and durably unmounts ext4. */
        /* Separate original signed Debian runlevel-0 authorization from
         * BusyBox's vfork/signal shutdown contract. This does not grant
         * generic root shells a working reboot(2) syscall. */
#ifdef NAS_ENABLE_M257_DEBIAN_SHUTDOWN
        result=nas_init_debian_power_request(&TASK,command);
#else
        result=nas_init_alpine_power_request(&TASK,command);
#endif
        if(result)return result;
        nas_sched_thread_group_exit(0,0);
        return nas_task_schedule((uint64_t *)(uintptr_t)args);
    }
    if (nr == NAS_SC_tkill) return nas_signal_tkill(args[1],args[2],(uint64_t *)(uintptr_t)args);
    if (nr == NAS_SC_tgkill) return nas_signal_tgkill(args[1],args[2],args[3],(uint64_t *)(uintptr_t)args);
    if (nr == NAS_SC_umask) {
        uint64_t previous=TASK.fs->umask;
        TASK.fs->umask=nas_fs_umask(args[1]);
        return previous;
    }
    if (nr == NAS_SC_uname) return sys_uname_naseed(args[1]);
    if (nr == NAS_SC_sethostname) return sys_sethostname_naseed(args[1],args[2]);
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
                                        nas_linux_clone_tls(args),args[1]);
    if (nr == NAS_SC_vfork)
        return nas_process_vfork((uint64_t *)(uintptr_t)args,
                                  nas_linux_vfork_child_stack(args));
    if (nr == NAS_SC_fork)
        return nas_process_fork((uint64_t *)(uintptr_t)args);
    if (nr == NAS_SC_fork_child_tid)
        return nas_process_fork_child_tid((uint64_t *)(uintptr_t)args,
                                          nas_linux_clone_child_tid(args));

    if (nr == NAS_SC_getrusage)
        return nas_process_getrusage(args[1],args[2]);
    if (nr == NAS_SC_memfd_create)
        return nas_fs_memfd_create(args[1],args[2]);
    if (nr == NAS_SC_wait4) {
        uint64_t ret = nas_process_wait4(args[1], args[2], args[3], args[4]);
        /* WUNTRACED/WCONTINUED are real blocking wait selectors too.
         * Do not treat them as unsupported when a selected child has
         * not reached a reportable event yet. Keep WNOHANG nonblocking. */
        if ((int64_t)ret == -EOPNOTSUPP &&
            !(args[3] & NAS_WAIT_NOHANG))
            return nas_task_block((uint64_t *)(uintptr_t)args, nr,
                              args[1], args[2], args[3], args[4],finish_wait4);
        return ret;
    }
    if (nr == NAS_SC_waitid) {
        uint64_t ret=nas_process_waitid(args[1],args[2],args[3],args[4],args[5]);
        if((int64_t)ret==-EOPNOTSUPP && !(args[4]&NAS_WAIT_NOHANG))
            return nas_task_block5((uint64_t *)(uintptr_t)args,nr,
                                   args[1],args[2],args[3],args[4],args[5],
                                   finish_waitid);
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
