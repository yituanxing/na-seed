#!/bin/sh
# Real signed native Python, no host emulator or interpreter replacement.
set -eu
stage=version
trap 'rc=$?; if [ "$rc" -ne 0 ]; then echo "[M52 FAIL] stage=$stage rc=$rc" >&2; fi' EXIT
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
PYTHONHOME=/usr
PYTHONDONTWRITEBYTECODE=1
PYTHONUNBUFFERED=1
export PATH HOME LC_ALL PYTHONHOME PYTHONDONTWRITEBYTECODE PYTHONUNBUFFERED
case "$(/usr/bin/python3 --version)" in 'Python 3.'*) ;; *) exit 1 ;; esac
echo M52_ORIGINAL_SIGNED_PYTHON_PASS
stage=python-stdlib
/usr/bin/python3 -B /tests/m52-python-suite.py
echo M52_PYTHON_SUITE_PASS
