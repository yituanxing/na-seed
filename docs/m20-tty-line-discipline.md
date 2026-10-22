# M20: native N_TTY line-discipline separation

TTY core owns Linux TCGETS/TCSETS configuration, owning session and
foreground process group, physical UART selection and protected usercopy.
The new portable freestanding tty_ldisc.c implements the N_TTY-style
byte policy independent of x86 COM1, RV SBI and ARM PL011.

Covered: ICRNL/INLCR/IGNCR, ICANON line-delimited input with partial
read preservation, ECHO, VERASE (DEL), VKILL (^U), VWERASE (^W),
VEOF (^D) including nonempty lines, and noncanonical VMIN with
VTIME=0. TCSETSF flushes the line queue; TCSETS doesn't.
OPOST/ONLCR output is now routed through tty core, not fs syscall.

Explicit limits: timed VTIME>0 requires clock/event scheduling and
returns EOPNOTSUPP. ISIG VINTR/VQUIT/VSUSP cannot pretend to deliver
real signals and return EOPNOTSUPP. UART read still uses the existing
synchronous polling loop; no claimed IRQ scheduler wake. UTF-8 erase,
IXON/IXOFF, literal-next and full Linux N_TTY not yet covered.

Verification: preflight executes tests/test-tty-ldisc.c directly against
the SAME freestanding source as the three QEMU kernels. The existing
real source BusyBox ash -i UART driver adds DEL, ^U, and ^W input
sequences and checks executable output for all x86, RV64 and ARM64.
Existing original Debian, Alpine and 13 source workload matrices stay
untouched.
