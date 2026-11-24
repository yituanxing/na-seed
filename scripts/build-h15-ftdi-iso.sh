#!/bin/sh
# Preserve the public H15 acceptance entry point.
set -eu
exec sh scripts/acceptance/build-qemu-usb-iso.sh h15
