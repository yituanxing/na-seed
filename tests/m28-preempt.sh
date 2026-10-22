#!/bin/sh
# EXACTLY the same unmodified BusyBox ash script runs on host Linux and
# NaSeed. A producer writes one pipe readiness line and then executes an
# infinite pure-user builtin loop. The consumer cannot even be forked on
# NaSeed's child-first cooperative scheduler without a real timer IRQ.
# This program deliberately NEVER terminates: the test harness requires
# real output and IRQ telemetry, then explicitly stops QEMU/process group.
(printf 'ready\n'; while :; do :; done) | {
    IFS= read -r started
    [ "$started" = ready ] || exit 1
    echo cpu-hog-did-not-starve-ash
}
