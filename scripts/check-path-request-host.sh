#!/bin/sh
set -eu
mkdir -p build/path-request-host
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
  -ffunction-sections -fdata-sections \
  -I kernel -I scripts/m89-test-include \
  scripts/test-path-request-host.c kernel/fs/namespace_syscall.c \
  -Wl,--gc-sections -o build/path-request-host/test
build/path-request-host/test
