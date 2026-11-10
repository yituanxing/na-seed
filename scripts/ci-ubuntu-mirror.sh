#!/bin/sh
# Choose a responsive, official Ubuntu archive for GitHub-hosted runners.
# Archive throughput varies drastically by region. Probe the signed release
# index over each mirror with a bounded transfer, then change ONLY its URI;
# apt-secure still authenticates InRelease and package hashes as before.
set -eu
best=''
best_rate=0
for candidate in \
    http://azure.archive.ubuntu.com/ubuntu \
    http://us.archive.ubuntu.com/ubuntu \
    http://archive.ubuntu.com/ubuntu; do
    rate=$(curl -fLsS --connect-timeout 3 --max-time 8 -o /dev/null \
        -w '%{speed_download}' "$candidate/dists/noble/InRelease" 2>/dev/null) || rate=''
    rate=${rate%%.*}
    case "$rate" in ''|*[!0-9]*) rate=0 ;; esac
    echo "[CI APT MIRROR] $candidate probe=${rate}B/s"
    if [ "$rate" -gt "$best_rate" ]; then
        best=$candidate
        best_rate=$rate
    fi
done
if [ -z "$best" ]; then
    echo '[CI APT MIRROR FAIL] no official Ubuntu archive answered bounded probe' >&2
    exit 1
fi
echo "[CI APT MIRROR] selected=$best rate=${best_rate}B/s"
for file in /etc/apt/apt-mirrors.txt /etc/apt/sources.list /etc/apt/sources.list.d/ubuntu.sources; do
    [ -f "$file" ] || continue
    sudo sed -i -E "s#https?://(azure\\.|us\\.)?archive\\.ubuntu\\.com/ubuntu#$best#g" "$file"
done
