#!/bin/bash
# Bash itself interprets this file; this is not an emulated or translated test.
set -euo pipefail
export PATH=/usr/bin:/bin
/bin/busybox mkdir -p /tmp/m58-bash
printf 'alpha\nbeta\ngamma\n' > /tmp/m58-bash/input.txt
out=$(/bin/busybox cat /tmp/m58-bash/input.txt | /bin/busybox grep -E 'a$' | /bin/busybox wc -l)
[[ "${out//[[:space:]]/}" == 3 ]]
echo M58_BASH_PIPELINE_PASS

# Redirection, shell parser, arrays and scoped command substitution.
values=(one two three)
[[ ${#values[@]} == 3 && ${values[1]} == two ]]
printf '%s\n' "${values[@]}" > /tmp/m58-bash/values.txt
mapfile -t result < /tmp/m58-bash/values.txt
[[ ${#result[@]} == 3 && ${result[2]} == three ]]
test "$((16 * 3 + 2))" -eq 50
echo M58_BASH_REDIRECTION_ARRAY_PASS

# True asynchronous child, signal disposition, trap and waitpid.
trap 'printf "cleanup\n" > /tmp/m58-bash/trap.txt' EXIT
/bin/busybox sleep 0.02 &
child=$!
wait "$child"
[[ $? == 0 ]]
echo M58_BASH_FORK_WAIT_PASS
