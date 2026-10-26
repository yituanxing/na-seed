#!/bin/sh
set -eu
mkdir -p build/m88-exceptions
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
    scripts/test-m88-exceptions.c kernel/exception/exception.c \
    kernel/mm/fault.c kernel/mm/anon_regions.c \
    -o build/m88-exceptions/check
build/m88-exceptions/check
