#!/bin/sh
# Source this file from a POSIX sh acceptance runner (do not execute it).
# One EXIT owner for host-side observers.  Guest serial logs, NIC PCAPs and
# physical ext4 evidence are intentionally never removed by this module.
#
# Contract: register only a direct background child from this shell, using
# "$!" immediately after launch.  Unregister it immediately after an explicit
# wait.  Do not register daemonized grandchildren or guessed/stale PIDs.
# A POSIX shell cannot reliably prove an arbitrary numeric PID is its child.

nas_acceptance_host_supervisor_pids=
nas_acceptance_host_supervisor_installed=0
nas_acceptance_host_supervisor_cleaned=0

nas_acceptance_host_supervisor_valid_pid() {
    [ "$#" -eq 1 ] || return 2
    case "$1" in
        ''|0*|*[!0-9]*) return 2 ;;
    esac
    # Numeric comparison also rejects zero, PID 1, and out-of-range values.
    [ "$1" -gt 1 ] 2>/dev/null || return 2
    [ "$1" != "$$" ] || return 2
}

nas_acceptance_host_supervisor_register() {
    [ "$#" -eq 1 ] || return 2
    nas_acceptance_host_supervisor_valid_pid "$1" || return 2
    [ "$nas_acceptance_host_supervisor_installed" = 1 ] || return 2
    [ "$nas_acceptance_host_supervisor_cleaned" = 0 ] || return 2
    # Space-delimited numeric PIDs have no splitting or pattern ambiguity.
    case " $nas_acceptance_host_supervisor_pids " in
        *" $1 "*) return 0 ;; # duplicate registration is harmless
    esac
    nas_acceptance_host_supervisor_pids="${nas_acceptance_host_supervisor_pids:+$nas_acceptance_host_supervisor_pids }$1"
}

nas_acceptance_host_supervisor_unregister() {
    [ "$#" -eq 1 ] || return 2
    nas_acceptance_host_supervisor_valid_pid "$1" || return 2
    nas_acceptance_host_supervisor_remaining=
    for nas_acceptance_host_supervisor_each in $nas_acceptance_host_supervisor_pids; do
        [ "$nas_acceptance_host_supervisor_each" = "$1" ] && continue
        nas_acceptance_host_supervisor_remaining="${nas_acceptance_host_supervisor_remaining:+$nas_acceptance_host_supervisor_remaining }$nas_acceptance_host_supervisor_each"
    done
    nas_acceptance_host_supervisor_pids=$nas_acceptance_host_supervisor_remaining
}

nas_acceptance_host_supervisor_cleanup() {
    [ "$nas_acceptance_host_supervisor_cleaned" = 0 ] || return 0
    nas_acceptance_host_supervisor_cleaned=1
    nas_acceptance_host_supervisor_cleanup_pids=$nas_acceptance_host_supervisor_pids
    nas_acceptance_host_supervisor_pids=
    # Signal ALL registered observers first, rather than allowing a wait on
    # one process to leave the other observers running indefinitely.
    for nas_acceptance_host_supervisor_each in $nas_acceptance_host_supervisor_cleanup_pids; do
        kill -TERM "$nas_acceptance_host_supervisor_each" 2>/dev/null || :
    done
    if [ -n "$nas_acceptance_host_supervisor_cleanup_pids" ]; then
        sleep 1 || :
    fi
    # Bound cleanup even when an observer ignores TERM.  Only the direct
    # children supplied via register are signalled; never a process group.
    for nas_acceptance_host_supervisor_each in $nas_acceptance_host_supervisor_cleanup_pids; do
        if kill -0 "$nas_acceptance_host_supervisor_each" 2>/dev/null; then
            kill -KILL "$nas_acceptance_host_supervisor_each" 2>/dev/null || :
        fi
    done
    for nas_acceptance_host_supervisor_each in $nas_acceptance_host_supervisor_cleanup_pids; do
        wait "$nas_acceptance_host_supervisor_each" 2>/dev/null || :
    done
    return 0
}

nas_acceptance_host_supervisor_on_exit() {
    nas_acceptance_host_supervisor_original_status=$1
    trap - 0
    # Repeated runner signals must not interrupt the bounded cleanup or
    # replace the failure that originally caused this EXIT.
    trap '' 1 2 15
    nas_acceptance_host_supervisor_cleanup
    exit "$nas_acceptance_host_supervisor_original_status"
}

nas_acceptance_host_supervisor_install() {
    [ "$#" -eq 0 ] || return 2
    [ "$nas_acceptance_host_supervisor_cleaned" = 0 ] || return 2
    [ "$nas_acceptance_host_supervisor_installed" = 0 ] || return 0
    nas_acceptance_host_supervisor_installed=1
    # This is deliberately the single EXIT owner.  Install only at the
    # acceptance runner's central trap boundary; existing EXIT traps must be
    # migrated into this owner rather than silently overwritten later.
    trap 'nas_acceptance_host_supervisor_on_exit "$?"' 0
    trap 'exit 129' 1
    trap 'exit 130' 2
    trap 'exit 143' 15
}
