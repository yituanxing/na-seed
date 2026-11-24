#!/bin/sh
# Host-only shell and fail-closed observer mechanics; never hardware proof.
set -eu
root=$(pwd -P)
for script in scripts/acceptance/stage-qemu-usb-alpine.sh \
              scripts/acceptance/build-qemu-usb-iso.sh \
              scripts/acceptance/qemu-negative-observer.sh \
              scripts/acceptance/run-local-alpine-cohort.sh \
              scripts/stage-h1[2-5]-*alpine.sh scripts/build-h1[2-5]-*iso.sh \
              scripts/check-h1[2-5]-no-*-qemu.sh scripts/run-m23[6-8]-local.sh; do
    sh -n "$script"
done
for script in stage-qemu-usb-alpine build-qemu-usb-iso run-local-alpine-cohort; do
    status=0
    sh "scripts/acceptance/$script.sh" invalid || status=$?
    [ "$status" -eq 2 ]
done
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir "$tmp/scripts"
cat > "$tmp/scripts/run.sh" <<'SH'
#!/bin/sh
set -eu
[ "$NAS_TEST_NEGATIVE" = 1 ]
printf '%s\r\n' "$1"
sleep 5
SH
for marker in '[USB TEST REJECT]' '[FAIL] mock fatal'; do
    (
        cd "$tmp"
        negative_flag=NAS_TEST_NEGATIVE
        image="$marker"
        expected='[USB TEST REJECT]'
        serial_log=raw.log
        clean=clean.log
        . "$root/scripts/acceptance/qemu-negative-observer.sh"
        grep -Fxq "$marker" "$clean"
        if [ "$marker" = "$expected" ]; then
            grep -Fxq "$expected" "$clean"
        else
            ! grep -Fxq "$expected" "$clean"
        fi
        [ -z "$qemu_pid" ] && [ -z "$observer_pid" ]
        case "$rc" in 0|1|124|143) ;; *) exit 1 ;; esac
    )
done
echo '[SHARED USB HOST PASS] shell syntax, rejected profile, observer and no false marker'
