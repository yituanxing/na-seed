#!/bin/sh
# Preserve the public H14 acceptance entry point.
set -eu
exec sh scripts/acceptance/stage-qemu-usb-alpine.sh h14
