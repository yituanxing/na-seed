#!/bin/sh
# Optional guest-side workload timing. Source this from the original test
# driver; it NEVER decides acceptance. /proc/uptime is NaSeed's actual
# architecture-monotonic clock in hundredths, independent of UEFI RTC.
# A broken/missing clock produces no PERF line, not a fake zero duration.
nas_perf_now_cs() {
    nas_perf_secs='' nas_perf_fraction='' nas_perf_idle=''
    IFS=' .' read -r nas_perf_secs nas_perf_fraction nas_perf_idle \
        < /proc/uptime || return 1
    case "$nas_perf_secs" in ''|*[!0-9]*) return 1 ;; esac
    case "$nas_perf_fraction" in [0-9][0-9]) ;; *) return 1 ;; esac
    nas_perf_fraction=${nas_perf_fraction#0}
    [ -n "$nas_perf_fraction" ] || nas_perf_fraction=0
    printf '%s\n' "$((nas_perf_secs * 100 + nas_perf_fraction))"
}
nas_perf_begin() {
    nas_perf_workload=$1
    nas_perf_start=$(nas_perf_now_cs 2>/dev/null || :)
}
nas_perf_phase() {
    nas_perf_stage=$1
    nas_perf_end=$(nas_perf_now_cs 2>/dev/null || :)
    case "$nas_perf_start:$nas_perf_end" in
        :*|*:) return 0 ;;
    esac
    if [ "$nas_perf_end" -ge "$nas_perf_start" ]; then
        printf 'NAS_PERF_PHASE workload=%s phase=%s elapsed_cs=%s\n' \
            "$nas_perf_workload" "$nas_perf_stage" \
            "$((nas_perf_end - nas_perf_start))"
    fi
    nas_perf_start=$nas_perf_end
}
