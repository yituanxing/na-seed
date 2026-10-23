#!/bin/sh
# Host-built freestanding three-ISA Linux ABI probe, then genuine Alpine zstd.
set -eu
/tests/m44-threadgroup
/bin/sh /tests/run-zstd.sh
echo M44_REAL_ZSTD_AND_GROUP_PASS
