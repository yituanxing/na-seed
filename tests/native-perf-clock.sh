#!/bin/sh
# Optional guest-side workload timing. Source this from the original test
# driver; it NEVER decides acceptance. /proc/uptime is NaSeed's actual
# architecture-monotonic clock in hundredths, independent of UEFI RTC.
# A broken/missing clock produces no PERF line, not a fake zero duration.
# The same phase boundaries optionally sample genuine NaSeed /proc/stat.
# Keeping the samples here covers whole phases (including M56 Kconfig's
# multiple shell commands), without wrapping or changing original commands.
nas_profile_helper=${NAS_PROFILE_HELPER_FILE:-/tests/native-profile-guest.sh}
if [ -r "$nas_profile_helper" ]; then
    NAS_PROFILE_SOURCE_ONLY=1
    . "$nas_profile_helper"
    unset NAS_PROFILE_SOURCE_ONLY
else
    # Older rootfs images stay runnable; no profile is ever fabricated.
    nas_profile_begin() { :; }
    nas_profile_phase() { :; }
fi
unset nas_profile_helper
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
    nas_profile_begin "$nas_perf_workload" || :
}
nas_perf_phase() {
    nas_perf_stage=$1
    nas_perf_end=$(nas_perf_now_cs 2>/dev/null || :)
    nas_profile_phase "$nas_perf_stage" || :
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
