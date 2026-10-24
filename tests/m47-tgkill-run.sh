#!/bin/sh
set -eu
/tests/m47-tgkill
/bin/sh /tests/run-zstd.sh
echo M47_SIGNALS_AND_ZSTD_PASS
