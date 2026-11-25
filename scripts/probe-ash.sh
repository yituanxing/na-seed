#!/bin/sh
# Archive-only BusyBox ash regression suites. One boot path; suite-specific
# assertions stay separate so process, pipe and pathname coverage is explicit.
set -eu

boot_case() {
    stage="build/$1-iso"
    iso="build/naseed-$1.iso"
    log="build/$1-serial.log"
    clean="build/$1-clean.log"
    rm -rf "$stage"
    cp -R build/alpine-archive-iso "$stage"
    awk -v args="$args" '/^    cmdline: / {
        print "    cmdline: naseed.argv=/bin/busybox " args
        next
    } { print }' limine-alpine-archive.conf > "$stage/boot/limine/limine.conf"
    xorriso -as mkisofs -R -r -J \
      --efi-boot boot/limine/limine-uefi-cd.bin \
      --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
    rc=0
    timeout "$2" sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
    if [ "$3" = clean ]; then
        tr -d '\r' < "$log" > "$clean"
    fi
}

# Discover missing *classes* of Linux process/shell semantics from the
# unmodified Alpine BusyBox ash on an archive-only NaSeed boot.
# Strict output acceptance for supported builtins; missing process features
# are recorded as explicit blockers, NEVER as accepted capabilities.
suite_diagnostic() {
    for scenario in builtin builtinpwd pid cd readfile cdread readtwice redirect applet external sequence pipeline; do
        case "$scenario" in
            builtin) args="sh -c 'echo shell-ready'" ;;
            builtinpwd) args="sh -c 'pwd'" ;;
            pid) marker=$(printf '\044\044'); args="sh -c 'echo $marker'" ;;
            cd) args="sh -c 'cd /etc; pwd'" ;;
            readfile) args="sh -c 'read word < /etc/hostname; echo \$word'" ;;
            cdread) args="sh -c 'cd /etc; read word < hostname; echo \$word'" ;;
            readtwice) args="sh -c 'exec 3< /etc/hostname; read first <&3; read second <&3; echo \$first:\$second'" ;;
            redirect) args="sh -c 'echo hello > /dev/null; echo redirect-ok'" ;;
            applet) args="sh -c 'cat /etc/hostname'" ;;
            external) args="sh -c '/bin/busybox cat /etc/hostname'" ;;
            sequence) args="sh -c '/bin/busybox cat /etc/hostname; echo finished'" ;;
            pipeline) args="sh -c 'echo abc | /bin/busybox wc -c'" ;;
        esac
        boot_case "ash-diag-$scenario" 4s raw
        echo "[ASH-DIAG] case=$scenario qemu_status=$rc args=$args"
        tr -d '\r' < "$log" | grep -E '^\[(MISS|FAULT|FAIL|EXIT|PASS)\]|^(shell-ready|naseed|redirect-ok|/etc|/|4)$' || true
        if ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$log"; then
            echo "[ASH-DIAG-FAIL] missing actual archive mount" >&2
            exit 1
        fi
        case "$scenario" in
            builtin) expected='shell-ready' ;;
            builtinpwd) expected='/' ;;
            pid) expected='1' ;;
            cd) expected='/etc' ;;
            readfile|cdread) expected='naseed' ;;
            readtwice) expected='naseed:' ;;
            applet) expected='naseed' ;;
            external) expected='naseed' ;;
            sequence) expected='finished' ;;
            pipeline) expected='4' ;;
            redirect) expected='redirect-ok' ;;
            *) expected='' ;;
        esac
        if [ -n "$expected" ]; then
            if [ "$rc" -ne 1 ] ||
               ! tr -d '\r' < "$log" | grep -Fxq "$expected" ||
               ! grep -Fq '[EXIT] user status=0' "$log" ||
               ! grep -Fq '[PASS] cmdline userspace exit 0' "$log"; then
                echo "[ASH-FAIL] real shell did not pass scenario=$scenario" >&2
                tail -85 "$log" >&2
                exit 1
            fi
            if [ "$scenario" = redirect ] &&
               tr -d '\r' < "$log" | grep -Fxq hello; then
                echo '[ASH-FAIL] redirected hello incorrectly reached stdout' >&2
                exit 1
            fi
            if [ "$scenario" = pipeline ] &&
               ! grep -Fq '[PIPE] created read=' "$log"; then
                echo '[ASH-FAIL] shell pipeline did not use kernel pipe endpoints' >&2
                tail -90 "$log" >&2
                exit 1
            fi
            if [ "$scenario" = sequence ] && {
                  ! tr -d '\r' < "$log" | grep -Fxq 'naseed' ||
                  ! grep -Fq '[PROC] fork parent=1 child=' "$log" ||
                  ! grep -Fq '[PROC] wait4 reaped pid=' "$log";
               }; then
                echo '[ASH-FAIL] no real child and wait4 continuation' >&2
                tail -90 "$log" >&2
                exit 1
            fi
            if [ "$scenario" = applet ] &&
               ! grep -Fq '[EXEC] replaced process with CPIO ELF: /bin/cat' "$log"; then
                echo '[ASH-FAIL] PATH applet did not exec via actual archive symlink' >&2
                tail -90 "$log" >&2
                exit 1
            fi
            if [ "$scenario" = external ] &&
               ! grep -Fq '[EXEC] replaced process with CPIO ELF: /bin/busybox' "$log"; then
                echo '[ASH-FAIL] external image was not replaced' >&2
                exit 1
            fi
            echo "[ACCEPTED] unmodified Alpine ash scenario=$scenario"
        fi
    done
}

