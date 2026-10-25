#!/bin/sh
set -eu
test -x /usr/bin/file
/usr/bin/file --version > /tmp/m59-file-version
/bin/busybox grep -Fq file /tmp/m59-file-version
echo M59_ORIGINAL_FILE_PASS
/usr/bin/file -b /bin/busybox > /tmp/m59-file-elf
/bin/busybox grep -Fq ELF /tmp/m59-file-elf
echo M59_FILE_ELF_MAGIC_PASS
printf 'naseed plain text\n' > /tmp/m59-plain.txt
/usr/bin/file -b /tmp/m59-plain.txt > /tmp/m59-file-plain
/bin/busybox grep -Fq 'ASCII text' /tmp/m59-file-plain
echo M59_FILE_TEXT_MAGIC_PASS
