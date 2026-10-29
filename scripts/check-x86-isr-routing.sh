#!/bin/sh
set -eu
mkdir -p build/x86-isr-routing
cc -mno-red-zone -c kernel/arch/x86_64/entry.S \
    -o build/x86-isr-routing/entry.o
python3 scripts/test-x86-isr-routing.py build/x86-isr-routing/entry.o