# M5.4 acceptance: unmodified Alpine BusyBox ash calls Linux execve(59).
# No rewritten user binary, no boot-cmdline swap after entry, no fake kernel
# syscall result. The observed second program must print real CPIO contents.
suite_execve() {
    for scenario in cat echo execbuiltin missing; do
        case "$scenario" in
            cat) args="sh -c '/bin/busybox cat /etc/hostname'"
                 expect='naseed'
                 execpath='/bin/busybox'
                 good=yes ;;
            echo) args="sh -c '/bin/busybox echo exec-replaced'"
                 expect='exec-replaced'
                 execpath='/bin/busybox'
                 good=yes ;;
            execbuiltin) args="sh -c 'exec /bin/busybox cat /etc/hostname'"
                 expect='naseed'
                 execpath='/bin/busybox'
                 good=yes ;;
            missing) args="sh -c 'exec /bin/does-not-exist'"
                 expect='[EXIT] user status=127'
                 good=no ;;
        esac
        boot_case "ash-exec-$scenario" 10s clean
        if [ "$good" = yes ]; then
            if [ "$rc" -ne 1 ] ||
               ! grep -Fxq "$expect" "$clean" ||
               ! grep -Fq '[EXIT] user status=0' "$clean" ||
               ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean"; then
                echo "[FAIL] real ash execve scenario=$scenario" >&2
                tail -120 "$clean" >&2
                exit 1
            fi
            if ! grep -Fq "[EXEC] replaced process with CPIO ELF: $execpath" "$clean"; then
                echo '[FAIL] syscall execve did not replace the image' >&2
                tail -120 "$clean" >&2
                exit 1
            fi
        else
            # A real shell exec builtin exits 127 when Linux execve returns
            # ENOENT. NaSeed's diagnostic fail-stop for nonzero boot program
            # exit requires the timeout; it must NOT log image replacement.
            if [ "$rc" -ne 124 ] ||
               ! grep -Fq 'does-not-exist' "$clean" ||
               ! grep -Fq "$expect" "$clean" ||
               ! grep -Fq '[EXEC] Linux execve rejected errno=2' "$clean" ||
               ! grep -Fq '[FAIL] M1 userspace exited nonzero' "$clean" ||
               grep -Fq '[EXEC] replaced process with CPIO ELF:' "$clean" ||
               grep -Fq '[PASS] cmdline userspace exit 0' "$clean"; then
                echo '[FAIL] missing executable did not return real ENOENT to ash' >&2
                tail -120 "$clean" >&2
                exit 1
            fi
        fi
        echo "[ACCEPTED] original BusyBox ash real execve scenario=$scenario"
    done
}

