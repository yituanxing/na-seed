#!/bin/sh
# Materialize the single-case native batch contract, never infer guest success.
set -eu
[ "$#" -eq 1 ] || exit 2
profile=$1
label=$(printf '%s' "$profile" | tr '[:lower:]' '[:upper:]')
entry=/bin/busybox
marker=${label}_BATCH_PASS
case "$profile" in
    m80|m10[9]|m11[0-9]|m12[0-9]|m13[0-9]|m140|m141|m146|m148|m150|m151|m153|m154|m155|m157|m158|m159)
        args="sh -c '/bin/sh /tests/$profile-batch-run.sh'" ;;
    m160) args="sh -c '/bin/sh /tests/$profile-batch-run.sh'"; marker=M160_WIRE_IMAGE_PASS ;;
    m161|m162|m163|m165)
        args="ash /tests/$profile-batch-run.sh"
        case "$profile" in m161) marker=M161_FRAMEBUFFER_PASS ;; m162) marker=M162_NETWORK_SCREEN_PASS ;; esac ;;
    m164) args=m164; marker=M164_INTERACTIVE_PASS; entry=/tests/m164-input-display ;;
    m166) args=m166; marker=M166_REAL_EVDEV_DISPLAY_PASS; entry=/tests/m166-input-display ;;
    m167|m168|m170|m171|m173) args=$profile; marker=${label}_DESKTOP_PASS; entry=/usr/bin/naseed-desktop ;;
    m174) args=m174; marker=M173_DESKTOP_PASS; entry=/usr/bin/naseed-m174-devfs-launch ;;
    *) echo "unsupported native batch profile: $profile" >&2; exit 2 ;;
esac
# Same six-column record consumed by all three existing native runners.
# Atomic replacement avoids partially written input in parallel invocations.
dir=build/acceptance-manifests
mkdir -p "$dir"
file=$dir/$profile.tsv
tmp=$(mktemp "$dir/.$profile.XXXXXX")
trap 'rm -f "$tmp"' EXIT
printf '# id mode args expected evidence entry\n%s-batch\tcpio\t%s\t%s\t-\t%s\n' "$profile" "$args" "$marker" "$entry" > "$tmp"
mv -f "$tmp" "$file"
printf '%s\n' "$file"
