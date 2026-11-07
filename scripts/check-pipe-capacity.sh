#!/bin/sh
set -eu
mkdir -p build/pipe-capacity
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    -I kernel scripts/test-pipe-capacity.c kernel/pipe.c kernel/mm/heap.c \
    kernel/lib/string.c -o build/pipe-capacity/test
build/pipe-capacity/test
cc -std=c11 -O2 -Wall -Wextra -Werror \
    scripts/test-pipe-fcntl-linux.c -o build/pipe-capacity/linux-oracle
build/pipe-capacity/linux-oracle