# M7.1: real, pinned Alpine BusyBox ash exercising descriptor-vs-open-file
# ownership across dup, fork/exec, unlink/recreate and final-close recycling.
suite_fds() {
    for scenario in shared dup inherited unlink finalclose; do
        case "$scenario" in
            shared)
                args="sh -c 'echo abc > /tmp/f; exec 3< /tmp/f; cat <&3; cat <&3; echo shared-ok'"
                expect=shared-ok; payload=abc; once=abc ;;
            dup)
                args="sh -c 'echo abc > /tmp/f; exec 3< /tmp/f; exec 4<&3; cat <&4; cat <&3; echo dup-ok'"
                expect=dup-ok; payload=abc; once=abc ;;
            inherited)
                args="sh -c 'echo abc > /tmp/f; exec 3< /tmp/f; ( cat <&3 ); cat <&3; echo inherited-ok'"
                expect=inherited-ok; payload=abc; once=abc ;;
            unlink)
                args="sh -c 'echo original > /tmp/f; exec 3< /tmp/f; rm /tmp/f; echo replacement > /tmp/f; cat <&3; cat /tmp/f; echo unlink-ok'"
                expect=unlink-ok; payload=original; once=original ;;
            finalclose)
                args="sh -c 'echo abc > /tmp/f; exec 3< /tmp/f; exec 3<&-; rm /tmp/f; echo recycled > /tmp/f; cat /tmp/f; echo close-ok'"
                expect=close-ok; payload=recycled; once=recycled ;;
        esac
        boot_case "ash-fd-$scenario" 22s clean
        if [ "$rc" -ne 1 ] ||
           ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
           ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
           ! grep -Fxq "$expect" "$clean" ||
           ! grep -Fxq "$payload" "$clean" ||
           [ "$(grep -Fxc "$once" "$clean" || true)" -ne 1 ] ||
           grep -Fq '[FAULT]' "$clean" ||
           grep -Fq '[DEADLOCK]' "$clean"; then
            echo "[FAIL] original BusyBox descriptor scenario=$scenario rc=$rc" >&2
            tail -120 "$clean" >&2
            exit 1
        fi
        if [ "$scenario" = unlink ]; then
            grep -Fxq replacement "$clean" || {
                echo '[FAIL] recreated pathname did not get independent bytes' >&2
                exit 1
            }
        fi
        echo "[ACCEPTED] real BusyBox FD description lifetime scenario=$scenario"
    done
}

# Native upstream Alpine BusyBox ash must fork a real child, execute an ELF,
# wait4/reap its PID, and continue in the original parent process.
suite_fork() {
    for scenario in sequence repeat cwd; do
        case "$scenario" in
            sequence)
                args="sh -c '/bin/busybox cat /etc/hostname; echo finished'"
                expected='naseed finished'
                forks=1 ;;
            repeat)
                args="sh -c '/bin/busybox echo child-one; /bin/busybox echo child-two; echo parent-after'"
                expected='child-one child-two parent-after'
                forks=2 ;;
            cwd)
                args="sh -c 'cd /etc; /bin/busybox cat hostname; echo after-cwd'"
                expected='naseed after-cwd'
                forks=1 ;;
        esac
        boot_case "ash-fork-$scenario" 12s clean
        if [ "$rc" -ne 1 ] ||
           ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
           ! grep -Fq '[EXIT] user status=0' "$clean"; then
            echo "[FORK-FAIL] QEMU exit or shell result: $scenario" >&2
            tail -110 "$clean" >&2
            exit 1
        fi
        got_forks=$(grep -Fc '[PROC] fork parent=1 child=' "$clean" || true)
        got_waits=$(grep -Fc '[PROC] wait4 reaped pid=' "$clean" || true)
        if [ "$got_forks" -ne "$forks" ] || [ "$got_waits" -ne "$forks" ]; then
            echo "[FORK-FAIL] expected $forks forks+wait4; got $got_forks + $got_waits" >&2
            tail -110 "$clean" >&2
            exit 1
        fi
        for word in $expected; do
            if ! grep -Fxq "$word" "$clean"; then
                echo "[FORK-FAIL] $scenario missing user stdout $word" >&2
                tail -110 "$clean" >&2
                exit 1
            fi
        done
        echo "[ACCEPTED] real BusyBox ash fork/exec/wait4 scenario=$scenario"
    done
}

# Discover which real BusyBox ash producer/consumer lifecycle paths need
# signal or wakeup work before declaring a new milestone accepted.
suite_lifecycle() {
    for case_name in shorthead yeshead yeschain; do
      case "$case_name" in
        shorthead) args="sh -c '/bin/busybox cat /bin/busybox | /bin/busybox head -c 256 | /bin/busybox wc -c'" ;;
        yeshead) args="sh -c '/bin/busybox yes Y | /bin/busybox head -n 3; echo after-short'" ;;
        yeschain) args="sh -c '/bin/busybox yes ok | /bin/busybox head -n 3 | /bin/busybox wc -l; echo after-chain'" ;;
      esac
        boot_case "ash-life-$case_name" 12s clean
      if [ "$rc" -ne 1 ] ||
         ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
         ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
         ! grep -Fq '[SIGNAL] default SIGPIPE terminated pid=' "$clean" ||
         ! grep -Fq '[PROC] wait4 reaped pid=2 status=13' "$clean" ||
         grep -Fq '[DEADLOCK]' "$clean" ||
         grep -Fq '[FAULT]' "$clean"; then
        echo "[FAIL] early-closed pipe has no genuine SIGPIPE/reap: $case_name rc=$rc" >&2
        tail -95 "$clean" >&2
        exit 1
      fi
      case "$case_name" in
        shorthead)
          if ! grep -Fxq '256' "$clean"; then
            echo "[FAIL] truncated cat/head/wc did not transfer exactly 256 bytes" >&2
            tail -90 "$clean" >&2
            exit 1
          fi ;;
        yeshead)
          if [ "$(grep -Fxc 'Y' "$clean" || true)" -ne 3 ] ||
             ! grep -Fxq 'after-short' "$clean"; then
            echo "[FAIL] yes/head must emit exactly three Y lines and resume shell" >&2
            tail -90 "$clean" >&2
            exit 1
          fi ;;
        yeschain)
          if ! grep -Fxq '3' "$clean" ||
             ! grep -Fxq 'after-chain' "$clean"; then
            echo "[FAIL] yes/head/wc must count exactly three lines and resume shell" >&2
            tail -90 "$clean" >&2
            exit 1
          fi ;;
      esac
      echo "[ACCEPTED] real BusyBox $case_name SIGPIPE, Linux wait status 13, and shell continuation"
    done
}

