#!/bin/sh
set -eu
/tests/m45-robust
/bin/sh /tests/run-zstd.sh
echo M45_ROBUST_AND_ZSTD_PASS
