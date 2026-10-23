#!/bin/sh
# Genuine GNU tar + XZ Utils ELF from signed Alpine. No guest compiler.
set -eu
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH
echo M40_ARCHIVE_START
if [ -x /usr/bin/tar ]; then tarcmd=/usr/bin/tar; else tarcmd=/bin/tar; fi
if [ -x /usr/bin/xz ]; then xzcmd=/usr/bin/xz; else xzcmd=/bin/xz; fi
"$tarcmd" --version | /bin/busybox grep -Fq 'GNU tar'
"$xzcmd" --version | /bin/busybox grep -Fq 'XZ Utils'
# CPIO source can be traversed directly, archive output is a genuine
# regular file in the page-backed tmpfs, not a simulated tar record.
"$tarcmd" -cf /tmp/m40-sources.tar -C /tests/archive-seed .
test -s /tmp/m40-sources.tar
"$tarcmd" -tf /tmp/m40-sources.tar | /bin/busybox grep -Fxq './nested/beta.txt'
echo M40_TAR_CREATE_PASS

# Use the real XZ decoder (and re-encode) on the complete binary tar file.
# The kernel currently has a single runnable hardware CPU and no POSIX
# userspace threads; explicitly select XZ's genuine SINGLE-THREADED codec.
# Testing implicit auto-threading would incorrectly test pthread_clone first.
"$xzcmd" --threads=1 -0 -c /tmp/m40-sources.tar > /tmp/m40-sources.tar.xz
test -s /tmp/m40-sources.tar.xz
"$xzcmd" --threads=1 -dc /tmp/m40-sources.tar.xz > /tmp/m40-restored.tar
/bin/busybox cmp /tmp/m40-sources.tar /tmp/m40-restored.tar
echo M40_XZ_PASS

/bin/busybox mkdir /tmp/m40-unpacked
"$tarcmd" --no-same-owner --no-same-permissions \
    -xf /tmp/m40-restored.tar -C /tmp/m40-unpacked
/bin/busybox cmp /tests/archive-seed/alpha.txt /tmp/m40-unpacked/alpha.txt
/bin/busybox cmp /tests/archive-seed/nested/beta.txt /tmp/m40-unpacked/nested/beta.txt
# Exercise directories, file metadata, and tmpfs name lookup after writes.
/bin/busybox test -s /tmp/m40-unpacked/nested/beta.txt
echo M40_TAR_EXTRACT_PASS
echo M40_ARCHIVES_PASS
