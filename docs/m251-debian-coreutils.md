# M251 — seven additional original Debian/glibc utilities on real ext4

M251 extends the independently accepted M249/M250 signed Debian 13 baseline.
The target is a **native NaSeed kernel** running under a full system QEMU for
each of x86-64 (`amd64`), RV64 (`riscv64`) and AArch64 (`arm64`). The guest
root must be the **actual writable VirtIO block ext4 filesystem**, not a
staged CPIO filesystem, Linux `chroot`, or `qemu-user`.

## Signed binaries and dependency chain

The physical root retains byte-identical original Debian `dash`, `true`,
`cat`, `wc`, `printf`, glibc `libc.so.6`, and its original ABI loader from
M248/M249. M251 additionally stages the **original unmodified** Debian 13
`ls`, `head`, `sort`, `mkdir`, `rm`, `env`, and `sleep` from the already
authenticated `coreutils` package. Their dynamically required shared
libraries must be obtained through Debian's signature-verified `InRelease`
and SHA/size-authenticated package metadata, with recursive DT_NEEDED closure
and PT_INTERP verification for all three architectures. Unknown dependencies
are an error, not an invitation to link in unrelated host libraries.

The only non-Debian executable on the physical disk is the **explicitly
identified test-only** `/sbin/m250-tty-launch`: an architecture-native,
statically linked controller that obtains the real UART controlling terminal
and `execve()`-replaces itself with original `/usr/bin/dash -i` in PID1.
The separate rescue CPIO cannot contain the signed Debian binaries or the
controller. A CPIO shell cannot satisfy these checks.

## Real UART execution and durable filesystem

The host UART driver synchronizes on the *actual* interactive `M250> `
prompt, writes commands byte-by-byte into QEMU's serial FIFO, and requires
separate guest stdout lines that cannot be satisfied by terminal command
echo. The sequence checks:

1. `mkdir` on writable physical ext4, followed by original `printf` data.
2. `head` directly reading the file.
3. `sort | head` across a real Unix pipe.
4. `ls` enumerating the real created directory.
5. `env` setting and propagating an environment variable into another ELF.
6. `sleep 0` followed by a separate original `printf` success marker.
7. An original `printf` writing `M251-DURABLE\n` to
   `/var/tmp/m251-dir/result` on ext4.
8. `rm` unlinking the original input, verified again by `dash test`.
9. Clean `exit`, PID1 teardown, successful lwext4 unmount and durable flush.

The native test requires actual `[EXEC] replaced process with lwext4 ELF`
evidence for all seven new signed binaries and original `printf`, with
genuine interpreter loading and clean system-QEMU exit. After shutdown, the
host runs real `e2fsck` and reopens the disk using `debugfs`, checks the
original executable bytes and signed manifests, and reads back the new
persisted file content. It fails closed on any discrepancy.

## Reproduce

In a suitable Linux host with native QEMU, the proper cross-GCC and Debian
archive keyring, run any of:

```sh
sh scripts/m251-probe-debian-coreutils.sh amd64
sh scripts/m251-probe-debian-coreutils.sh riscv64
sh scripts/m251-probe-debian-coreutils.sh arm64
```

Or run the scoped GitHub Actions workflow:
`.github/workflows/m251-debian-coreutils-ext4.yml`.

**Evidence boundary:** host unit tests, signature verification alone, any
individual startup marker, and green CI without the actual native UART and
post-shutdown fsck/readback **do not count** as M251 guest acceptance.
M249/M250 baseline evidence and their original acceptance paths stay intact.
