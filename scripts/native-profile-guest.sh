#!/bin/sh
# Opt-in diagnostic wrapper INSIDE the real NaSeed guest. It records the
# kernel's /proc/stat single-CPU accounting, never a host QEMU timing proxy.
# Usage: sh native-profile-guest.sh m55 ninja_build -- command [arguments...]
# Keep original acceptance and NAS_PERF_PHASE instrumentation outside here.
set -eu

if [ "$#" -lt 4 ] || [ "$3" != -- ]; then
    echo 'usage: native-profile-guest.sh m38|m55|m56 phase -- command [args...]' >&2
    exit 2
fi
nas_workload=$1
nas_phase=$2
shift 3
case "$nas_workload" in m38|m55|m56) ;; *) exit 2 ;; esac
case "$nas_phase" in ''|*[!a-z0-9_]*) exit 2 ;; esac

# Test override only: the production input remains the guest's real procfs.
nas_stat_file=${NAS_PROFILE_STAT_FILE:-/proc/stat}
nas_stat_snapshot() {
    nas_busy='' nas_idle='' nas_switches=''
    [ -r "$nas_stat_file" ] || return 1
    while read -r nas_key nas_a nas_b nas_c nas_d nas_rest; do
        case "$nas_key" in
            cpu) nas_busy=$nas_c; nas_idle=$nas_d ;;
            ctxt) nas_switches=$nas_a ;;
        esac
    done < "$nas_stat_file"
    for nas_number in "$nas_busy" "$nas_idle" "$nas_switches"; do
        case "$nas_number" in ''|*[!0-9]*) return 1 ;; esac
    done
}

nas_before_valid=false
if nas_stat_snapshot; then
    nas_before_valid=true
    nas_before_busy=$nas_busy
    nas_before_idle=$nas_idle
    nas_before_switches=$nas_switches
fi

# Failure is the original command's failure. Never emit a successful profile
# for a failed original compiler/build/test command.
nas_command_status=0
"$@" || nas_command_status=$?
[ "$nas_command_status" -eq 0 ] || exit "$nas_command_status"

if [ "$nas_before_valid" = true ] && nas_stat_snapshot &&
   [ "$nas_busy" -ge "$nas_before_busy" ] &&
   [ "$nas_idle" -ge "$nas_before_idle" ] &&
   [ "$nas_switches" -ge "$nas_before_switches" ]; then
    printf 'NAS_PERF_COUNTER workload=%s scope=cpu metric=%s.busy value=%s unit=cs\n' \
        "$nas_workload" "$nas_phase" "$((nas_busy - nas_before_busy))"
    printf 'NAS_PERF_COUNTER workload=%s scope=cpu metric=%s.idle value=%s unit=cs\n' \
        "$nas_workload" "$nas_phase" "$((nas_idle - nas_before_idle))"
    printf 'NAS_PERF_COUNTER workload=%s scope=cpu metric=%s.switches value=%s unit=count\n' \
        "$nas_workload" "$nas_phase" "$((nas_switches - nas_before_switches))"
fi
