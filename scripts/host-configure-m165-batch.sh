#!/bin/sh
# Reuse M163's *identical* nonempty PNM oracle so codec measurements are
# comparable across batches. No second fixture algorithm or new ELF source.
set -eu
[ "$#" -eq 2 ] || exit 2
sh scripts/host-configure-m163-batch.sh "$1" "$2"
