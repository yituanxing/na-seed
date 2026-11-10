#!/bin/sh
# Generate Graphviz's upstream text plugin registry on the Linux HOST.
# Alpine APKs are signature-verified and installed --no-scripts for NaSeed;
# the original target plugin DSOs are present but Graphviz's post-install
# config6 generation is omitted.  No target ELF is executed or modified.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
target="$(pwd)/$root"
test -f "$target/usr/bin/dot"
test -d "$target/usr/lib/graphviz"

# Generate the official registry with original Alpine Graphviz inside the
# native x86-64 host container.  Plugin names and capabilities are portable
# textual metadata; validate every referenced DSO against the TARGET APK.
# This is host preparation only, NOT a NaSeed guest build or dot -c run.
docker run --rm --volume "$target:/target" alpine:3.23 sh -ec '
    apk --no-cache add graphviz >/dev/null
    /usr/bin/dot -c
    cfg=/usr/lib/graphviz/config6
    test -s "$cfg"
    grep -Fq "libgvplugin_core.so" "$cfg"
    grep -Fq "libgvplugin_dot_layout.so" "$cfg"
    grep -Fq "plain" "$cfg"
    cp "$cfg" /target/usr/lib/graphviz/config6
'

registry="$root/usr/lib/graphviz/config6"
test -s "$registry" || {
    echo "[M110 HOST GRAPHVIZ FAIL] $port no generated plugin registry" >&2
    exit 1
}
# Fail closed if the native host's Graphviz package exposes a plugin absent
# from the staged target package.  Never point guest config at a host-only DSO.
awk '/^libgvplugin_[[:alnum:]_]+\.so/ {print $1}' "$registry" |
while IFS= read -r plugin; do
    case "$plugin" in libgvplugin_*.so*) ;; *) exit 1 ;; esac
    test -e "$root/usr/lib/graphviz/$plugin" || {
        echo "[M110 HOST GRAPHVIZ FAIL] $port references missing target plugin $plugin" >&2
        exit 1
    }
done
echo "[M110 HOST GRAPHVIZ PASS] $port official host-generated Graphviz plugin metadata, target DSOs verified"
