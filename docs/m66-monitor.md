# M66 — Native original Linux top and htop

Extend M65's Task-backed procfs with `/proc/stat`, `loadavg`,
`vmstat`, PID `statm` and exact argv captured after successful exec.
The same 43 common C translation units build on all three native ISAs.

- Task/accounting.c records real monotonic time samples at dispatch and
  scheduler switches, and distinguishes no-runnable-task idle. Process
  runtime is *coarse Task residency* including kernel syscall time, not
  cycle-accurate user/kernel hardware performance statistics.
- Load average is real variable-interval EMA over runnable Task depth;
  it is not a hardcoded constant.
- `/proc/stat btime` is derived from validated firmware UTC and monotonic
  clock anchors. Never invent it if RTC is unavailable.
- PID `statm` is deliberately partial: anonymous regions and initial
  stack only. Resident pages and ELF mappings are not completely tracked;
  a reported zero RSS is *unmeasured*, not proof of zero memory used.
- Command lines are retained from successful exec after shebang expansion
  and inherited by fork; vectors too large for the bounded snapshot
  are omitted rather than misrepresented.

Real acceptance: host stages original signed Alpine `top`, `uptime`,
`htop` and `tmux`. Genuine native QEMU exercises batch top and an
interactive htop view inside a real tmux-managed PTY. Exact-head existing
M38–M65 CI must stay green. No guest-side compiler, per-app syscall hacks
or fake success markers. The test prints independent blockers before failure.
