#!/bin/sh
set -eu
task_dir="$(mktemp -d)"
trap 'rm -f "$task_dir/h0-acpi-host"; rmdir "$task_dir"' EXIT HUP INT TERM
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
  tests/h0-acpi-host.c kernel/platform/firmware_acpi.c \
  -o "$task_dir/h0-acpi-host"
"$task_dir/h0-acpi-host"
