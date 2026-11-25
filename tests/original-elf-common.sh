# Shared guest-side ELF identity gate. Host staging owns signature and ISA
# validation; this rejects shell wrappers and BusyBox multicall aliases.
# The caller supplies busy(), and selects its original diagnostic policy.
nas_original_elf_find() (
    diagnostic=$1; check_resolved=$2; empty_quiet=$3; shift 3
    if [ "$empty_quiet" = yes ] && [ "$#" -eq 0 ]; then return 1; fi
    for candidate do
        [ -x "$candidate" ] || continue
        resolved=$(busy readlink -f "$candidate") || continue
        if [ "$check_resolved" = yes ]; then [ -x "$resolved" ] || continue; fi
        case "$resolved" in /bin/busybox|/usr/bin/busybox) continue ;; esac
        magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
        if [ "$magic" = 7f454c46 ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    echo "$diagnostic $*" >&2
    return 1
)
nas_original_elf_one() (
    diagnostic=$1; candidate=$2
    [ -x "$candidate" ] || return 1
    resolved=$(busy readlink -f "$candidate") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || {
        if [ -n "$diagnostic" ]; then echo "$diagnostic $candidate" >&2; fi
        return 1
    }
    printf '%s\n' "$candidate"
)
