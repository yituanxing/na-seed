#!/bin/sh
# M108: production tmpfs inode+namespace scale, Linux-like inode lifetime.
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -O2 -Wall -Wextra -Werror \
  scripts/test-a015-linux-truncate-time.c \
  -o /tmp/naseed-a015-linux-truncate-time
/tmp/naseed-a015-linux-truncate-time
cc -std=c11 -O2 -Wall -Wextra -Werror \
  scripts/test-a005-linux-mmap-hole.c \
  -o /tmp/naseed-a005-linux-mmap-hole
/tmp/naseed-a005-linux-mmap-hole
cc -std=c11 -O2 -Wall -Wextra -Werror -I kernel \
  scripts/test-m108-tmp-index.c kernel/tmpfs.c kernel/fs/tmp_namespace.c \
  -o /tmp/naseed-m108-tmp-index
/tmp/naseed-m108-tmp-index
cc -std=c11 -O1 -g -Wall -Wextra -Werror -I kernel \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  scripts/test-m108-tmp-index.c kernel/tmpfs.c kernel/fs/tmp_namespace.c \
  -o /tmp/naseed-m108-tmp-index-asan
# LeakSanitizer cannot discover mock fixed physical-frame ownership, whose
# lifetime intentionally models a powered-on kernel, not a host allocator.
ASAN_OPTIONS=detect_leaks=0 /tmp/naseed-m108-tmp-index-asan
printf '[M108 HOST PASS]\n'