# M7.2: unchanged Alpine BusyBox ash and symlink applets exercise the
# syscall path builder and CPIO component walk. No substituted test shell.
suite_paths() {
    for scenario in symlink-dotdot relative-cpio relative-tmp symlink-final parent-error; do
        case "$scenario" in
            symlink-dotdot)
                args="sh -c 'if cat /bin/cat/../busybox > /dev/null 2>&1; then echo wrong; else echo enotdir; fi; cat /etc/hostname'"
                expect=enotdir; also=naseed ;;
            relative-cpio)
                args="sh -c 'cd /etc && cat ../etc/hostname && echo archive-path-ok'"
                expect=archive-path-ok; also=naseed ;;
            relative-tmp)
                args="sh -c 'cd /tmp && mkdir d && cd d && echo scratch > ../file && cat ../file && echo tmp-path-ok'"
                expect=tmp-path-ok; also=scratch ;;
            symlink-final)
                args="sh -c 'readlink /bin/cat && cat /etc/hostname && echo link-final-ok'"
                expect=link-final-ok; also=busybox ;;
            parent-error)
                args="sh -c 'if cat /etc/hostname/../hostname > /dev/null 2>&1; then echo wrong; else echo notdir; fi; cat /etc/hostname'"
                expect=notdir; also=naseed ;;
        esac
        boot_case "ash-path-$scenario" 22s clean
        if [ "$rc" -ne 1 ] ||
           ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
           ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
           ! grep -Fxq "$expect" "$clean" ||
           ! grep -Fxq "$also" "$clean" ||
           grep -Fxq wrong "$clean" ||
           grep -Fq '[FAULT]' "$clean" ||
           grep -Fq '[DEADLOCK]' "$clean"; then
            echo "[FAIL] BusyBox pathname semantics scenario=$scenario rc=$rc" >&2
            tail -110 "$clean" >&2
            exit 1
        fi
        echo "[ACCEPTED] original BusyBox pathname scenario=$scenario"
    done
}

# Original pinned Alpine BusyBox ash exercising shared pipe descriptions,
# repeated allocation/close, read EOF and multi-stage pipeline scheduling.
# No fake BusyBox or kernel-side expected-output shortcuts.
suite_pipe_backend() {
    for scenario in reuse cascade readback; do
        case "$scenario" in
            reuse)
                args="sh -c 'for i in 1 2 3 4 5 6 7 8 9 10 11 12; do echo abc | wc -c; done; echo pipe-reuse-ok'"
                expected=pipe-reuse-ok; value=4; count=12 ;;
            cascade)
                args="sh -c 'echo abc | cat | wc -c; echo pipe-cascade-ok'"
                expected=pipe-cascade-ok; value=4; count=1 ;;
            readback)
                args="sh -c 'for i in 1 2 3 4 5 6 7 8; do echo line | cat > /tmp/message; cat /tmp/message; done; echo pipe-readback-ok'"
                expected=pipe-readback-ok; value=line; count=8 ;;
        esac
        boot_case "ash-pipe-backend-$scenario" 35s clean
        if [ "$rc" -ne 1 ] ||
           ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
           ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
           ! grep -Fxq "$expected" "$clean" ||
           [ "$(grep -Fxc "$value" "$clean" || true)" -ne "$count" ] ||
           grep -Fq '[FAULT]' "$clean" ||
           grep -Fq '[DEADLOCK]' "$clean"; then
            echo "[FAIL] real BusyBox pipe backend scenario=$scenario rc=$rc expected=$count" >&2
            tail -120 "$clean" >&2
            exit 1
        fi
        echo "[ACCEPTED] authentic BusyBox pipe backend scenario=$scenario"
    done
}

