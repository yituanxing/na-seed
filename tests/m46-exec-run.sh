#!/bin/sh
set -eu
/tests/m46-exec
/bin/sh /tests/run-zstd.sh
echo M46_EXEC_AND_ZSTD_PASS
