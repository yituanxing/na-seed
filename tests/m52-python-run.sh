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
# Diagnostic: distinguish absent CPIO runtime files from VFS/import ABI.
for f in /usr/lib/python3.12/encodings/__init__.py \
         /usr/lib/python3.12/encodings/utf_8.py \
         /usr/lib/python3.12/codecs.py; do
    if [ ! -s "$f" ]; then echo "[M52 ERROR] guest cannot read $f" >&2; exit 1; fi
done
echo M52_GUEST_STDLIB_FILES_VISIBLE
PYTHONVERBOSE=1 PYTHONPYCACHEPREFIX=/tmp/m52-source-cache /usr/bin/python3 -v -B /tests/m52-python-suite.py
echo M52_PYTHON_SUITE_PASS
