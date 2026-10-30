#!/bin/sh
# One native QEMU boot per ISA; all four genuine programs cooperate on one
# writable tmpfs tree. Validate guest-owned output, not a fabricated marker.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m79-prebuilt.sh "$port"
work="build/external/m79-workflow/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m79-workflow-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export NAS_SYSCALL_TRACE=0
if ! sh "$runner"; then
 log="$logs/m79-workflow.clean.log"
 echo "[M79 FAIL] $port authentic four-program guest workflow" >&2
 test ! -s "$log" || tail -320 "$log" >&2
 exit 1
fi
log="$logs/m79-workflow.clean.log"
test -s "$log"
for marker in M79_ORIGINAL_TOOLS_PASS M79_BASH_PIPE_BACKGROUND_PASS \
              M79_VIM_GIT_DIFF_PASS M79_PYTHON_JSON_SQLITE_PASS \
              M79_GIT_COMMIT_RECHECK_PASS M79_FOUR_ORIGINAL_PROGRAMS_PASS; do
 if ! grep -Fxq "$marker" "$log"; then
  echo "[M79 FAIL] $port missing authentic guest stage $marker" >&2
  tail -320 "$log" >&2
  exit 1
 fi
done
echo "[M79 PASS] $port actual Bash/Vim/Git/Python end-to-end guest workflow"
