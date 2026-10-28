#!/bin/sh
# Compatibility with the initial x86-only GPT acceptance entrypoint.
# All three ISAs now use one original program/disk verification script.
set -eu
exec sh scripts/probe-vfs-gpt-busybox.sh amd64
