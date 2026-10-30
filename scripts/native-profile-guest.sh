#!/bin/sh
# Lightweight CPU occupancy sampling INSIDE NaSeed. The kernel's /proc/stat
# deliberately writes its aggregate task-busy centiseconds in the Linux
# "system" field: "cpu 0 0 <busy_cs> <idle_cs> ..." (see kernel/fs/procfs.c).
# This is single-CPU task occupancy, NOT measured kernel-mode CPU time,
# hardware cycles or the sum of Linux's user/nice/system/IRQ fields.
#
# The source-build phase clock sources this file with
# NAS_PROFILE_SOURCE_ONLY=1. The standalone command wrapper remains useful
# for independent diagnostics: sh native-profile-guest.sh m56 kbuild -- make.

nas_stat_snapshot() {
    nas_busy='' nas_idle='' nas_switches=''
    nas_cpu_seen=0 nas_ctxt_seen=0
    nas_stat_file=${NAS_PROFILE_STAT_FILE:-/proc/stat}
    [ -r "$nas_stat_file" ] || return 1
    while read -r nas_key nas_a nas_b nas_c nas_d nas_rest; do
        case "$nas_key" in
            cpu)
                # Never interpret an ordinary Linux /proc/stat as NaSeed's
                # special task-occupancy accounting (user=nice=0).
                [ "$nas_cpu_seen" -eq 0 ] &&
                    [ "$nas_a" = 0 ] && [ "$nas_b" = 0 ] || return 1
                nas_busy=$nas_c nas_idle=$nas_d nas_cpu_seen=1
                ;;
            ctxt)
                [ "$nas_ctxt_seen" -eq 0 ] || return 1
                nas_switches=$nas_a nas_ctxt_seen=1
                ;;
        esac
    done < "$nas_stat_file"
    [ "$nas_cpu_seen" -eq 1 ] && [ "$nas_ctxt_seen" -eq 1 ] || return 1
    for nas_number in "$nas_busy" "$nas_idle" "$nas_switches"; do
        case "$nas_number" in ''|*[!0-9]*) return 1 ;; esac
    done
    return 0
}

nas_profile_begin() {
    nas_profile_workload=$1
    nas_profile_valid=0
    if nas_stat_snapshot; then
        nas_profile_before_busy=$nas_busy
        nas_profile_before_idle=$nas_idle
        nas_profile_before_switches=$nas_switches
        nas_profile_valid=1
    fi
    return 0
}

nas_profile_phase() {
    nas_profile_phase_name=$1
    if [ "${nas_profile_valid:-0}" -eq 1 ] && nas_stat_snapshot &&
       [ "$nas_busy" -ge "$nas_profile_before_busy" ] &&
       [ "$nas_idle" -ge "$nas_profile_before_idle" ] &&
       [ "$nas_switches" -ge "$nas_profile_before_switches" ]; then
        printf 'NAS_PERF_COUNTER workload=%s scope=cpu metric=%s.busy value=%s unit=cs\n' \
            "$nas_profile_workload" "$nas_profile_phase_name" \
            "$((nas_busy - nas_profile_before_busy))"
        printf 'NAS_PERF_COUNTER workload=%s scope=cpu metric=%s.idle value=%s unit=cs\n' \
            "$nas_profile_workload" "$nas_profile_phase_name" \
            "$((nas_idle - nas_profile_before_idle))"
        printf 'NAS_PERF_COUNTER workload=%s scope=cpu metric=%s.switches value=%s unit=count\n' \
            "$nas_profile_workload" "$nas_profile_phase_name" \
            "$((nas_switches - nas_profile_before_switches))"
    fi
    nas_profile_valid=0
    return 0
}

if [ "${NAS_PROFILE_SOURCE_ONLY:-0}" != 1 ]; then
    set -eu
    if [ "$#" -lt 4 ] || [ "$3" != -- ]; then
        echo 'usage: native-profile-guest.sh m38|m55|m56 phase -- command [args...]' >&2
        exit 2
    fi
    nas_cli_workload=$1
    nas_cli_phase=$2
    shift 3
    case "$nas_cli_workload" in m38|m55|m56) ;; *) exit 2 ;; esac
    case "$nas_cli_phase" in ''|*[!a-z0-9_]*) exit 2 ;; esac
    nas_profile_begin "$nas_cli_workload"
    nas_cli_status=0
    "$@" || nas_cli_status=$?
    [ "$nas_cli_status" -eq 0 ] || exit "$nas_cli_status"
    nas_profile_phase "$nas_cli_phase" || :
fi
