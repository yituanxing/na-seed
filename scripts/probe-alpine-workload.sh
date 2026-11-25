#!/bin/sh
# Native Alpine workload runner: keep independent guest cases and raw evidence.
set -eu
[ "$#" -eq 2 ] || exit 2
name=$1
port=$2
. scripts/native-qemu-target.sh
trace_value=; timeout_value=; failure=; runner_failure=; extra=; abi_oracles=0; clone_required=0
case "$name" in
    m38)
        kind=rootfs; profile=default; work="build/external/m38-gcc/$port"
        binary=usr/bin/gcc; manifest=m38-gcc-cases.tsv; ram=1024M; disk=768; log_name=m38-gcc.clean.log
        markers='M38_GCC_PASS M38_READV_PASS M38_FS_MODE_PASS'
        success="[M38 GCC PASS] $port original Alpine GCC + musl builds/links/runs Linux C ELF in NaSeed"
        trace_value=1
        ;;
    m39)
        kind=rootfs; profile=runtime; work="build/external/m39-packages/$port"
        binary=bin/busybox; manifest=m39-packages-cases.tsv; ram=512M; disk=256; log_name=m39-packages.clean.log
        markers='lua-vm-ok lua-io-ok M39_LUA_PASS M39_JQ_PASS M39_SQLITE_PASS M39_PACKAGES_PASS'
        success="[M39 PASS] $port prebuilt Alpine Lua/jq/SQLite real user programs"
        trace_value=${NAS_SYSCALL_TRACE:-0}
        failure="[M39 FAIL] missing guest marker %s ($port)"; tail_lines=130
        ;;
    m40)
        kind=rootfs; profile=archive; work="build/external/m40-archive/$port"
        binary=bin/busybox; manifest=m40-archives-cases.tsv; ram=512M; disk=256; log_name=m40-archives.clean.log
        markers='M40_TAR_CREATE_PASS M40_XZ_PASS M40_TAR_EXTRACT_PASS M40_ARCHIVES_PASS'
        success="[M40 PASS] $port original Alpine GNU tar + XZ Utils archive lifecycle"
        trace_value=${NAS_SYSCALL_TRACE:-0}
        failure="[M40 FAIL] missing %s ($port)"; tail_lines=140
        ;;
    m41)
        kind=rootfs; profile=unix; work="build/external/m41-unix/$port"
        binary=bin/busybox; manifest=m41-unix-cases.tsv; ram=512M; disk=256; log_name=m41-unix.clean.log
        markers='M41_GNU_ELF_PASS M41_COPY_PASS M41_FIND_EXEC_PASS M41_PIPELINE_PASS M41_RENAME_SHA256_PASS M41_DIFF_EXIT_PASS M41_UNIX_PASS'
        success="[M41 PASS] $port native GNU Unix pipeline and VFS acceptance"
        trace_value=${NAS_SYSCALL_TRACE:-0}
        failure="[M41 FAIL] missing %s ($port)"; tail_lines=140
        ;;
    m42)
        kind=rootfs; profile=pthread; work="build/external/m42-pthread/$port"
        binary=bin/busybox; manifest=m42-pthread-cases.tsv; ram=1024M; disk=256; log_name=m42-pthread.clean.log
        markers='M42_XZ_THREADS_PASS M42_PTHREAD_PASS A009_LINUX_PWRITE_APPEND_POSITION_PASS A010_LINUX_NATIVE_FCNTL_FLAGS_PASS A009_A010_NATIVE_ABI_DIFFERENTIAL_PASS A014_NATIVE_BLOCKED_READV_IOV_SNAPSHOT_PASS A014_NATIVE_BLOCKED_READV_FILE_SNAPSHOT_PASS A014_NATIVE_TWO_BLOCKING_OWNERSHIP_RACES_3X_PASS'
        success="[M42+A014 PASS] $port original pthread XZ and real guest blocked readv ownership"
        trace_value=${NAS_SYSCALL_TRACE:-0}
        abi_oracles=1
        failure="[M42 FAIL] missing %s ($port)"; tail_lines=140
        clone_required=1
        ;;
    m43)
        kind=rootfs; profile=zstd; work="build/external/m43-zstd/$port"
        binary=bin/busybox; manifest=m43-zstd-cases.tsv; ram=1024M; disk=256; log_name=m43-zstd.clean.log
        markers='M43_NATIVE_ZSTD_PASS M43_THREADS_PASS'
        success="[M43 PASS] $port original zstd pthreads and shared task state"
        trace_value=${NAS_SYSCALL_TRACE:-1}
        failure="[M43 FAIL] missing %s ($port)"; tail_lines=140
        clone_required=1
        ;;
    m45)
        kind=rootfs; profile=robust; work="build/external/m45-robust/$port"
        binary=bin/busybox; manifest=m45-robust-cases.tsv; ram=1024M; disk=256; log_name=m45-robust.clean.log
        markers='M45_REGISTRATION_PASS M45_ROBUST_OWNER_DIED_PASS M45_PENDING_AND_MULTI_LOCK_PASS M43_THREADS_PASS M45_ROBUST_AND_ZSTD_PASS'
        success="[M45 PASS] $port Linux robust futex recovery + genuine zstd pthreads"
        trace_value=0
        failure="[M45 FAIL] missing %s ($port)"; tail_lines=160
        ;;
    m46)
        kind=rootfs; profile=exec; work="build/external/m46-exec/$port"
        binary=bin/busybox; manifest=m46-exec-cases.tsv; ram=1024M; disk=256; log_name=m46-exec.clean.log
        markers='M46_EXEC_TARGET_PASS M46_LEADER_EXEC_WAIT4_PASS M46_WORKER_EXEC_TGID_WAIT4_PASS M46_MULTITHREAD_EXEC_PASS M43_THREADS_PASS M46_EXEC_AND_ZSTD_PASS'
        success="[M46 PASS] $port Linux execve from leader or nonleader + original zstd"
        trace_value=0
        failure="[M46 FAIL] missing %s ($port)"; tail_lines=180
        extra='[THREAD EXEC] worker adopted original process tgid=
