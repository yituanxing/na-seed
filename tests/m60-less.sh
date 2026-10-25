#!/bin/sh
set -eu
test -x /usr/bin/less
/usr/bin/less --version > /tmp/m60-less-version
/bin/busybox grep -Fq 'less ' /tmp/m60-less-version
echo M60_ORIGINAL_LESS_PASS

# Real pager consumes a file with an overlong line and multiple pages. With
# redirected stdout its native filter mode must preserve the entire input.
printf 'line one\n\tindented line two\n' > /tmp/m60-less-input
/bin/busybox seq 1 80 >> /tmp/m60-less-input
/usr/bin/less /tmp/m60-less-input > /tmp/m60-less-output
/bin/busybox cmp -s /tmp/m60-less-input /tmp/m60-less-output
echo M60_LESS_FILE_FILTER_PASS

# Test stdin streaming independently; this should not assume a fake terminal.
/usr/bin/less < /tmp/m60-less-input > /tmp/m60-less-stream
/bin/busybox cmp -s /tmp/m60-less-input /tmp/m60-less-stream
echo M60_LESS_STDIN_STREAM_PASS
