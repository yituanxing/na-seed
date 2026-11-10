#!/usr/bin/dash
# A guest-only service-shaped workload. This script is placed on the signed
# Debian physical ext4 root, then entered through the original dynamic dash.
# No host process executes it as a substitute for a NaSeed QEMU guest.
set -eu

/usr/bin/mkdir /var/tmp/m254-lifecycle
/usr/bin/printf 'alpha\nbeta\ngamma\n' > /var/tmp/m254-lifecycle/input

# Foreground shell -> asynchronous subshell -> two concurrent original Debian
# glibc ELF processes -> waitpid() -> exact result. A misplaced shell prompt,
# a lost wakeup or a broken pipe must prevent the success marker being written.
(
    /usr/bin/sed 's/^beta$/M254-PIPE-OK/' /var/tmp/m254-lifecycle/input |
        /usr/bin/grep -x M254-PIPE-OK > /var/tmp/m254-lifecycle/pipeline
) &
worker=$!
wait "$worker"
test -s /var/tmp/m254-lifecycle/pipeline

# Prove shell environment, command substitution, case, filesystem traversal
# and status propagation before setting the durable guest-created proof.
M254_TOKEN=M254-ENV-OK
export M254_TOKEN
# env prints NAME=value; this deliberately rejects an accidental token-only
# match and therefore checks the original signed env process output.
value=$(/usr/bin/env | /usr/bin/grep -x M254_TOKEN=M254-ENV-OK)
case "$value" in
    M254_TOKEN=M254-ENV-OK) : ;;
    *) exit 31 ;;
esac
found=$(/usr/bin/find /var/tmp/m254-lifecycle -type f -name pipeline)
test "$found" = /var/tmp/m254-lifecycle/pipeline

/usr/bin/printf '%s\n' M254-DURABLE > /var/tmp/m254-lifecycle/result
/usr/bin/printf '%s\n' M254-SCRIPT-PASS
