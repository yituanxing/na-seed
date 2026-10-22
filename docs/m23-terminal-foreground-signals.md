# M23 — POSIX terminal-generated foreground group default signals

Three virtual ISA boards use exactly one unchanged Linux PID1 ELF and
one shared TTY/Signal/Task implementation. The byte *config* is the native
TCGETS/TCSETS termios, and N_TTY identifies VINTR (^C), VQUIT (^\),
VSUSP (^Z) only when ISIG is enabled. It resets incomplete canonical
input (NOFLSH=0). It defers actual process changes to a safe cooperative
scheduler boundary, not while hardware console is reading RX registers.

Signal delivery uses the *current controlling TTY foreground PGID*, not
the identity of the calling process or a global kill-all. Default SIGINT
and SIGQUIT terminate target processes via the existing per-task Linux
signal exit/reparent/fd-lifetime machinery and wait4 reports WIFSIGNALED
and exact WTERMSIG. SIGTSTP stops tasks using the existing STOPPED state;
wait4 reports actual WSTOPSIG=SIGTSTP (not hardcoded SIGSTOP), and real
SIGCONT resumes them. PID1 stays protected.

Native test creates a genuine leader and secondary member in a child
foreground PGID; both block on their real controlling TTY. Host sends
^C only after PID1 switches terminal foreground and announces readiness;
both must be reaped as SIGINT. A second real foreground process is stopped
by ^Z, observed through waitpid(WUNTRACED), resumed using SIGCONT,
observed using WCONTINUED, then cleaned up with SIGKILL. Each check
runs in physical QEMU COM1 / OpenSBI / PL011 on x86/RV64/ARM64.

Scope is explicitly default/ignored terminal actions. Arbitrary installed
user handlers, blocked pending signals, rt_sigreturn/user sigframe, and
timer-preemptive delivery are still unsupported. If either a custom
handler or a masked foreground member exists, group event delivery
returns unsupported rather than claiming a fake signal-frame delivery.
The existing idle serial scan still polls hardware; no real RX IRQ or
low-power idle is claimed.

All previous original Debian/Alpine binaries, source BusyBox 13/13,
eight-command interactive ash, forked TTY read and poll/ppoll tests
remain mandatory. No per-ISA duplicated TTY logic or bespoke syscalls.
