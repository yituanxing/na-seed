# Run using the original signed /usr/bin/tclsh, without guest-side build.
if {[package require Tcl] eq ""} {error "Tcl runtime missing"}
set p /tmp/m58-tcl.txt
set f [open $p w]
puts $f "naseed-tcl-original"
close $f
set f [open $p r]
set content [gets $f]
close $f
if {$content ne "naseed-tcl-original"} {error "Tcl file roundtrip"}
file rename -force $p /tmp/m58-tcl-renamed.txt
if {![file exists /tmp/m58-tcl-renamed.txt]} {error "Tcl rename"}
puts M58_TCL_FILE_PASS

# Real pipe/exec/wait through Tcl's runtime, with BusyBox as the child ELF.
set uppercase [exec /bin/busybox echo "naseed-tcl-pipe" | /bin/busybox tr a-z A-Z]
if {$uppercase ne "NASEED-TCL-PIPE"} {error "Tcl exec/pipe: $uppercase"}
puts M58_TCL_EXEC_PIPE_PASS

# Exercises target libc time + notifier rather than a mocked delay.
after 10 {set ::m58_timer 1}
vwait ::m58_timer
if {$::m58_timer != 1} {error "Tcl event timer"}
puts M58_TCL_TIMER_PASS
puts M58_TCL_FULL_PASS
