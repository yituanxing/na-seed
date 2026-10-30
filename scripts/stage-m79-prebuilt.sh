#!/bin/sh
# Single signed Alpine overlay; no guest compilation or duplicated install.
set -eu
[ "$#" -eq 1 ] || exit 2
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m79-workflow
echo "[M79 HOST STAGED] $1 original Bash + Vim + Git + Python native ELF"
