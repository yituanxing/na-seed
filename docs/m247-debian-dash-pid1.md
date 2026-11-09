# M247: original Debian dash as direct NaSeed PID1

M247 extends M245 and M246 by making the original Debian 13 glibc dash
the actual **initial PID1** in one real NaSeed x86-64 QEMU guest.

Debian trixie dash is version 0.5.12-12. M247 reuses the M245 signed
InRelease / Packages index, pins that exact dash package and checks
the signed-record SHA256 and length before extraction. The original dash
ELF is validated for x86-64, PIE, the exact glibc PT_INTERP, and
DT_NEEDED containing only libc.so.6. Signed Debian original coreutils
and glibc ELF bytes are also compared after extraction from final CPIO.

The generic fixture includes static BusyBox only for the host harness
and rootfs staging convention. **BusyBox is never the selected PID1**.
The acceptance manifest explicitly selects /usr/bin/dash, running the
CPIO-resident /etc/m247-case.sh.

The script tests shell variable substitution, a shell conditional, output
redirection, a background pipeline with wait, and these original Debian
glibc coreutils: true, cat, wc and printf.

Only real native system-QEMU serial can PASS M247. Required evidence:

- Initial [M5] CPIO executable selected: /usr/bin/dash.
- Four distinct [EXEC] coreutils, plus at least five [DYN] interpreter
  loads including the initial PID1.
- Exact cat, byte-count, pipeline and final printf output.
- Real guest status zero, no kernel/application [FAIL].

Host Linux controls and negative tests are never guest PASS. M245/M246
remain independent success baselines. M247 does not prove a complete
Debian boot or general glibc compatibility.

Run: sh scripts/m247-probe-debian-dash-pid1.sh amd64.
