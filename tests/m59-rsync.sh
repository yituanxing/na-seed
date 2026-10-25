#!/bin/sh
set -eu
test -x /usr/bin/rsync
/usr/bin/rsync --version > /tmp/m59-rsync-version
/bin/busybox grep -Fq rsync /tmp/m59-rsync-version
echo M59_ORIGINAL_RSYNC_PASS

/bin/busybox mkdir -p /tmp/m59-src/sub /tmp/m59-dst
printf 'alpha-1\n' > /tmp/m59-src/sub/data.txt
printf 'delete-me\n' > /tmp/m59-src/old.txt
/bin/busybox ln -s sub/data.txt /tmp/m59-src/current
/usr/bin/rsync -a /tmp/m59-src/ /tmp/m59-dst/
/bin/busybox cmp -s /tmp/m59-src/sub/data.txt /tmp/m59-dst/sub/data.txt
test -L /tmp/m59-dst/current
test "$(/bin/busybox readlink /tmp/m59-dst/current)" = sub/data.txt
echo M59_RSYNC_TREE_PASS

# No synthetic rsync: the original program must detect that no data changed.
/usr/bin/rsync -a --itemize-changes /tmp/m59-src/ /tmp/m59-dst/ > /tmp/m59-rsync-noop
test ! -s /tmp/m59-rsync-noop
echo M59_RSYNC_INCREMENTAL_PASS

printf 'alpha-2\n' > /tmp/m59-src/sub/data.txt
/bin/busybox rm /tmp/m59-src/old.txt
/usr/bin/rsync -a --delete --checksum /tmp/m59-src/ /tmp/m59-dst/
/bin/busybox cmp -s /tmp/m59-src/sub/data.txt /tmp/m59-dst/sub/data.txt
test ! -e /tmp/m59-dst/old.txt
echo M59_RSYNC_DELETE_PASS
