#!/bin/sh
# Preserve the public H14 acceptance entry point.
set -eu
exec sh scripts/acceptance/build-qemu-usb-iso.sh h14
