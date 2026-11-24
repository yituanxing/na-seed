#!/bin/sh
# Preserve the public H13 acceptance entry point.
set -eu
exec sh scripts/acceptance/build-qemu-usb-iso.sh h13