# Real unmodified BusyBox ash exercises Linux anonymous pipes and POSIX EOF.
suite_pipes() {
    for testcase in builtin external chain large largechain hash; do
      case "$testcase" in
        builtin)
          args="sh -c 'echo abc | /bin/busybox wc -c'"
          expected=4 ;;
        external)
          args="sh -c '/bin/busybox cat /etc/hostname | /bin/busybox wc -c'"
          expected=7 ;;
        chain)
          args="sh -c 'echo test | /bin/busybox cat | /bin/busybox wc -c'"
          expected=5 ;;
        large)
          args="sh -c '/bin/busybox cat /bin/busybox | /bin/busybox wc -c'"
          expected=$(wc -c < build/alpine/bin/busybox | tr -d ' ')
          if [ "$expected" -le 4096 ]; then
            echo '[FAIL] fixture smaller than pipe ring; not a blocking test' >&2
            exit 1
          fi ;;
        largechain)
          args="sh -c '/bin/busybox cat /bin/busybox | /bin/busybox cat | /bin/busybox wc -c'"
          expected=$(wc -c < build/alpine/bin/busybox | tr -d ' ') ;;
        hash)
          args="sh -c '/bin/busybox cat /bin/busybox | /bin/busybox sha256sum'"
          expected=$(sha256sum build/alpine/bin/busybox | cut -d ' ' -f 1) ;;
      esac
        boot_case "ash-pipe-$testcase" 25s clean
      if [ "$rc" -ne 1 ] ||
         ! { if [ "$testcase" = hash ]; then
                 grep -Fq "$expected" "$clean"
               else
                 grep -Fxq "$expected" "$clean"
               fi; } ||
         ! grep -Fq '[PIPE] created read=' "$clean" ||
         ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean"; then
        echo "[FAIL] real ash $testcase pipeline did not produce $expected" >&2
        tail -110 "$clean" >&2
        exit 1
      fi
      if [ "$testcase" = large ] || [ "$testcase" = largechain ] ||
         [ "$testcase" = hash ]; then
        if ! grep -Fq '[SCHED] blocked pid=' "$clean" ||
           ! grep -Fq '[SCHED] from=' "$clean"; then
          echo '[FAIL] large input completed without real block and task switch' >&2
          tail -90 "$clean" >&2
          exit 1
        fi
      fi
      echo "[ACCEPTED] unmodified ash $testcase pipeline output=$expected"
    done
}

