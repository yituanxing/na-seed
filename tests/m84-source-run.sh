#!/bin/sh
# One original-software guest acceptance orchestrator, two independent apps.
# Each application owns its complete existing/original functional oracle.
set -eu
case "${1:-}" in
    --build)
        [ "$#" -eq 1 ] || exit 2
        /bin/sh /tests/m84-sqlite-run.sh --build
        /bin/sh /tests/m84-lua-run.sh --build
        printf 'NaSeed-M84-original-SQLite3-Lua-native-GCC-PASS\n' \
            > /mnt/m84-result.txt
        /bin/busybox sync
        echo M84_BUILD_PASS ;;
    --replay)
        [ "$#" -eq 1 ] || exit 2
        /bin/sh /tests/m84-sqlite-run.sh --replay
        /bin/sh /tests/m84-lua-run.sh --replay
        [ "$(/bin/busybox cat /mnt/m84-result.txt)" = \
          'NaSeed-M84-original-SQLite3-Lua-native-GCC-PASS' ]
        echo M84_REPLAY_PASS ;;
    *) echo 'usage: m84-source-run.sh --build|--replay' >&2; exit 2 ;;
esac
