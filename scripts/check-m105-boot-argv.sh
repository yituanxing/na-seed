#!/bin/sh
# Compile production Limine argv parser, not a copied test-only parser.
set -eu
mkdir -p build/m105-boot-argv
cc -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -Wl,--gc-sections -I kernel \
    scripts/test-m105-boot-argv.c kernel/task/user_stack.c \
    -o build/m105-boot-argv/host
timeout 30s build/m105-boot-argv/host