# M6.9: genuine pinned Alpine BusyBox cp/mv, no modified applet code.
# Each ISO is fresh: exact stdout, shell status and QEMU exit are checked.
suite_rename() {
    for scenario in copy copylarge move replace pinned dirtree cwdmove noreplace readonly quota; do
        case "$scenario" in
            copy)
                args="sh -c 'cp /etc/hostname /tmp/copied && cat /tmp/copied'"
                expect=naseed; also=naseed ;;
            copylarge)
                args="sh -c 'yes X | head -c 8192 > /tmp/large; cp /tmp/large /tmp/other && wc -c < /tmp/other'"
                expect=8192; also=8192 ;;
            move)
                args="sh -c 'echo moved > /tmp/from; mv /tmp/from /tmp/to && cat /tmp/to && echo move-ok'"
                expect=move-ok; also=moved ;;
            replace)
                args="sh -c 'echo original > /tmp/one; echo old > /tmp/two; mv /tmp/one /tmp/two && cat /tmp/two && echo replace-ok'"
                expect=replace-ok; also=original ;;
            pinned)
                args="sh -c 'echo original > /tmp/one; echo former > /tmp/two; exec 3< /tmp/two; mv /tmp/one /tmp/two && cat <&3 && cat /tmp/two && echo pinned-ok'"
                expect=pinned-ok; also=former ;;
            dirtree)
                args="sh -c 'mkdir -p /tmp/tree/deep && echo nested > /tmp/tree/deep/file && mv /tmp/tree /tmp/newtree && cat /tmp/newtree/deep/file && rm -r /tmp/newtree && echo tree-ok'"
                expect=tree-ok; also=nested ;;
            cwdmove)
                args="sh -c 'mkdir -p /tmp/tree/deep; cd /tmp/tree/deep; mv /tmp/tree /tmp/newtree; pwd -P; cd /tmp; rm -r newtree; echo cwd-ok'"
                expect=cwd-ok; also=/tmp/newtree/deep ;;
            noreplace)
                args="sh -c 'echo keep > /tmp/to; echo incoming > /tmp/from; mv -n /tmp/from /tmp/to; cat /tmp/to; echo noreplace-ok'"
                expect=noreplace-ok; also=keep ;;
            readonly)
                args="sh -c 'echo keep > /tmp/from; mv /tmp/from /etc/target && echo wrong || echo readonly; cat /tmp/from; cat /etc/hostname'"
                expect=readonly; also=keep ;;
            quota)
                # A real build needs >8 simultaneous inodes; verify that the
                # ninth and sixteenth files succeed through a second slab.
                # Keep the historical quota scenario name for CI stability.
                args="sh -c 'for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do echo x > /tmp/file\$i || exit 1; done; cat /tmp/file1; cat /tmp/file16; echo grown; echo quota-ok'"
                expect=quota-ok; also=grown ;;
        esac
        boot_case "ash-rename-$scenario" 25s clean
        if [ "$rc" -ne 1 ] ||
           ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
           ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
           ! grep -Fxq "$expect" "$clean" ||
           ! grep -Fxq "$also" "$clean" ||
           grep -Fxq wrong "$clean" ||
           grep -Fq '[FAULT]' "$clean" ||
           grep -Fq '[DEADLOCK]' "$clean"; then
            echo "[FAIL] real BusyBox cp/mv scenario=$scenario rc=$rc expected=$expect/$also" >&2
            tail -110 "$clean" >&2
            exit 1
        fi
        case "$scenario" in
            copylarge)
                ! grep -Fxq 0 "$clean" || {
                    echo '[FAIL] large copy silently wrote zero bytes' >&2
                    exit 1
                } ;;
            readonly)
                grep -Fxq naseed "$clean" || {
                    echo '[FAIL] immutable CPIO data changed or inaccessible' >&2
                    exit 1
                } ;;
            pinned)
                grep -Fxq original "$clean" || {
                    echo '[FAIL] rename overwrite did not preserve source inode' >&2
                    exit 1
                } ;;
            quota)
                grep -Fxq x "$clean" || {
                    echo '[FAIL] demand-paged tmpfs inode slab lost the first or last file' >&2
                    exit 1
                } ;;
        esac
        echo "[ACCEPTED] unchanged BusyBox cp/mv inode scenario=$scenario"
    done
}

# M7.5: repeated actual Alpine BusyBox ash workloads test resource *reuse*
# across fork/wait4, syscall-boundary scheduling, shared FD descriptions,
# anonymous pipes, and the volatile inode tombstone pool.
suite_resources() {
    for scenario in pipeline scratch subshell tombstone sigpipe; do
        case "$scenario" in
            pipeline)
                args="sh -c 'for i in 1 2 3 4 5 6 7 8 9 10 11 12; do echo abc | cat | wc -c; done; echo lifecycle-pipeline-ok'"
                final=lifecycle-pipeline-ok; value=4; count=12 ;;
            scratch)
                args="sh -c 'for i in 1 2 3 4 5 6 7 8 9 10 11 12; do echo source > /tmp/item; cat /tmp/item | wc -c; rm /tmp/item; done; echo lifecycle-scratch-ok'"
                final=lifecycle-scratch-ok; value=7; count=12 ;;
            subshell)
                args="sh -c 'for i in 1 2 3 4 5 6 7 8 9 10 11 12; do ( echo child ) > /tmp/item; cat /tmp/item; rm /tmp/item; done; echo lifecycle-subshell-ok'"
                final=lifecycle-subshell-ok; value=child; count=12 ;;
            tombstone)
                args="sh -c 'for i in 1 2 3 4 5 6 7 8 9 10; do echo original > /tmp/item; exec 3< /tmp/item; rm /tmp/item; echo replacement > /tmp/item; cat <&3; exec 3<&-; rm /tmp/item; done; echo lifecycle-tombstone-ok'"
                final=lifecycle-tombstone-ok; value=original; count=10 ;;
            sigpipe)
                args="sh -c 'for i in 1 2 3 4 5 6 7 8; do yes X | head -c 2; done; echo lifecycle-sigpipe-ok'"
                final=lifecycle-sigpipe-ok; value=X; count=8 ;;
        esac
        boot_case "ash-resource-$scenario" 55s clean
        actual="$(grep -Fxc "$value" "$clean" || true)"
        if [ "$rc" -ne 1 ] ||
           ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
           ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
           ! grep -Fxq "$final" "$clean" ||
           [ "$actual" -ne "$count" ] ||
           grep -Fq '[FAULT]' "$clean" ||
           grep -Fq '[DEADLOCK]' "$clean"; then
            echo "[FAIL] real BusyBox repeated resource scenario=$scenario rc=$rc count=$actual expected=$count" >&2
            tail -160 "$clean" >&2
            exit 1
        fi
        echo "[ACCEPTED] original BusyBox resource reuse scenario=$scenario cycles=$count"
    done
}

