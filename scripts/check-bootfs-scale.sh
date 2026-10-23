#!/bin/sh
set -eu
mkdir -p build/tests
cc -std=c11 -O2 -Wall -Wextra -Werror -I kernel \
  scripts/test-bootfs-scale.c kernel/bootfs.c -o build/tests/bootfs-scale
build/tests/bootfs-scale