[THREAD EXEC] leader reaped siblings tgid='
        ;;
    m47)
        kind=rootfs; profile=tgkill; work="build/external/m47-tgkill/$port"
        binary=bin/busybox; manifest=m47-tgkill-cases.tsv; ram=1024M; disk=256; log_name=m47-tgkill.clean.log
        markers='M47_WORKER_DIRECTED_PENDING_PASS M47_TGID_TID_VALIDATION_PASS M47_TARGETED_MASKED_WAIT_PASS M47_GROUP_FATAL_WAIT4_PASS M47_THREAD_SIGNALS_PASS M43_THREADS_PASS M47_SIGNALS_AND_ZSTD_PASS'
        success="[M47 PASS] $port directed tgkill/tkill, fatal group, original signed zstd"
        trace_value=0
        failure="[M47 FAIL] missing %s ($port)"; tail_lines=160
        extra='[THREAD GROUP] exit_group tgid='
        ;;
    m48)
        kind=rootfs; profile=parallel; work="build/external/m48-parallel/$port"
        binary=bin/busybox; manifest=m48-parallel-cases.tsv; ram=1024M; disk=768; log_name=m48-parallel.clean.log
        markers='M48_GNU_ORIGINAL_BINARIES_PASS M48_MAKE_PARALLEL_A_PASS M48_MAKE_PARALLEL_B_PASS M38_GCC_PASS M38_READV_PASS M48_MAKE_GCC_BUILD_PASS M48_PARALLEL_MAKE_PASS M48_XARGS_WORKER_alpha M48_XARGS_WORKER_beta M48_PARALLEL_XARGS_PASS M48_INCREMENTAL_REBUILD_PASS M48_NATIVE_PARALLEL_SUITE_PASS'
        success="[M48 PASS] $port original GNU Make -j2, original GCC/musl, xargs -P2"
        trace_value=0
        failure="[M48 FAIL] missing marker %s ($port)"; tail_lines=180
        ;;
    m49)
        kind=rootfs; profile=capacity; work="build/external/m49-capacity/$port"
        binary=bin/busybox; manifest=m49-capacity-cases.tsv; ram=1024M; disk=768; log_name=m49-capacity.clean.log
        markers='M49_ORIGINAL_GNU_BINARIES_PASS M49_MAKE_ALPHA_PARALLEL_PASS M49_MAKE_BETA_PARALLEL_PASS M49_MAKE_GAMMA_PARALLEL_PASS M49_MAKE_DELTA_PARALLEL_PASS M49_GNU_MAKE_J4_PASS M49_FOUR_PARALLEL_COMPILERS_PASS M49_XARGS_alpha_PASS M49_XARGS_beta_PASS M49_XARGS_gamma_PASS M49_XARGS_delta_PASS M49_FOUR_PARALLEL_XARGS_PASS M49_24_SIMULTANEOUS_CHILDREN_PASS M49_24_CHILDREN_WAIT4_REAP_PASS M49_24_CHILDREN_KERNEL_PASS M49_INCREMENTAL_PARALLEL_MAKE_PASS M49_NATIVE_CAPACITY_SUITE_PASS'
        success="[M49 PASS] $port 48-task kernel, real GNU Make -j4/GCC, xargs -P4, 24 fork+wait4"
        trace_value=0
        failure="[M49 FAIL] missing %s ($port)"; tail_lines=240
        ;;
    m51)
        kind=rootfs; profile=git; work="build/external/m51-git/$port"
        binary=bin/busybox; manifest=m51-git-cases.tsv; ram=768M; disk=512; log_name=m51-git.clean.log
        markers='M51_ORIGINAL_SIGNED_GIT_PASS M51_GIT_INIT_PASS M51_GIT_OBJECT_INDEX_COMMIT_PASS M51_GIT_DIFF_HISTORY_PASS M51_GIT_REFS_CHECKOUT_PASS M51_GIT_SUITE_PASS'
        success="[M51 PASS] $port original Git init/add/commit/diff/history/branch/checkout"
        trace_value=${NAS_SYSCALL_TRACE:-0}
        failure="[M51 FAIL] missing real Git marker %s ($port)"; tail_lines=180
        ;;
    m52)
        kind=rootfs; profile=python; work="build/external/m52-python/$port"
        binary=bin/busybox; manifest=m52-python-cases.tsv; ram=1024M; disk=512; log_name=m52-python.clean.log
        markers='M52_ORIGINAL_SIGNED_PYTHON_PASS M52_PYTHON_BYTECODE_PASS M52_PYTHON_STDLIB_JSON_HASH_ZLIB_PASS M52_PYTHON_TMPFS_PATHLIB_PASS M52_PYTHON_SQLITE_FILE_PASS M52_PYTHON_PTHREAD_PASS M52_PYTHON_SUBPROCESS_PASS M52_PYTHON_SUITE_PASS'
        success="[M52 PASS] $port CPython, stdlib, SQLite, pthread and subprocess"
        trace_value=${NAS_SYSCALL_TRACE:-1}
        failure="[M52 FAIL] $port no marker %s"; tail_lines=160
        ;;
    m54)
        kind=rootfs; profile=dev; work="build/external/m54-dev/$port"
        binary=bin/busybox; manifest=m54-dev-cases.tsv; ram=1536M; disk=768; log_name=m54-dev.clean.log
        markers='M54_ORIGINAL_FIVE_NATIVE_TOOLS_PASS M54_GIT_INITIAL_SOURCES_PASS M54_VIM_EDIT_GIT_DIFF_PASS M54_GNU_MAKE_GCC_LINK_RUN_PASS M54_PYTHON_EXEC_GUEST_COMPILED_7_PASS M54_PYTHON_HASH_JSON_7_PASS M54_PYTHON_SQLITE_GUEST_BUILD_7_PASS M54_PYTHON_GIT_WORKTREE_7_PASS M54_GIT_COMMIT_BUILT_SOURCE_PASS M54_INCREMENTAL_VIM_MAKE_REBUILD_PASS M54_PYTHON_EXEC_GUEST_COMPILED_9_PASS M54_PYTHON_HASH_JSON_9_PASS M54_PYTHON_SQLITE_GUEST_BUILD_9_PASS M54_PYTHON_GIT_WORKTREE_9_PASS M54_DEV_FULL_LIFECYCLE_PASS'
        success="[M54 PASS] $port real Vim -> GNU Make -j2/GCC -> executable -> Python -> Git"
        trace_value=${NAS_SYSCALL_TRACE:-0}
        failure="[M54 FAIL] $port missing guest stage %s"; tail_lines=180
        ;;
    m56)
        kind=rootfs; profile=native-busybox; work="build/external/m56-native-busybox/$port"
        binary=bin/busybox; manifest=m56-native-busybox-cases.tsv; ram=2048M; disk=1024; log_name=m56-busybox.clean.log
        markers='M56_GENUINE_NATIVE_ALPINE_TOOLCHAIN_PASS M56_LINUX_LONG_EXECVE_ARG_PASS M56_LINUX_MAKE_ENV_CAPACITY_PASS M56_LINUX_LARGE_ARGV_VECTOR_PASS M56_UPSTREAM_SOURCE_IN_TMPFS_PASS M56_NATIVE_KCONFIG_PINNED_OPTIONS_PASS M56_REAL_UPSTREAM_KBUILD_LINK_PASS M56_NEW_GUEST_BUILT_BUSYBOX_EXEC_PASS M56_REAL_GUEST_BUSYBOX_FULL_PASS'
        success="[M56 PASS] $port upstream BusyBox configured/compiled/linked/executed WITHIN NaSeed"
        trace_value=${NAS_SYSCALL_TRACE:-0}
        timeout_value=${NAS_M56_QEMU_TIMEOUT:-3600}
        failure="[M56 FAIL] $port missing stage %s"; tail_lines=130
        ;;
    m57)
        kind=rootfs; profile=sqlite; work="build/external/m57-native-sqlite/$port"
        binary=bin/busybox; manifest=m57-native-sqlite-cases.tsv; ram=2048M; disk=1024; log_name=m57-sqlite.clean.log
        markers='M57_GENUINE_UPSTREAM_SOURCE_PASS M57_LINUX_LARGE_TMPFS_INODE_PASS M57_NATIVE_SQLITE_CONFIGURE_PASS M57_NATIVE_SQLITE_COMPILE_LINK_PASS M57_NEW_SQLITE_CLI_EXEC_PASS M57_FILE_DB_CREATE_COMMIT_PASS M57_FILE_DB_REOPEN_QUERY_PASS M57_TRANSACTION_ROLLBACK_PASS M57_SQLITE_INDEX_JOIN_PASS M57_SQLITE_INTEGRITY_PASS M57_NATIVE_SQLITE_FULL_PASS'
        success="[M57 PASS] $port genuine upstream SQLite 3.54.1 configured/built/executed in native NaSeed guest"
        trace_value=${NAS_SYSCALL_TRACE:-0}
        timeout_value=${NAS_M57_QEMU_TIMEOUT:-3600}
        failure="[M57 FAIL] $port missing stage %s"; tail_lines=160
        ;;
    m58)
        kind=basic; profile=m58-runtime; work="build/external/m58-runtime/$port"
        binary=bin/busybox; manifest=m58-runtime-cases.tsv; ram=768M; disk=512; log_name=m58-runtime.clean.log
        markers='M58_ORIGINAL_SIGNED_BASH_PASS M58_BASH_PIPELINE_PASS M58_BASH_REDIRECTION_ARRAY_PASS M58_BASH_FORK_WAIT_PASS M58_BASH_TRAP_PASS M58_TCL_FILE_PASS M58_TCL_EXEC_PIPE_PASS M58_TCL_TIMER_PASS M58_TCL_FULL_PASS M58_ORIGINAL_SIGNED_TCL_PASS M58_ORIGINAL_SIGNED_OPENSSL_PASS M58_OPENSSL_ENTROPY_PASS M58_OPENSSL_DIGEST_PASS M58_OPENSSL_ROUNDTRIP_PASS M58_OPENSSL_CERT_VERIFY_PASS M58_RUNTIME_FULL_PASS'
        success="[M58 PASS] $port genuine prebuilt Bash, Tcl and OpenSSL executed under NaSeed QEMU"
        trace_value=${NAS_SYSCALL_TRACE:-0}
        failure="[M58 FAIL] $port: missing guest marker %s"; tail_lines=150
        ;;
    m59)
        kind=basic; profile=m59-worktree; work="build/external/m59-worktree/$port"
        binary=bin/busybox; manifest=m59-worktree-cases.tsv; ram=768M; disk=512; log_name=m59-worktree.clean.log
        markers='M59_ORIGINAL_RSYNC_PASS M59_RSYNC_TREE_PASS M59_RSYNC_INCREMENTAL_PASS M59_RSYNC_DELETE_PASS M59_DIFF_UNIFIED_PASS M59_PATCH_APPLY_PASS M59_ORIGINAL_FILE_PASS M59_FILE_ELF_MAGIC_PASS M59_FILE_TEXT_MAGIC_PASS M59_FULL_PASS'
        success="[M59 PASS] $port rsync/diff/patch/file real original binaries, all assertions"
        trace_value=${NAS_SYSCALL_TRACE:-0}
        failure="[M59 FAIL] $port missing %s"; tail_lines=150
        ;;
    m60)
        kind=basic; profile=m60-terminal; work="build/external/m60-terminal/$port"
        binary=bin/busybox; manifest=m60-terminal-cases.tsv; ram=768M; disk=512; log_name=m60-terminal.clean.log
        markers='M60_ORIGINAL_LESS_PASS M60_LESS_FILE_FILTER_PASS M60_LESS_STDIN_STREAM_PASS M60_ORIGINAL_TMUX_PASS M60_TMUX_REAL_PTY_CHILD_PASS M60_FULL_PASS'
        success="[M60 PASS] $port original less + tmux with real PTY child"
        trace_value=${NAS_SYSCALL_TRACE:-0}
        failure="[M60 FAIL] $port missing %s"; tail_lines=160
        ;;
    m65)
        kind=basic; profile=m65-observe; work="build/external/m65-observe/$port"
        binary=bin/busybox; manifest=m65-procfs-cases.tsv; ram=768M; disk=512; log_name=m65-procfs.clean.log
        markers='M65_PROCFS_NATIVE_PASS M65_PROCPS_FREE_PASS M65_PROCPS_PS_PASS M65_PROCPS_FULL_PASS'
        success="[M65 PASS] $port original signed procps-ng ps/free, procfs inode/FD/Task"
        failure="[M65 FAIL] $port missing %s"; tail_lines=160
        ;;
    m66)
        kind=basic; profile=m66-monitor; work="build/external/m66-monitor/$port"
        binary=bin/busybox; manifest=m66-monitor-cases.tsv; ram=768M; disk=512; log_name=m66-monitor.clean.log
        markers='M66_PROCFS_METRICS_PASS M66_ORIGINAL_TOP_PASS M66_ORIGINAL_UPTIME_PASS M66_ORIGINAL_HTOP_ELF_PASS M66_ORIGINAL_HTOP_PTY_PASS M66_MONITOR_FULL_PASS'
        success="[M66 PASS] $port original signed procps-ng top/htop on real procfs"
        failure="[M66 FAIL] $port missing %s"; tail_lines=160
        ;;
    m67)
        kind=basic; profile=m67-control; work="build/external/m67-control/$port"
        binary=bin/busybox; manifest=m67-control-cases.tsv; ram=768M; disk=512; log_name=m67-control.clean.log
        markers='M67_REAL_TMUX_PTYS_PASS M67_STD_DEVICE_NULL_PASS M67_ORIGINAL_PGREP_CMDLINE_PASS M67_ORIGINAL_PGREP_COMM_PASS M67_ORIGINAL_PKILL_STOP_CONT_PASS M67_ORIGINAL_PKILL_TERM_WAIT_PASS M67_ORIGINAL_PKILL_KILL_WAIT_PASS M67_ORIGINAL_PGREP_REAPED_PASS M68_ASH_BACKGROUND_WAIT_PASS M68_ASH_FD0_REOPEN_PASS M68_ASH_BACKGROUND_PIPE_PASS M67_PROCPS_CONTROL_FULL_PASS'
        success="[M67 PASS] $port original signed procps-ng pgrep/pkill signals, /proc and wait4"
        failure="[M67 FAIL] $port missing %s"; tail_lines=480
        ;;
    m69)
        kind=basic; profile=m69-inspect; work="build/external/m69-inspect/$port"
        binary=bin/busybox; manifest=m69-inspect-cases.tsv; ram=768M; disk=512; log_name=m69-inspect.clean.log
        markers='M69_PROC_THREAD_NAMESPACE_PASS M69_ORIGINAL_PROCPS_THREADS_PASS M69_ORIGINAL_PSTREE_PASS M69_ORIGINAL_TASKSET_AFFINITY_PASS M69_ORIGINAL_PRLIMIT_PASS M69_TRACE_PREP_FULL_PASS'
        success="[M69 PASS] $port original signed pstree / ps -eL / taskset / prlimit"
        failure="[M69 FAIL] $port missing %s"; tail_lines=480
        ;;
    m70)
        kind=basic; profile=m70-vmfd; work="build/external/m70-vmfd/$port"
        binary=bin/busybox; manifest=m70-vmfd-cases.tsv; ram=768M; disk=512; log_name=m70-vmfd.clean.log
        markers='M70_PROC_FD_PASS M70_PROC_MAPS_PASS M70_ORIGINAL_PMAP_PASS M70_ORIGINAL_LSOF_PASS M70_VMFD_FULL_PASS'
        success="[M70 PASS] $port original signed pmap/lsof against real native Task VMA and FD objects"
        failure="[M70 FAIL] $port missing %s"; tail_lines=480
        ;;
    m72)
        kind=basic; profile=m72-strace; work="build/external/m72-strace/$port"
        binary=bin/busybox; manifest=m72-strace-cases.tsv; ram=768M; disk=512; log_name=m72-strace.clean.log
        markers='M72_ORIGINAL_STRACE_LOAD_PASS M72_GENUINE_PTRACE_PASS'
        success="[M72 PASS] $port genuine original Alpine strace syscall observations"
        failure="[M72 FAIL] $port missing %s"; tail_lines=360
        runner_failure="[M72 FAIL] $port original strace could not trace the original process"; runner_tail=360
        ;;
    m73)
        kind=basic; profile=m73-strace; work="build/external/m73-strace/$port"
        binary=bin/busybox; manifest=m73-strace-cases.tsv; ram=768M; disk=512; log_name=m73-follow.clean.log
        markers=M73_BLOCKED_SYSCALL_PASS
        success="[M73 PASS] $port genuine original Alpine strace multi-task observations"
        failure="[M73 FAIL] $port missing %s"; tail_lines=360
        runner_failure="[M73 FAIL] $port original strace could not trace the original process"; runner_tail=360
        ;;
    m74)
        kind=basic; profile=m74-strace; work="build/external/m74-strace/$port"
        binary=bin/busybox; manifest=m74-strace-cases.tsv; ram=768M; disk=512; log_name=m74-attach.clean.log
        markers=M74_ATTACH_REAL_TRACE_PASS
        success="[M74 PASS] $port original Alpine strace truly attached and traced a separate process"
        failure="[M74 FAIL] $port missing %s"; tail_lines=360
        runner_failure="[M74 FAIL] $port original strace could not trace the original process"; runner_tail=360
        ;;
    m75)
        kind=basic; profile=m75-gdb; work="build/external/m75-gdb/$port"
        binary=bin/busybox; manifest=m75-gdb-cases.tsv; ram=1024M; disk=768; log_name=m75-debug.clean.log
        markers=M75_REAL_GDB_INFERIOR_PASS
        success="[M75 PASS] $port original Alpine GDB truly attached and traced a separate process"
        failure="[M75 FAIL] $port missing %s"; tail_lines=360
        runner_failure="[M75 FAIL] $port original GDB could not trace the original process"; runner_tail=360
        ;;
    m79)
        kind=basic; profile=m79-workflow; work="build/external/m79-workflow/$port"
        binary=bin/busybox; manifest=m79-workflow-cases.tsv; ram=1536M; disk=768; log_name=m79-workflow.clean.log
        markers='M79_ORIGINAL_TOOLS_PASS M79_BASH_PIPE_BACKGROUND_PASS M79_VIM_GIT_DIFF_PASS M79_PYTHON_JSON_SQLITE_PASS M79_GIT_COMMIT_RECHECK_PASS M79_FOUR_ORIGINAL_PROGRAMS_PASS'
        success="[M79 PASS] $port actual Bash/Vim/Git/Python end-to-end guest workflow"
        trace_value=0
        failure="[M79 FAIL] $port missing authentic guest stage %s"; tail_lines=320
        runner_failure="[M79 FAIL] $port authentic four-program guest workflow"; runner_tail=320
        ;;
    *) exit 2 ;;