# M6.6: real CPIO S_IFLNK entries, no artificial /bin/app command rewrites.
# Assert original BusyBox stdout, Linux execve, and pipe/scheduler behavior.
suite_symlinks() {
    for scenario in barecat smallpipe largepipe hash readlink lslink; do
        case "$scenario" in
            barecat) args="sh -c 'cat /etc/hostname'"
                     expected=naseed ;;
            smallpipe) args="sh -c 'cat /etc/hostname | wc -c'"
                       expected=7 ;;
            largepipe) args="sh -c 'cat /bin/busybox | wc -c'"
                       expected=$(wc -c < build/alpine/bin/busybox | tr -d ' ') ;;
            hash) args="sh -c 'cat /bin/busybox | sha256sum'"
                  expected=$(sha256sum build/alpine/bin/busybox | cut -d ' ' -f 1) ;;
            readlink) args="sh -c 'readlink /bin/cat'"
                      expected=busybox ;;
            lslink) args="sh -c 'ls -l /bin/cat'"
                    expected='cat -> busybox' ;;
        esac
        boot_case "ash-link-$scenario" 25s clean
        if [ "$rc" -ne 1 ] ||
           ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
           ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
           grep -Fq '[FAULT]' "$clean" ||
           grep -Fq '[DEADLOCK]' "$clean"; then
            echo "[FAIL] real bare BusyBox applet scenario=$scenario rc=$rc" >&2
            tail -100 "$clean" >&2
            exit 1
        fi
        if [ "$scenario" = hash ] || [ "$scenario" = lslink ]; then
            grep -Fq "$expected" "$clean" || {
                echo "[FAIL] $scenario expected $expected" >&2
                tail -100 "$clean" >&2
                exit 1
            }
        else
            grep -Fxq "$expected" "$clean" || {
                echo "[FAIL] $scenario expected exact stdout $expected" >&2
                tail -100 "$clean" >&2
                exit 1
            }
        fi
        if [ "$scenario" = largepipe ] || [ "$scenario" = hash ]; then
            grep -Fq '[SCHED] blocked pid=' "$clean" || {
                echo '[FAIL] large bare-app pipeline bypassed blocking' >&2
                exit 1
            }
        fi
        if [ "$scenario" = barecat ] &&
           ! grep -Fq '[EXEC] replaced process with CPIO ELF: /bin/cat' "$clean"; then
            echo '[FAIL] bare cat did not enter execve through a symlink' >&2
            exit 1
        fi
        echo "[ACCEPTED] authentic BusyBox ash bare-app $scenario"
    done
}

# M6.7 real BusyBox ash redirection: bounded volatile /tmp, original Alpine ELF.
# QEMU stdout, process exit, CPIO mount and errno are independent acceptance.
suite_tmp() {
    for scenario in create append truncate pipeline copy listing shared readonly; do
        case "$scenario" in
            create) args="sh -c 'echo scratch > /tmp/message; cat /tmp/message'"
                    expect=scratch ;;
            append) args="sh -c 'echo alpha > /tmp/log; echo beta >> /tmp/log; cat /tmp/log'"
                    expect=beta ;;
            truncate) args="sh -c 'echo old > /tmp/log; echo fresh > /tmp/log; cat /tmp/log'"
                      expect=fresh ;;
            pipeline) args="sh -c 'echo abc > /tmp/f; cat /tmp/f | wc -c'"
                      expect=4 ;;
            copy) args="sh -c 'echo persisted > /tmp/source; cat /tmp/source > /tmp/copy; cat /tmp/copy'"
                  expect=persisted ;;
            listing) args="sh -c 'echo data > /tmp/entry; ls /tmp'"
                     expect=entry ;;
            shared) args="sh -c 'echo abc > /tmp/entry; exec 3< /tmp/entry; read one <&3; read two <&3; echo \$one:\$two'"
                    expect=abc: ;;
            readonly) args="sh -c 'echo corrupt > /etc/hostname || echo readonly; cat /etc/hostname'"
                      expect=naseed ;;
        esac
        boot_case "ash-tmp-$scenario" 20s clean
        if [ "$rc" -ne 1 ] ||
           ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
           ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
           ! grep -Fxq "$expect" "$clean" ||
           grep -Fq '[FAULT]' "$clean" ||
           grep -Fq '[DEADLOCK]' "$clean"; then
            echo "[FAIL] BusyBox tmp/$scenario expected=$expect rc=$rc" >&2
            tail -105 "$clean" >&2
            exit 1
        fi
        case "$scenario" in
            append)
                grep -Fxq alpha "$clean" || exit 1
                [ "$(grep -Fxc alpha "$clean" || true)" -eq 1 ] || exit 1 ;;
            truncate)
                if grep -Fxq old "$clean"; then
                    echo '[FAIL] O_TRUNC left old bytes visible' >&2
                    exit 1
                fi ;;
            readonly)
                grep -Fxq readonly "$clean" || {
                    echo '[FAIL] CPIO unexpectedly writable' >&2
                    exit 1
                } ;;
            copy)
                grep -Fq '[PROC] fork parent=1 child=' "$clean" || {
                    echo '[FAIL] copy never executed a child process' >&2
                    exit 1
                } ;;
        esac
        echo "[ACCEPTED] unmodified BusyBox ash writable /tmp scenario=$scenario"
    done
}

