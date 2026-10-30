#!/bin/sh
# Linux oracle for the ownership of a blocked readv request: the imported
# user iovec and the open file are both pinned for the in-flight syscall.
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -O2 -Wall -Wextra -Werror -pthread \
  scripts/test-a014-linux-blocked-readv.c -o /tmp/naseed-a014-linux-blocked-readv

# Repeat the real thread races; a 5s per-process watchdog fails closed if
# a backend accidentally resumes on the re-bound, unwritten pipe.
for attempt in 1 2 3 4 5; do
    /tmp/naseed-a014-linux-blocked-readv
done
printf '[A014 LINUX OWNERSHIP ORACLE PASS] 5x iov and fd rebinding while readv blocked\n'
