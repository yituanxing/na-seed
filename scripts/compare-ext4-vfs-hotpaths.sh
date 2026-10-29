#!/bin/sh
# One real image per mode, identical pinned ext4/VFS harness and workload.
# Timings are observations, not CI pass thresholds; vendor iterator calls
# are independently counted by the original directory-entry API wrapper.
set -eu
baseline=$(sh scripts/bench-ext4-vfs-hotpaths.sh baseline)
optimized=$(sh scripts/bench-ext4-vfs-hotpaths.sh optimized)
printf '%s\n%s\n' "$baseline" "$optimized"
base_csv=$(printf '%s\n' "$baseline" |
  awk '/^\[EXT4 VFS HOTPATH CSV\]/ {print $5}')
opt_csv=$(printf '%s\n' "$optimized" |
  awk '/^\[EXT4 VFS HOTPATH CSV\]/ {print $5}')
test -n "$base_csv"
test -n "$opt_csv"
awk -v b="$base_csv" -v o="$opt_csv" 'BEGIN {
  nb=split(b, before, ","); no=split(o, after, ",");
  if(nb!=6 || no!=6 || before[1]!=after[1] ||
     before[2]!=after[2] || !after[3] || !after[4] ||
     !after[5] || !after[6])exit 2;
  printf "[EXT4 VFS HOTPATH IMPROVEMENT] ";
  printf "readdir_vendor_calls=%.2fx readdir_wall=%.2fx ",
         before[3]/after[3],before[4]/after[4];
  printf "deep_lookup_vendor_calls=%.2fx deep_lookup_wall=%.2fx\n",
         before[5]/after[5],before[6]/after[6];
}'