esac
case "$kind:$profile" in
    rootfs:default) sh rootfs/alpine/build.sh "$port" ;;
    rootfs:*) NAS_ALPINE_PROFILE="$profile" sh rootfs/alpine/build.sh "$port" ;;
    basic:*) sh scripts/stage-alpine-basic.sh "$port" "$profile" ;;
esac
if [ "$abi_oracles" -eq 1 ]; then
    sh scripts/stage-m42-abi-oracles.sh "$port"
fi
export BUSYBOX_PARITY_BINARY="$work/rootfs/$binary"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST="scripts/$manifest"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM="$ram"
export NAS_ACCEPTANCE_DISK_MB="$disk"
if [ -n "$trace_value" ]; then export NAS_SYSCALL_TRACE="$trace_value"; fi
case "$name" in
    m56) export NAS_M56_QEMU_TIMEOUT="$timeout_value" ;;
    m57) export NAS_M57_QEMU_TIMEOUT="$timeout_value" ;;
esac
log="$logs/$log_name"
if sh "$runner"; then
    :
else
    status=$?
    if [ -n "$runner_failure" ]; then
        echo "$runner_failure" >&2
        test ! -s "$log" || tail -"$runner_tail" "$log" >&2
        exit 1
    fi
    exit "$status"
fi
test -s "$log"
for marker in $markers; do
    if ! grep -Fxq "$marker" "$log"; then
        if [ -n "$failure" ]; then
            printf "$failure\n" "$marker" >&2
            tail -"$tail_lines" "$log" >&2
        fi
        exit 1
    fi
done
if [ "$clone_required" -eq 1 ]; then
    grep -Fq '[THREAD] clone tid=' "$log" || {
        echo "[M${name#m} FAIL] no real clone thread ($port)" >&2
        tail -120 "$log" >&2
        exit 1
    }
fi
if [ -n "$extra" ]; then
    printf '%s\n' "$extra" | while IFS= read -r line; do
        grep -Fq "$line" "$log" || exit 1
    done
fi
echo "$success"
