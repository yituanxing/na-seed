#!/bin/sh
# Preserve the public H15 acceptance entry point.
set -eu
exec sh scripts/acceptance/stage-qemu-usb-alpine.sh h15
