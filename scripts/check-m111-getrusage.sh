#!/bin/sh
# No guest compilation. Link real kernel process accounting into a host mock.
set -eu
mkdir -p build/m111-rusage
cc -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
   -fno-builtin -I kernel scripts/test-m111-getrusage.c \
   kernel/task/process.c -Wl,--gc-sections -o build/m111-rusage/host
build/m111-rusage/host
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only -I kernel kernel/task.c
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only -I kernel kernel/syscall/dispatch.c
