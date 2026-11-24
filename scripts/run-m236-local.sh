#!/bin/sh
# Host-only default and explicit native opt-in are enforced by shared runner.
set -eu
exec sh scripts/acceptance/run-local-alpine-cohort.sh m236 "$@"