# Real, unchanged Alpine BusyBox ash + mkdir/rm/rmdir/ls/cat; all QEMU boots.
# Acceptance requires output, zero shell status, and old CPIO kept read-only.
suite_tree() {
    for scenario in lifecycle nested cwd listing nonempty live readonly missing; do
        case "$scenario" in
            lifecycle)
                args="sh -c 'mkdir /tmp/work && echo hello > /tmp/work/test && cat /tmp/work/test && rm /tmp/work/test && rmdir /tmp/work && echo removed'"
                expect='removed'; also='hello' ;;
            nested)
                args="sh -c 'mkdir -p /tmp/work/deep && echo nested > /tmp/work/deep/file && cat /tmp/work/deep/file && rm -r /tmp/work && echo recursive-ok'"
                expect='recursive-ok'; also='nested' ;;
            cwd)
                args="sh -c 'mkdir /tmp/d && cd /tmp/d && echo cwd > file && cat file && cd /tmp && rm -r d && echo cwd-ok'"
                expect='cwd-ok'; also='cwd' ;;
            listing)
                args="sh -c 'mkdir /tmp/d && echo data > /tmp/d/entry && ls /tmp/d && rm -r /tmp/d && echo listed'"
                expect='listed'; also='entry' ;;
            nonempty)
                args="sh -c 'mkdir /tmp/d && echo busy > /tmp/d/f && if rmdir /tmp/d; then echo wrong; else echo nonempty; fi; rm /tmp/d/f && rmdir /tmp/d && echo empty-removed'"
                expect='empty-removed'; also='nonempty' ;;
            live)
                args="sh -c 'echo original > /tmp/a; exec 3< /tmp/a; rm /tmp/a && echo replacement > /tmp/a && cat <&3 && cat /tmp/a && echo live-ok'"
                expect='live-ok'; also='original' ;;
            readonly)
                args="sh -c 'mkdir /etc/newdir && echo wrong || echo readonly; rm /etc/hostname && echo wrong || echo readonly-rm; cat /etc/hostname'"
                expect='readonly-rm'; also='naseed' ;;
            missing)
                args="sh -c 'if rm /tmp/no-such-file; then echo wrong; else echo enoent; fi; echo missing-ok'"
                expect='missing-ok'; also='enoent' ;;
        esac
        boot_case "ash-tree-$scenario" 20s clean
        if [ "$rc" -ne 1 ] ||
           ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
           ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
           ! grep -Fxq "$expect" "$clean" ||
           ! grep -Fxq "$also" "$clean" ||
           grep -Fxq wrong "$clean" ||
           grep -Fq '[FAULT]' "$clean" ||
           grep -Fq '[DEADLOCK]' "$clean"; then
            echo "[FAIL] real BusyBox directory/lifetime case=$scenario rc=$rc" >&2
            tail -105 "$clean" >&2
            exit 1
        fi
        if [ "$scenario" = live ]; then
            grep -Fxq replacement "$clean" || {
                echo '[FAIL] unlink+recreate lost the new inode' >&2
                exit 1
            }
        fi
        echo "[ACCEPTED] real BusyBox tmp tree/lifecycle scenario=$scenario"
    done
}

[ "$#" -eq 1 ] || exit 2
case "$1" in
    diagnostic) suite_diagnostic ;;
    execve) suite_execve ;;
    fds) suite_fds ;;
    fork) suite_fork ;;
    lifecycle) suite_lifecycle ;;
    paths) suite_paths ;;
    pipe-backend) suite_pipe_backend ;;
    pipes) suite_pipes ;;
    rename) suite_rename ;;
    resources) suite_resources ;;
    symlinks) suite_symlinks ;;
    tmp) suite_tmp ;;
    tree) suite_tree ;;
    *) exit 2 ;;
esac
