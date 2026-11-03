#!/bin/sh
# M175: real shared IRQ ACK + deferred VirtIO input, strictly zero fallback.
# One existing M174 system-QEMU run per ISA, never a second replay or fake
# serial-marker-only success. The QMP variant preserves all five M173 frames.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh

# M174 still owns signed Alpine 20-ELF staging, devfs/PTY, original guest
# semantics, and the five physical pixel oracle. Override only QMP input.
NAS_QMP_VERIFY_SCRIPT=scripts/m175-qmp-verify.py
export NAS_QMP_VERIFY_SCRIPT
sh scripts/probe-m174-batch.sh "$port"

work="build/external/m174-software-batch/$port"
log="$logs/m174-batch.clean.log"
serial="$logs/m174-batch.serial.log"
oracle="$work/m174-qmp-check.log"
for file in "$log" "$serial" "$oracle"; do
    test -s "$file" || {
        echo "[M175 IRQ FAIL] $port missing real guest/QMP log: $file" >&2
        exit 1
    }
done

# M174 succeeded only after 20 genuine native ELF exec events, real
# dbus/proc work, mounted devfs checks, and five QEMU screen assertions.
# Require both QMP's physical-pixel conclusion and its extra HARDWARE bursts.
for verdict in '[M173 QMP DYNAMIC PASS]' '[M175 QMP IRQ BURST PASS]'; do
    [ "$(grep -Fc "$verdict" "$oracle" || :)" -eq 1 ] || {
        echo "[M175 IRQ FAIL] $port missing/duplicate QMP verdict: $verdict" >&2
        exit 1
    }
done
for frame in four five clicked closed shell-after; do
    shot="$work/m173-$frame.ppm"
    test -s "$shot" || {
        echo "[M175 IRQ FAIL] $port QMP framebuffer image absent: $frame" >&2
        exit 1
    }
    [ "$(wc -c < "$shot")" -eq 1440015 ] || {
        echo "[M175 IRQ FAIL] $port invalid 800x600 physical frame size: $frame" >&2
        exit 1
    }
done
# Five identical frames would falsify the state-transition narrative even if
# a guest had printed the expected text. M173 also checks concrete pixel ROIs.
for pair in 'four five' 'five clicked' 'five closed' 'closed shell-after'; do
    first=${pair%% *}
    second=${pair#* }
    if cmp -s "$work/m173-$first.ppm" "$work/m173-$second.ppm"; then
        echo "[M175 IRQ FAIL] $port identical physical frames: $first and $second" >&2
        exit 1
    fi
done

# The marker is issued by the scheduler's real input consumer only after
# BOTH independently enumerated devices have ACKed their hardware IRQ, run a
# deferred drain, processed >8 events each, and performed ZERO fallback polls.
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    [ "$(grep -Fxc "$marker" "$log" || :)" -eq 1 ] || {
        echo "[M175 IRQ FAIL] $port guest IRQ-only marker absent/duplicate: $marker" >&2
        exit 1
    }
done < scripts/m175-expected-markers.txt

# Parse actual kernel counters rather than trusting the marker alone.
# Exactly one final line; extra/fabricated summaries also fail.
awk '
    /^\[M175 INPUT IRQ STATS\] / {
        n++
        for (i=5; i<=NF; i++) {
            parts=split($i, pair, "=")
            if (parts!=2 || pair[2]!~/^[0-9]+$/) bad=1
            val[pair[1]]=pair[2]+0
            seen[pair[1]]++
        }
    }
    END {
        if (n!=1 || bad) exit 1
        split("keyboard_ack pointer_ack keyboard_drain pointer_drain keyboard_events pointer_events fallback_poll", req, " ")
        for (i=1; i<=7; i++) if (seen[req[i]]!=1) exit 1
        if (val["keyboard_ack"]<1 || val["pointer_ack"]<1 ||
            val["keyboard_drain"]<1 || val["pointer_drain"]<1 ||
            val["keyboard_events"]<=8 || val["pointer_events"]<=8 ||
            val["fallback_poll"]!=0) exit 1
    }
' "$log" || {
    echo "[M175 IRQ FAIL] $port device ACK/drain/events/fallback counters invalid" >&2
    grep -a 'M175 INPUT IRQ' "$log" >&2 || :
    exit 1
}
# The native guest *and* external physical QMP input have now independently
# met their conditions; no skip, host simulation or another QEMU boot counted.
echo "[M175 IRQ NATIVE PASS] $port 20 signed original Alpine ELF entries, M174 devfs, five QEMU physical frames, keyboard+pointer >8 actions, real device ACK+deferred drain, fallback=0"
