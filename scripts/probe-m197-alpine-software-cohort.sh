#!/bin/sh
# Real original Alpine PID1 on physical ext4: direct public HTTPS one-transaction
# install of Bash/Lua/SQLite/Git, integrated work and independent second QEMU.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: sh scripts/probe-m197-alpine-software-cohort.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in
    amd64) alpine_arch=x86_64 ;;
    riscv64) alpine_arch=riscv64 ;;
    arm64) alpine_arch=aarch64 ;;
    *) exit 2 ;;
esac
. scripts/native-qemu-target.sh

# M198 reuses THIS full public-Alpine workload and the same disposable disk.
# It adds already-established VirtIO GPU/input + userspace desktop, without
# another distro image, PID1, graphical kernel ABI or software-test pipeline.
m198=${NAS_M198_INTEGRATED_DESKTOP:-0}
case "$m198" in 0|1) ;; *) echo '[M198 FAIL] invalid desktop selector' >&2; exit 2 ;; esac
m199=${NAS_M199_ORIGINAL_OPENRC_DESKTOP:-0}
case "$m199" in 0|1) ;; *) echo '[M199 FAIL] invalid original OpenRC selector' >&2; exit 2 ;; esac
m204=${NAS_M204_COMPAT_COHORT:-0}
case "$m204" in 0|1) ;; *) echo '[M204 FAIL] invalid compatibility selector' >&2; exit 2 ;; esac
m205=${NAS_M205_COMPAT_COHORT:-0}
case "$m205" in 0|1) ;; *) echo '[M205 FAIL] invalid compatibility selector' >&2; exit 2 ;; esac
m206=${NAS_M206_COMPAT_COHORT:-0}
case "$m206" in 0|1) ;; *) echo '[M206 FAIL] invalid compatibility selector' >&2; exit 2 ;; esac
m215=${NAS_M215_PILOT:-0}
case "$m215" in 0|1) ;; *) echo '[M215 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
m218=${NAS_M218_PILOT:-0}
case "$m218" in 0|1) ;; *) echo '[M218 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
m219=${NAS_M219_PILOT:-0}
case "$m219" in 0|1) ;; *) echo '[M219 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
m220=${NAS_M220_PILOT:-0}
case "$m220" in 0|1) ;; *) echo '[M220 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
m221=${NAS_M221_PILOT:-0}
case "$m221" in 0|1) ;; *) echo '[M221 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
m222=${NAS_M222_PILOT:-0}
case "$m222" in 0|1) ;; *) echo '[M222 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
m223=${NAS_M223_PILOT:-0}
case "$m223" in 0|1) ;; *) echo '[M223 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
if { [ "$m204" = 1 ] || [ "$m205" = 1 ] || [ "$m206" = 1 ] || [ "$m215" = 1 ] || [ "$m218" = 1 ] || [ "$m219" = 1 ] || [ "$m220" = 1 ] || [ "$m221" = 1 ] || [ "$m222" = 1 ] || [ "$m223" = 1 ]; } && \
   { [ "$m198" = 1 ] || [ "$m199" = 1 ]; }; then
    echo '[COMPAT FAIL] software cohort cannot enable a desktop' >&2
    exit 2
fi
if [ "$((m204+m205+m206+m215+m218+m219+m220+m221+m222+m223))" -gt 1 ]; then
    echo '[COMPAT FAIL] software cohorts must own separate ledgers' >&2
    exit 2
fi
if [ "$m199" = 1 ]; then
    # OpenRC runs the exact M198 graphics and original Alpine software; only
    # its lifecycle and *second genuine Alpine PID1 boot* differ.
    m198=1
fi
if [ "$m198" = 1 ]; then
    # Must be set BEFORE M191 builds the native kernel on all three ISAs.
    export NAS_ENABLE_VIRTIO_INPUT=1 NAS_ENABLE_VIRTIO_GPU=1
    export NAS_M191_BUILD_TAG=m198-root
fi

export NAS_ENABLE_VIRTIO_NET=1 NAS_VIRTIO_NET_USER=1 NAS_ENABLE_LWIP=1
unset NAS_SYSCALL_TRACE
sh scripts/probe-m191-alpine-root.sh "$port"
work="build/external/m191-alpine-rootfs/$port"
root="$PWD/$work/rootfs"
baseline="$PWD/$work/alpine-root.img"
rescue="$PWD/$work/smoke/rescue/rootfs.cpio"
busybox="$PWD/$work/smoke/elf/busybox"
test -s "$baseline" && test -s "$rescue" && test -s "$busybox"
disk_mib=256
if [ "$m205" = 1 ] || [ "$m206" = 1 ]; then
    # 20 additional originals include large LLVM21 dependencies; keep the
    # canonical M191 256MiB baseline immutable and enlarge only the cohort's
    # disposable guest disk, with matching geometry at prepare and fsck.
    disk_mib=768
fi
if [ "$m215" = 1 ] || [ "$m218" = 1 ] || [ "$m219" = 1 ] || [ "$m220" = 1 ] || [ "$m221" = 1 ] || [ "$m222" = 1 ] || [ "$m223" = 1 ]; then
    # Small exact-owner pilots keep an isolated physical ext4 disk; never
    # carry M213's unrelated LLVM/BIND/SSH discovery pool closure.
    disk_mib=512
fi

# A prior *verified cold public two-boot run with clean final unmount* can
# accelerate ABI iteration, but that separate third-boot replay is
# never current-commit public HTTPS/two-boot acceptance. Default always cold.
disk="$PWD/$work/m197-software-disposable.img"
if [ "$m222" = 1 ]; then
    # M142's genuine temporary host OpenSSH daemon, now phase-unique and
    # bound to the M197 2×QEMU signed physical-ext4 owner. The EXIT trap
    # removes only identified ephemeral SSH secrets; never baseline media.
    . scripts/m222-ssh-host.sh
    trap 'm222_cleanup' EXIT
    # Convert interruption to a normal nonzero exit so EXIT scrubs both
    # transient private identities and the exact disposable ext4 image.
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
fi
if [ "$m223" = 1 ]; then
    # Actual host-only random HTTP witnesses stay outside signed Alpine
    # staging. EXIT always kills this exact independent HTTP server.
    . scripts/m223-http-host.sh
    trap 'm223_cleanup' EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
fi
checkpoint="$PWD/$work/m204-first-checkpoint.img"
checkpoint_meta="$PWD/$work/m204-first-checkpoint.json"
checkpoint_serial="$PWD/$work/m204-first-checkpoint.serial.log"
checkpoint_pcap="$PWD/$work/m204-first-checkpoint.pcap"
checkpoint_reopen="$PWD/$work/m204-first-checkpoint.reopen.serial.log"
replay=0
case "${NAS_M204_FAST_REPLAY:-0}" in 0|1) ;; *) echo '[M204 FAIL] invalid replay flag' >&2; exit 2 ;; esac
case "${NAS_M204_CAPTURE_CHECKPOINT:-0}" in 0|1) ;; *) echo '[M204 FAIL] invalid capture flag' >&2; exit 2 ;; esac
if [ "${NAS_M204_FAST_REPLAY:-0}" = 1 ]; then
    [ "$m204" = 1 ] || { echo '[M204 FAIL] replay requires M204 cohort' >&2; exit 2; }
    if sudo python3 scripts/m204-alpine-checkpoint.py verify "$port" "$root" \
            "$checkpoint" "$checkpoint_meta" "$checkpoint_serial" "$checkpoint_pcap" \
            "$checkpoint_reopen"; then
        replay=1
        printf 'verified-replay\n' > "$PWD/$work/m204-replay-mode.txt"
        echo '[M204 VERIFIED CHECKPOINT REPLAY] using prior cold two-boot clean image; this is NOT fresh public-HTTPS acceptance'
    else
        echo '[M204 CHECKPOINT MISS] missing/invalid provenance or fsck; falling back to full cold public HTTPS boot'
    fi
fi

if [ "$replay" = 0 ]; then
# An external mirror failure is a clear CI infrastructure failure, never a
# guest success or a reason to silently substitute the local M195 repository.
public_index="https://dl-cdn.alpinelinux.org/alpine/v3.23/main/$alpine_arch/APKINDEX.tar.gz"
if ! curl --noproxy '*' -fIsS --retry 2 --retry-delay 2 \
          --connect-timeout 12 --max-time 40 "$public_index" >/dev/null; then
    echo '[M197 PUBLIC INFRA FAIL] GitHub runner cannot reach official Alpine HTTPS CDN' >&2
    exit 1
fi

overlay=$(mktemp -d "$PWD/$work/m197-software-overlay.XXXXXX")
sudo cp -a "$root/." "$overlay/"
sudo python3 scripts/m193-alpine-login-overlay.py "$root" "$overlay"
sudo python3 scripts/m197-alpine-stage.py "$root" "$overlay"
if [ "$m198" = 1 ]; then
    sh scripts/stage-m198-alpine-desktop.sh "$port" "$overlay"
    if [ "$m199" = 1 ]; then
        sh scripts/stage-m199-alpine-openrc.sh "$port" "$overlay"
    fi
fi
sudo install -m 0644 tests/m197-software-workload.sh \
    "$overlay/root/m197-software-workload.sh"
sudo test -s "$overlay/root/m197-software-workload.sh"
if [ "$m204" = 1 ]; then
    sudo install -m 0644 tests/m204-software-workload.sh \
        "$overlay/root/m204-software-workload.sh"
    sudo install -m 0644 scripts/m204-alpine-cohort.tsv \
        "$overlay/root/m204-cohort.tsv"
    sudo test -s "$overlay/root/m204-software-workload.sh"
    sudo test -s "$overlay/root/m204-cohort.tsv"
    sudo test ! -e "$overlay/root/m204-first-results.tsv" || {
        echo '[M204 FAIL] cohort results were preseeded' >&2; exit 1;
    }
fi
if [ "$m205" = 1 ] || [ "$m206" = 1 ] || [ "$m215" = 1 ] || [ "$m218" = 1 ] || [ "$m219" = 1 ] || [ "$m220" = 1 ] || [ "$m221" = 1 ] || [ "$m222" = 1 ] || [ "$m223" = 1 ]; then
    # Reuse the M205 signed-install/ledger owner for both independent cohorts;
    # the M206 module contributes only new original-program semantic probes.
    sudo install -m 0644 tests/m205-software-workload.sh \
        "$overlay/root/m205-software-workload.sh"
    if [ "$m223" = 1 ]; then
    cohort=m223
    sudo install -m 0644 tests/m223-http-workload.sh \
        "$overlay/root/m223-http-workload.sh"
    sudo install -m 0644 scripts/m223-candidate-elfs.tsv \
        "$overlay/root/m223-cohort.tsv"
    sudo test -s "$overlay/root/m223-http-workload.sh"
    sudo test -s "$overlay/root/m223-cohort.tsv"
    # Check only the observed signed stage, not an imagined BusyBox alias.
    # Any preinstalled native ELF or arbitrary symlink is a hard failure.
    sudo python3 scripts/m223-alpine-disk-check.py stage "$port" "$overlay"
    elif [ "$m222" = 1 ]; then
    cohort=m222
    sudo install -m 0644 tests/m222-ssh-workload.sh \
        "$overlay/root/m222-ssh-workload.sh"
    sudo install -m 0644 scripts/m222-candidate-elfs.tsv \
        "$overlay/root/m222-cohort.tsv"
    sudo test -s "$overlay/root/m222-ssh-workload.sh"
    sudo test -s "$overlay/root/m222-cohort.tsv"
    m222_prepare_host first "$overlay"
    m222_prepare_host reopen "$overlay"
    cmp -s "$PWD/$work/m222-host-first/client_id_ed25519.pub" \
        "$PWD/$work/m222-host-reopen/client_id_ed25519.pub" && {
        echo '[M222 SECRET FAIL] first/reopen Ed25519 public identities repeated' >&2
        exit 1
    }
    cmp -s "$PWD/$work/m222-host-first/ssh_host_ed25519_key.pub" \
        "$PWD/$work/m222-host-reopen/ssh_host_ed25519_key.pub" && {
        echo '[M222 SECRET FAIL] first/reopen host key identities repeated' >&2
        exit 1
    }
    elif [ "$m221" = 1 ]; then
    cohort=m221
    sudo install -m 0644 tests/m221-dns-update-workload.sh \
        "$overlay/root/m221-dns-update-workload.sh"
    sudo install -m 0644 scripts/m221-candidate-elfs.tsv \
        "$overlay/root/m221-cohort.tsv"
    sudo test -s "$overlay/root/m205-software-workload.sh"
    sudo test -s "$overlay/root/m221-dns-update-workload.sh"
    sudo test -s "$overlay/root/m221-cohort.tsv"
    elif [ "$m220" = 1 ]; then
    cohort=m220
    sudo install -m 0644 tests/m220-nping-workload.sh \
        "$overlay/root/m220-nping-workload.sh"
    sudo install -m 0644 scripts/m220-candidate-elfs.tsv \
        "$overlay/root/m220-cohort.tsv"
    sudo test -s "$overlay/root/m205-software-workload.sh"
    sudo test -s "$overlay/root/m220-nping-workload.sh"
    sudo test -s "$overlay/root/m220-cohort.tsv"
    elif [ "$m219" = 1 ]; then
    cohort=m219
    sudo install -m 0644 tests/m219-utmpdump-workload.sh \
        "$overlay/root/m219-utmpdump-workload.sh"
    sudo install -m 0644 scripts/m219-candidate-elfs.tsv \
        "$overlay/root/m219-cohort.tsv"
    sudo test -s "$overlay/root/m205-software-workload.sh"
    sudo test -s "$overlay/root/m219-utmpdump-workload.sh"
    sudo test -s "$overlay/root/m219-cohort.tsv"
    elif [ "$m218" = 1 ]; then
    cohort=m218
    sudo install -m 0644 tests/m218-ifstat-workload.sh \
        "$overlay/root/m218-ifstat-workload.sh"
    sudo install -m 0644 scripts/m218-candidate-elfs.tsv \
        "$overlay/root/m218-cohort.tsv"
    sudo test -s "$overlay/root/m205-software-workload.sh"
    sudo test -s "$overlay/root/m218-ifstat-workload.sh"
    sudo test -s "$overlay/root/m218-cohort.tsv"
    elif [ "$m215" = 1 ]; then
    cohort=m215
    sudo install -m 0644 tests/m213-software-workload.sh \
        "$overlay/root/m213-software-workload.sh"
    sudo install -m 0644 scripts/m215-candidate-elfs.tsv \
        "$overlay/root/m215-cohort.tsv"
    sudo test -s "$overlay/root/m205-software-workload.sh"
    sudo test -s "$overlay/root/m213-software-workload.sh"
    sudo test -s "$overlay/root/m215-cohort.tsv"
    elif [ "$m205" = 1 ]; then
    cohort=m205
    sudo install -m 0644 scripts/m205-alpine-cohort.tsv \
        "$overlay/root/m205-cohort.tsv"
    # M205's genuine llvm21-symbolizer needs a real, debug-info-bearing
    # target-ISA ELF. Build it on the host, just as our earlier M141 toolchain
    # fixtures did: the guest receives NO alternate compiler or fake output.
    # The only guest program under acceptance is the original Alpine LLVM.
    case "$port" in
        amd64) fixture_cc=gcc; fixture_nm=nm ;;
        arm64) fixture_cc=aarch64-linux-gnu-gcc; fixture_nm=aarch64-linux-gnu-nm ;;
        riscv64) fixture_cc=riscv64-linux-gnu-gcc; fixture_nm=riscv64-linux-gnu-nm ;;
    esac
    fixture_elf="$PWD/$work/m205-symbolizer.elf"
    fixture_addr="$PWD/$work/m205-symbolizer.addr"
    "$fixture_cc" -O0 -g -fno-inline tests/m205_symbol.c -o "$fixture_elf"
    fixture_listener="$PWD/$work/m205-loopback-listener.elf"
    "$fixture_cc" -static -Os tests/m205-loopback-listener.c -o "$fixture_listener"
    fixture_pty="$PWD/$work/m205-pty-exchange.elf"
    "$fixture_cc" -static -O2 -Wall -Wextra -Werror \
        tests/m205-pty-exchange.c -o "$fixture_pty"
    "$fixture_nm" -n "$fixture_elf" | awk \
        '$3 == "m205_symbol" { print "0x" $1; found = 1 } END { if (!found) exit 1 }' \
        > "$fixture_addr"
    test -s "$fixture_elf" && test -s "$fixture_addr" && \
        test -s "$fixture_listener" && test -s "$fixture_pty"
    sudo install -d -m 0755 "$overlay/root/m205-fixtures"
    sudo install -m 0644 "$fixture_elf" \
        "$overlay/root/m205-fixtures/symbolizer.elf"
    sudo install -m 0644 "$fixture_addr" \
        "$overlay/root/m205-fixtures/symbolizer.addr"
    sudo install -m 0755 "$fixture_listener" \
        "$overlay/root/m205-fixtures/loopback-listener"
    sudo install -m 0755 "$fixture_pty" \
        "$overlay/root/m205-fixtures/pty-exchange"
    sudo test -s "$overlay/root/m205-software-workload.sh"
    sudo test -s "$overlay/root/m205-cohort.tsv"
    else
    cohort=m206
    sudo install -m 0644 tests/m206-software-workload.sh \
        "$overlay/root/m206-software-workload.sh"
    sudo install -m 0644 scripts/m206-alpine-cohort.tsv \
        "$overlay/root/m206-cohort.tsv"
    sudo test -s "$overlay/root/m205-software-workload.sh"
    sudo test -s "$overlay/root/m206-software-workload.sh"
    sudo test -s "$overlay/root/m206-cohort.tsv"
    fi
    # Both M205's original UDP tools and M218's genuine ifstat require
    # this one target-native 40-datagram stimulus. Build it only when a
    # consuming cohort is enabled; never pass off a host fixture as an
    # original signed Alpine ELF under acceptance.
    if [ "$m205" = 1 ] || [ "$m218" = 1 ]; then
        case "$port" in
            amd64) udp_cc=gcc ;;
            arm64) udp_cc=aarch64-linux-gnu-gcc ;;
            riscv64) udp_cc=riscv64-linux-gnu-gcc ;;
        esac
        fixture_udp="$PWD/$work/m205-udp-traffic.elf"
        "$udp_cc" -static -O2 -Wall -Wextra -Werror \
            tests/m205-udp-traffic.c -o "$fixture_udp"
        test -s "$fixture_udp"
        sudo install -d -m 0755 "$overlay/root/m205-fixtures"
        sudo install -m 0755 "$fixture_udp" \
            "$overlay/root/m205-fixtures/udp-traffic"
        sudo test -x "$overlay/root/m205-fixtures/udp-traffic"
    fi
    if [ "$m219" = 1 ]; then
        # The host fixture is a deterministic target-native struct utmp
        # writer/oracle, NOT an accepted Alpine program. /usr/bin/utmpdump
        # itself must come only from the original signed APK inside guest.
        case "$port" in
            amd64) utmp_cc=gcc ;;
            arm64) utmp_cc=aarch64-linux-gnu-gcc ;;
            riscv64) utmp_cc=riscv64-linux-gnu-gcc ;;
        esac
        fixture_utmp="$PWD/$work/m219-utmp-fixture.elf"
        "$utmp_cc" -static -O2 -Wall -Wextra -Werror \
            tests/m219-utmpdump-fixture.c -o "$fixture_utmp"
        test -s "$fixture_utmp"
        sudo install -d -m 0755 "$overlay/root/m219-fixtures"
        sudo install -m 0755 "$fixture_utmp" \
            "$overlay/root/m219-fixtures/utmpdump-fixture"
        sudo test -x "$overlay/root/m219-fixtures/utmpdump-fixture"
    fi
    # The M205 host census already fetched every package from Alpine's
    # signature-verified official indexes. Reuse the original byte-for-byte
    # APK archives as a standard local Alpine cache, not preinstalled ELFs.
    # The genuine APK guest still performs every install and postinstall
    # transaction; M197's separate public HTTPS installs stay live/unchanged.
    cohort_package_sha=$(sha256sum "rootfs/alpine/packages-$cohort-candidates.txt" |
        awk '{print $1}')
    cohort_cache_root="$PWD/build/external/$cohort-candidate-inventory/$port/packages-$cohort_package_sha/rootfs"
    cohort_cache_src="$cohort_cache_root/var/cache/apk"
    cohort_cache_manifest="$PWD/build/external/$cohort-candidate-inventory/$port/signed-cache.sha256"
    test -d "$cohort_cache_src" &&
        test ! -L "$cohort_cache_root" &&
        test ! -L "$cohort_cache_root/var" &&
        test ! -L "$cohort_cache_root/var/cache" &&
        test ! -L "$cohort_cache_src" &&
        test -s "$cohort_cache_manifest" &&
        test ! -L "$cohort_cache_manifest" &&
        test "$(cd "$cohort_cache_src" && pwd -P)" = "$cohort_cache_src" || {
            echo '[SIGNED CACHE FAIL] signed host APK archive/index proof missing' >&2
            exit 1
        }
    ( cd "$cohort_cache_src" && sha256sum -c "$cohort_cache_manifest" >/dev/null ) || {
        echo '[SIGNED CACHE FAIL] host APK archive/index SHA256 changed' >&2
        exit 1
    }
    sudo test ! -L "$overlay/var" &&
        sudo test ! -L "$overlay/var/cache" &&
        sudo test ! -L "$overlay/etc/apk" &&
        sudo test ! -e "$overlay/etc/apk/cache" &&
        sudo test ! -L "$overlay/etc/apk/cache" || {
            echo '[SIGNED CACHE FAIL] unsafe guest cache parent/destination' >&2
            exit 1
        }
    sudo install -d -m 0755 "$overlay/var/cache/apk"
    sudo cp -a "$cohort_cache_src/." "$overlay/var/cache/apk/"
    sudo ln -s /var/cache/apk "$overlay/etc/apk/cache"
    ( cd "$overlay/var/cache/apk" &&
      sha256sum -c "$cohort_cache_manifest" >/dev/null ) || {
        echo '[SIGNED CACHE FAIL] staged guest APK archive/index SHA256 mismatch' >&2
        exit 1
    }
    # Require conservative post-install headroom for the unpacked LLVM/BIND
    # dependencies, ext4 metadata and persistent second QEMU. No silent disk
    # enlargement or fallback to an unverified network install is allowed.
    cohort_used_mib=$(du -sm "$overlay" | awk '{print $1}')
    if [ "$((cohort_used_mib+256+16))" -ge "$disk_mib" ]; then
        echo "[SIGNED CACHE FAIL] disk capacity: staged=${cohort_used_mib}MiB, disk=${disk_mib}MiB" >&2
        exit 1
    fi
    echo "[$cohort SIGNED APK CACHE STAGED] official native archives/indexes; guest add is --no-network, M197 stays public HTTPS"
    sudo test ! -e "$overlay/root/$cohort-first-results.tsv" || {
        echo '[COMPAT FAIL] cohort results were preseeded' >&2; exit 1;
    }
fi
sudo test ! -e "$overlay/root/m197-app" || {
    echo '[M197 PUBLIC STAGE FAIL] application output was preseeded' >&2; exit 1;
}
sudo test ! -e "$overlay/root/m197-wan.txt" || {
    echo '[M197 PUBLIC STAGE FAIL] persistence witness was preseeded' >&2; exit 1;
}
sudo env NAS_M191_DISK_MIB="$disk_mib" \
    python3 scripts/m191-alpine-root-disk.py prepare "$port" "$overlay" "$disk"
sudo chown "$(id -u):$(id -g)" "$disk"
else
    # Never mutate the immutable checkpoint: all test writes go to a new
    # disposable disk, and the current kernel performs a REAL QEMU reboot.
    cp "$checkpoint" "$disk"
    sudo chown "$(id -u):$(id -g)" "$disk"
fi

export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_M191_ALPINE_ROOT=1
export NAS_ENABLE_RAMFS=0 NAS_ENABLE_LITTLEFS=0
unset NAS_ENABLE_M179_FS_IRQ NAS_ENABLE_M180_FS_USER_IRQ
unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
export NAS_VIRTIO_BLK_IMAGE="$disk" NAS_ACCEPTANCE_CPIO="$rescue"
pcap="$PWD/$work/m197-public-wire.pcap"
wire_json="$PWD/$work/m197-public-wire.json"
if [ "$replay" = 0 ]; then
    test ! -e "$pcap" || { echo '[M197 PUBLIC WIRE FAIL] stale PCAP'; exit 1; }
    export NAS_VIRTIO_NET_PCAP="$pcap"
fi
export BUSYBOX_PARITY_BINARY="$busybox"
export BUSYBOX_PARITY_CASE=m197-batch BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m197-alpine-cases.tsv
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M197_PUBLIC_QEMU_TIMEOUT:-1020}
if [ "$m204" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M204_PUBLIC_QEMU_TIMEOUT:-2400}
fi
if [ "$m205" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M205_PUBLIC_QEMU_TIMEOUT:-4500}
fi
if [ "$m206" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M206_PUBLIC_QEMU_TIMEOUT:-4500}
fi
if [ "$m215" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M215_PUBLIC_QEMU_TIMEOUT:-2500}
fi
if [ "$m218" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M218_PUBLIC_QEMU_TIMEOUT:-2500}
fi
if [ "$m219" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M219_PUBLIC_QEMU_TIMEOUT:-2500}
fi
if [ "$m220" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M220_PUBLIC_QEMU_TIMEOUT:-2500}
fi
if [ "$m221" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M221_PUBLIC_QEMU_TIMEOUT:-2500}
fi
if [ "$m222" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M222_PUBLIC_QEMU_TIMEOUT:-2500}
fi
if [ "$m223" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M223_PUBLIC_QEMU_TIMEOUT:-2500}
fi
if [ "$m198" = 1 ]; then
    export NAS_QEMU_INPUT_OPTIN=1 NAS_QEMU_VIRTIO_GPU=1 NAS_QEMU_RAMFB=0
    export NAS_ACCEPTANCE_QEMU_RAM=2048M
    # One boot runs M197's full public transaction before GUI/Vim. Keep the
    # slow external WAN budget separate from the post-install QMP budget.
    if [ "$m199" = 1 ]; then
        export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M199_FIRST_BOOT_SECONDS:-2600}
    else
        export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M198_FIRST_BOOT_SECONDS:-2400}
    fi
    export NAS_QEMU_QMP_SOCKET="$PWD/$work/m198-integrated-desktop.qmp"
    test ! -e "$NAS_QEMU_QMP_SOCKET" || {
        echo '[M198 QMP FAIL] stale socket from another run' >&2
        exit 1
    }
fi
if [ "$port" = amd64 ]; then
    export NAS_ACCEPTANCE_KERNEL="$PWD/build/${NAS_M191_BUILD_TAG:-m191-root}-virtio-x86/kernel.elf"
    test -s "$NAS_ACCEPTANCE_KERNEL"
fi

# M220 alone needs an independent *host process* to receive the original
# guest's unprivileged UDP packet on each native QEMU boot. This lives outside
# the guest and cannot be satisfied by a CLI exit marker or a user fixture.
m220_host_pid=
m220_stop_host() {
    if [ -n "$m220_host_pid" ]; then
        kill "$m220_host_pid" 2>/dev/null || :
        wait "$m220_host_pid" 2>/dev/null || :
        m220_host_pid=
    fi
}
m220_start_host() {
    phase=$1
    log="$PWD/$work/m220-$phase-host-udp.log"
    test ! -e "$log" || {
        echo "[M220 HOST FAIL] stale independent $phase host UDP log" >&2
        exit 1
    }
    python3 -u scripts/m220-nping-host-oracle.py "$phase" > "$log" 2>&1 &
    m220_host_pid=$!
    ready=0
    for attempt in 1 2 3 4 5 6; do
        if grep -Fxq "[M220 HOST READY] phase=$phase port=15416" "$log"; then
            ready=1
            break
        fi
        kill -0 "$m220_host_pid" 2>/dev/null || break
        sleep 1
    done
    if [ "$ready" -ne 1 ]; then
        echo "[M220 HOST FAIL] host UDP socket not ready on $phase QEMU" >&2
        tail -30 "$log" >&2 || :
        exit 1
    fi
}
if [ "$m220" = 1 ]; then
    trap 'm220_stop_host' EXIT
    m220_start_host first
fi

# The M221 authoritative server shares M142's strict RFC2136 decoder but
# starts with NO records in a brand new process on EACH independent QEMU.
# Its lifecycle and log cannot reuse the earlier Nping UDP host process.
m221_host_pid=
m221_stop_host() {
    if [ -n "$m221_host_pid" ]; then
        kill "$m221_host_pid" 2>/dev/null || :
        wait "$m221_host_pid" 2>/dev/null || :
        m221_host_pid=
    fi
}
m221_start_host() {
    phase=$1
    case "$phase" in
        first) dns_owner=m221-first.naseed.test.; dns_ip=192.0.2.221 ;;
        reopen) dns_owner=m221-reopen.naseed.test.; dns_ip=192.0.2.222 ;;
        *) echo '[M221 HOST FAIL] invalid native boot phase' >&2; exit 2 ;;
    esac
    log="$PWD/$work/m221-$phase-host-dns.log"
    test ! -e "$log" || {
        echo "[M221 HOST FAIL] stale independent $phase DNS host log" >&2
        exit 1
    }
    python3 -u scripts/m221-dns-host-oracle.py "$phase" > "$log" 2>&1 &
    m221_host_pid=$!
    ready=0
    for attempt in 1 2 3 4 5 6; do
        if grep -Fxq "[M221 HOST READY] phase=$phase port=15416 owner=$dns_owner ip=$dns_ip" "$log"; then
            ready=1
            break
        fi
        kill -0 "$m221_host_pid" 2>/dev/null || break
        sleep 1
    done
    if [ "$ready" -ne 1 ]; then
        echo "[M221 HOST FAIL] authoritative DNS server absent on $phase QEMU" >&2
        tail -30 "$log" >&2 || :
        exit 1
    fi
}
if [ "$m221" = 1 ]; then
    trap 'm221_stop_host' EXIT
    m221_start_host first
fi
if [ "$m222" = 1 ]; then
    command -v sshd >/dev/null && command -v ssh-keygen >/dev/null || {
        echo '[M222 HOST FAIL] real OpenSSH server/keygen missing' >&2; exit 1;
    }
    m222_start_host first
fi
if [ "$m223" = 1 ]; then
    m223_start_host first
fi

if [ "$m199" = 1 ]; then
    # This is the SAME original /sbin/init manifest on BOTH boots; M198's
    # independent second QEMU previously used a CPIO BusyBox entry instead.
    export BUSYBOX_PARITY_MANIFEST=scripts/m199-alpine-boot-cases.tsv
fi

if [ "$replay" = 0 ]; then
fifo="$PWD/$work/m197-software-serial-input.fifo"
export NAS_QEMU_SERIAL_FIFO="$fifo"
serial="$PWD/$logs/m197-batch.serial.log"
if [ "$m198" = 1 ]; then
    if [ "$m199" = 1 ]; then
        export NAS_M199_QMP_FAIL_FILE="$PWD/$work/m199-qmp-first.failed"
        export NAS_M199_QMP_PASS_FILE="$PWD/$work/m199-qmp-first.pass"
        test ! -e "$NAS_M199_QMP_FAIL_FILE" && test ! -e "$NAS_M199_QMP_PASS_FILE"
        python3 scripts/m199-qmp-alpine-openrc.py --mode first \
            --serial "$serial" --qmp "$NAS_QEMU_QMP_SOCKET" \
            --outdir "$work" --success-file "$NAS_M199_QMP_PASS_FILE" \
            --timeout "${NAS_M199_DESKTOP_SECONDS:-360}" \
            > "$work/m199-first-qmp.log" 2>&1 &
        watcher=$!
        if ! python3 scripts/m199-alpine-uart.py first "$runner" "$serial" \
                "$PWD/$work/m199-first-runner.log" "$fifo"; then
            kill "$watcher" 2>/dev/null || :
            wait "$watcher" 2>/dev/null || :
            tail -100 "$work/m199-first-qmp.log" >&2 || :
            exit 1
        fi
        if ! wait "$watcher"; then
            echo '[M199 FIRST QMP FAIL] physical Vim and OpenRC desktop' >&2
            tail -100 "$work/m199-first-qmp.log" >&2
            exit 1
        fi
        test -s "$NAS_M199_QMP_PASS_FILE"
    else
    export NAS_M198_QMP_FAIL_FILE="$PWD/$work/m198-qmp.failed"
    test ! -e "$NAS_M198_QMP_FAIL_FILE" || {
        echo '[M198 QMP FAIL] stale failure file' >&2; exit 1;
    }
    python3 scripts/m198-qmp-alpine-desktop.py \
        --serial "$serial" --qmp "$NAS_QEMU_QMP_SOCKET" \
        --outdir "$work" --timeout "${NAS_M198_DESKTOP_SECONDS:-300}" \
        > "$work/m198-qmp-check.log" 2>&1 &
    watcher=$!
    if ! python3 scripts/m198-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m198-first-runner.log" "$fifo"; then
        kill "$watcher" 2>/dev/null || :
        wait "$watcher" 2>/dev/null || :
        tail -100 "$work/m198-qmp-check.log" >&2 || :
        exit 1
    fi
    if ! wait "$watcher"; then
        echo '[M198 QMP FAIL] physical desktop and original editor' >&2
        tail -100 "$work/m198-qmp-check.log" >&2
        exit 1
    fi
    grep -Fq '[M198 PHYSICAL QMP PASS]' "$work/m198-qmp-check.log"
    fi
else
    if [ "$m223" = 1 ]; then
        python3 scripts/m223-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m223-first-runner.log" "$fifo"
    elif [ "$m222" = 1 ]; then
        python3 scripts/m222-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m222-first-runner.log" "$fifo"
    elif [ "$m221" = 1 ]; then
        python3 scripts/m221-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m221-first-runner.log" "$fifo"
    elif [ "$m220" = 1 ]; then
        python3 scripts/m220-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m220-first-runner.log" "$fifo"
    elif [ "$m219" = 1 ]; then
        python3 scripts/m219-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m219-first-runner.log" "$fifo"
    elif [ "$m218" = 1 ]; then
        python3 scripts/m218-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m218-first-runner.log" "$fifo"
    elif [ "$m215" = 1 ]; then
        python3 scripts/m215-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m215-first-runner.log" "$fifo"
    elif [ "$m206" = 1 ]; then
        python3 scripts/m206-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m206-first-runner.log" "$fifo"
    elif [ "$m205" = 1 ]; then
        python3 scripts/m205-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m205-first-runner.log" "$fifo"
    elif [ "$m204" = 1 ]; then
        python3 scripts/m204-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m204-first-runner.log" "$fifo"
    else
        python3 scripts/m197-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m197-first-runner.log" "$fifo"
    fi
fi
test -s "$serial" && test -s "$pcap"
if [ "$m220" = 1 ]; then
    m220_stop_host
    python3 scripts/m220-nping-wire-check.py first \
        "$pcap" "$PWD/$work/m220-first-host-udp.log" \
        "$PWD/$work/m220-first-wire.json"
fi
if [ "$m221" = 1 ]; then
    m221_stop_host
    python3 scripts/m221-dns-wire-check.py first \
        "$pcap" "$PWD/$work/m221-first-host-dns.log" \
        "$PWD/$work/m221-first-wire.json"
fi
if [ "$m222" = 1 ]; then
    m222_stop_host
    NAS_M191_DISK_MIB="$disk_mib" python3 scripts/m222-ssh-wire-check.py first "$port" \
        "$pcap" "$PWD/$work/m222-first-host-sshd.log" \
        "$PWD/$work/m222-host-first" "$disk" \
        "$PWD/$work/m222-first-wire.json"
fi
if [ "$m223" = 1 ]; then
    m223_stop_host
    NAS_M191_DISK_MIB="$disk_mib" python3 scripts/m223-http-wire-check.py first "$port" \
        "$pcap" "$PWD/$work/m223-first-host-http.log" \
        "$PWD/$work/m223-host-first" "$disk" \
        "$PWD/$work/m223-first-wire.json"
fi
cp "$serial" "$PWD/$work/m197-live.serial.log"
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$serial" || {
    echo '[M197 PUBLIC WIRE FAIL] no real VirtIO DHCP lease' >&2
    exit 1
}
# M196's strict independent parser demands actual guest DNS query+answer,
# a public TCP three-way handshake and TLS payload, not just guest markers.
python3 scripts/m196-public-wire-check.py "$pcap" "$wire_json"
grep -Fq 'M197_APP_WORKLOAD_PASS' "$serial" || {
    echo '[M197 PUBLIC WORKLOAD FAIL] no genuine integrated app result' >&2
    exit 1
}
if [ "$m204" = 1 ]; then
    grep -Fq 'M204_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M204 FAIL] first compatibility inventory did not complete' >&2
        exit 1
    }
fi
if [ "$m205" = 1 ]; then
    grep -Fq 'M205_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M205 FAIL] first new-ELF inventory did not complete' >&2
        exit 1
    }
fi
if [ "$m206" = 1 ]; then
    grep -Fq 'M206_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M206 FAIL] first new-ELF inventory did not complete' >&2
        exit 1
    }
fi
if [ "$m215" = 1 ]; then
    grep -Fq 'M215_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M215 FAIL] first five-ELF pilot ledger did not complete' >&2
        exit 1
    }
fi
if [ "$m218" = 1 ]; then
    grep -Fq 'M218_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M218 FAIL] first original ifstat ledger did not complete' >&2
        exit 1
    }
fi
if [ "$m219" = 1 ]; then
    grep -Fq 'M219_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M219 FAIL] first original utmpdump ledger did not complete' >&2
        exit 1
    }
fi
if [ "$m220" = 1 ]; then
    grep -Fq 'M220_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M220 FAIL] first original Nping ledger did not complete' >&2
        exit 1
    }
fi
if [ "$m221" = 1 ]; then
    grep -Fq 'M221_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M221 FAIL] first original nsupdate+host ledger incomplete' >&2
        exit 1
    }
fi
if [ "$m222" = 1 ]; then
    grep -Fq 'M222_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M222 FAIL] first original SSH/SCP/SFTP ledger incomplete' >&2; exit 1;
    }
fi
if [ "$m223" = 1 ]; then
    grep -Fq 'M223_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M223 FAIL] first original HTTP client/server ledger incomplete' >&2; exit 1;
    }
fi
echo "[M197 ALPINE PUBLIC SOFTWARE LIVE PASS] $port official signed "\
     'Bash Lua SQLite Git from public HTTPS, live DHCP resolver, ' \
     'independent PCAP and four real applications on ext4'
if [ "$m198" = 1 ]; then
    if [ "$m199" = 1 ]; then
        grep -Fq 'M199_FIRST_DURABLE_RC=0' "$serial"
        grep -Fq 'M199_OPENRC_RESTART_RC=0' "$serial"
    else
        grep -Fq 'M198_GUI_DURABLE_RC=0' "$serial"
        # Real Alpine UART is CRLF-terminated. Normalize the serial line
        # ending before strict whole-line matching: grep -x on raw \r\n
        # falsely failed after QMP, Vim, wait4 and guest ext4 all passed.
        tr -d '\r' < "$serial" | grep -Fxq 'M187_DESKTOP_PASS'
    fi
    echo "[M198 ALPINE GUI LIVE PASS] $port original Alpine PID1 + public APK "\
        'and old GUI as ordinary process, native VirtIO keyboard/Vim + GPU pixels'
fi
else
    serial="$checkpoint_serial"
fi

if [ "$m199" = 1 ]; then
    # Retain the first full UART proof before a SECOND original Alpine PID1
    # overwrites the runner's m197-batch.serial.log in the same native matrix.
    cp "$serial" "$PWD/$work/m199-first.serial.log"
    unset NAS_QEMU_SERIAL_FIFO NAS_VIRTIO_NET_PCAP NAS_QEMU_QMP_SOCKET
    export NAS_QEMU_QMP_SOCKET="$PWD/$work/m199-reboot-desktop.qmp"
    export NAS_QEMU_SERIAL_FIFO="$PWD/$work/m199-reboot-serial-input.fifo"
    export BUSYBOX_PARITY_MANIFEST=scripts/m199-alpine-boot-cases.tsv
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M199_REBOOT_SECONDS:-500}
    export NAS_M199_QMP_FAIL_FILE="$PWD/$work/m199-qmp-reboot.failed"
    export NAS_M199_QMP_PASS_FILE="$PWD/$work/m199-qmp-reboot.pass"
    test ! -e "$NAS_QEMU_QMP_SOCKET" &&
    test ! -e "$NAS_M199_QMP_FAIL_FILE" && test ! -e "$NAS_M199_QMP_PASS_FILE"
    python3 scripts/m199-qmp-alpine-openrc.py --mode reboot \
        --serial "$serial" --qmp "$NAS_QEMU_QMP_SOCKET" \
        --outdir "$work" --success-file "$NAS_M199_QMP_PASS_FILE" \
        --timeout "${NAS_M199_DESKTOP_SECONDS:-360}" \
        > "$work/m199-reboot-qmp.log" 2>&1 &
    watcher=$!
    if ! python3 scripts/m199-alpine-uart.py reboot "$runner" "$serial" \
            "$PWD/$work/m199-reboot-runner.log" "$NAS_QEMU_SERIAL_FIFO"; then
        kill "$watcher" 2>/dev/null || :
        wait "$watcher" 2>/dev/null || :
        tail -100 "$work/m199-reboot-qmp.log" >&2 || :
        exit 1
    fi
    if ! wait "$watcher"; then
        echo '[M199 REBOOT QMP FAIL] OpenRC auto-started GPU desktop' >&2
        tail -100 "$work/m199-reboot-qmp.log" >&2
        exit 1
    fi
    grep -Fq 'M199_REBOOT_AUTO_STATUS_RC=0' "$serial"
    grep -Fq 'M199_REBOOT_DURABLE_RC=0' "$serial"
    grep -Fq 'M197_REOPEN_WORKLOAD_PASS' "$serial"
    test -s "$NAS_M199_QMP_PASS_FILE"
    sudo python3 scripts/m199-alpine-disk-check.py "$port" "$root" "$disk"
    sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
    echo "[M199 ALPINE OPENRC TWO BOOT PASS] $port original signed Alpine "\
         'init/OpenRC twice, guest rc-update persisted, OpenRC daemon lifecycle, ' \
         'auto-restored physical GPU, QMP Vim and signed applications on ext4'
    exit 0
fi

# A SECOND native QEMU has NO network requirement; it must observe and run
# the genuine installed ELFs and all SQL/Git/application state from disk.
unset NAS_QEMU_SERIAL_FIFO NAS_VIRTIO_NET_PCAP NAS_QEMU_QMP_SOCKET
if [ "$m220" = 1 ]; then
    # M197 normally unsets the first PCAP on reboot. M220 requires TWO
    # independent captures and two different host-oracle nonce payloads.
    export NAS_VIRTIO_NET_PCAP="$PWD/$work/m220-reopen-wire.pcap"
    test ! -e "$NAS_VIRTIO_NET_PCAP" || {
        echo '[M220 HOST FAIL] stale second QEMU PCAP' >&2; exit 1;
    }
    m220_start_host reopen
fi
if [ "$m221" = 1 ]; then
    export NAS_VIRTIO_NET_PCAP="$PWD/$work/m221-reopen-wire.pcap"
    test ! -e "$NAS_VIRTIO_NET_PCAP" || {
        echo '[M221 HOST FAIL] stale second independent QEMU PCAP' >&2; exit 1;
    }
    m221_start_host reopen
fi
if [ "$m222" = 1 ]; then
    export NAS_VIRTIO_NET_PCAP="$PWD/$work/m222-reopen-wire.pcap"
    test ! -e "$NAS_VIRTIO_NET_PCAP" || {
        echo '[M222 FAIL] stale second SSH QEMU PCAP' >&2; exit 1;
    }
    m222_start_host reopen
fi
if [ "$m223" = 1 ]; then
    export NAS_VIRTIO_NET_PCAP="$PWD/$work/m223-reopen-wire.pcap"
    test ! -e "$NAS_VIRTIO_NET_PCAP" || {
        echo '[M223 HOST FAIL] stale independent second HTTP QEMU PCAP' >&2; exit 1;
    }
    m223_start_host reopen
fi
if [ "$m198" = 1 ]; then
    export BUSYBOX_PARITY_MANIFEST=scripts/m198-alpine-reopen-cases.tsv
else
    if [ "$m223" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m223-alpine-reopen-cases.tsv
    elif [ "$m222" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m222-alpine-reopen-cases.tsv
    elif [ "$m221" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m221-alpine-reopen-cases.tsv
    elif [ "$m220" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m220-alpine-reopen-cases.tsv
    elif [ "$m219" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m219-alpine-reopen-cases.tsv
    elif [ "$m218" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m218-alpine-reopen-cases.tsv
    elif [ "$m215" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m215-alpine-reopen-cases.tsv
    elif [ "$m206" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m206-alpine-reopen-cases.tsv
    elif [ "$m205" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m205-alpine-reopen-cases.tsv
    elif [ "$m204" = 1 ]; then
        if [ "$replay" = 1 ]; then
            export BUSYBOX_PARITY_MANIFEST=scripts/m204-alpine-replay-cases.tsv
        else
            export BUSYBOX_PARITY_MANIFEST=scripts/m204-alpine-reopen-cases.tsv
        fi
    else
        export BUSYBOX_PARITY_MANIFEST=scripts/m197-alpine-reopen-cases.tsv
    fi
fi
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M197_REOPEN_SECONDS:-240}
if [ "$m204" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M204_REOPEN_SECONDS:-540}
fi
if [ "$m205" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M205_REOPEN_SECONDS:-900}
fi
if [ "$m206" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M206_REOPEN_SECONDS:-900}
fi
if [ "$m215" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M215_REOPEN_SECONDS:-600}
fi
if [ "$m218" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M218_REOPEN_SECONDS:-600}
fi
if [ "$m219" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M219_REOPEN_SECONDS:-600}
fi
if [ "$m220" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M220_REOPEN_SECONDS:-600}
fi
if [ "$m221" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M221_REOPEN_SECONDS:-700}
fi
if [ "$m222" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M222_REOPEN_SECONDS:-900}
fi
if [ "$m223" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M223_REOPEN_SECONDS:-900}
fi
if ! sh "$runner" > "$PWD/$work/m197-reopen-runner.log" 2>&1; then
    echo "[M197 ALPINE REOPEN FAIL] $port second native QEMU" >&2
    tail -120 "$PWD/$work/m197-reopen-runner.log" >&2
    exit 1
fi
if [ "$m220" = 1 ]; then
    m220_stop_host
    python3 scripts/m220-nping-wire-check.py reopen \
        "$PWD/$work/m220-reopen-wire.pcap" \
        "$PWD/$work/m220-reopen-host-udp.log" \
        "$PWD/$work/m220-reopen-wire.json"
fi
if [ "$m221" = 1 ]; then
    m221_stop_host
    python3 scripts/m221-dns-wire-check.py reopen \
        "$PWD/$work/m221-reopen-wire.pcap" \
        "$PWD/$work/m221-reopen-host-dns.log" \
        "$PWD/$work/m221-reopen-wire.json"
fi
if [ "$m222" = 1 ]; then
    m222_stop_host
    NAS_M191_DISK_MIB="$disk_mib" python3 scripts/m222-ssh-wire-check.py reopen "$port" \
        "$PWD/$work/m222-reopen-wire.pcap" \
        "$PWD/$work/m222-reopen-host-sshd.log" \
        "$PWD/$work/m222-host-reopen" "$disk" \
        "$PWD/$work/m222-reopen-wire.json"
fi
if [ "$m223" = 1 ]; then
    m223_stop_host
    NAS_M191_DISK_MIB="$disk_mib" python3 scripts/m223-http-wire-check.py reopen "$port" \
        "$PWD/$work/m223-reopen-wire.pcap" \
        "$PWD/$work/m223-reopen-host-http.log" \
        "$PWD/$work/m223-host-reopen" "$disk" \
        "$PWD/$work/m223-reopen-wire.json"
fi
reopen="$PWD/$logs/m197-batch.clean.log"
for marker in \
    'M197_REOPEN_WORKLOAD_PASS' \
    'M197_PUBLIC_ALPINE_SOFTWARE_2026' \
    '[M191 ROOT] physical Alpine ext4 registered at /' \
    '[LWEXT4 GUEST] clean upstream unmount and durable flush' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0'; do
    grep -Fxq "$marker" "$reopen" || {
        echo "[M197 ALPINE REOPEN FAIL] $port missing $marker" >&2
        tail -120 "$reopen" >&2
        exit 1
    }
done
if [ "$m204" = 1 ]; then
    if [ "$replay" = 1 ]; then
        expected_m204_marker=M204_REPLAY_DISCOVERY_COMPLETE
        report_replay_opt=--replay
    else
        expected_m204_marker=M204_REOPEN_DISCOVERY_COMPLETE
        report_replay_opt=
    fi
    grep -Fq "$expected_m204_marker" "$reopen" || {
        echo '[M204 FAIL] second QEMU did not re-evaluate the cohort' >&2
        exit 1
    }
    # The second native QEMU reuses the m197-batch SERIAL pathname! The
    # original first-QEMU proof was copied before reboot and must be used by
    # the report. A replay uses the immutable verified prior-first UART.
    if [ "$replay" = 1 ]; then
        first_serial="$checkpoint_serial"
    else
        first_serial="$PWD/$work/m197-live.serial.log"
    fi
    case "${NAS_M204_REQUIRE_ALL:-0}" in
        0) python3 scripts/m204-compat-report.py \
               scripts/m204-alpine-cohort.tsv "$first_serial" "$reopen" \
               "$PWD/$work/m204-compat-report.json" ${report_replay_opt:+"$report_replay_opt"} ;;
        1) python3 scripts/m204-compat-report.py \
               scripts/m204-alpine-cohort.tsv "$first_serial" "$reopen" \
               "$PWD/$work/m204-compat-report.json" --require-all \
               ${report_replay_opt:+"$report_replay_opt"} ;;
        *) echo '[M204 FAIL] invalid strict mode' >&2; exit 2 ;;
    esac
fi
if [ "$m205" = 1 ]; then
    grep -Fq 'M205_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M205 FAIL] second QEMU did not reevaluate new ELFs' >&2
        exit 1
    }
    case "${NAS_M205_REQUIRE_ALL:-0}" in
        0) python3 scripts/m205-compat-report.py \
               scripts/m205-alpine-cohort.tsv \
               "$PWD/$work/m197-live.serial.log" "$reopen" \
               "$PWD/$work/m205-compat-report.json" ;;
        1) python3 scripts/m205-compat-report.py \
               scripts/m205-alpine-cohort.tsv \
               "$PWD/$work/m197-live.serial.log" "$reopen" \
               "$PWD/$work/m205-compat-report.json" --require-all ;;
        *) echo '[M205 FAIL] invalid strict mode' >&2; exit 2 ;;
    esac
fi
if [ "$m206" = 1 ]; then
    grep -Fq 'M206_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M206 FAIL] second QEMU did not reevaluate new ELFs' >&2
        exit 1
    }
    case "${NAS_M206_REQUIRE_ALL:-0}" in
        0) python3 scripts/m206-compat-report.py \
               scripts/m206-alpine-cohort.tsv \
               "$PWD/$work/m197-live.serial.log" "$reopen" \
               "$PWD/$work/m206-compat-report.json" ;;
        1) python3 scripts/m206-compat-report.py \
               scripts/m206-alpine-cohort.tsv \
               "$PWD/$work/m197-live.serial.log" "$reopen" \
               "$PWD/$work/m206-compat-report.json" --require-all ;;
        *) echo '[M206 FAIL] invalid strict mode' >&2; exit 2 ;;
    esac
fi
if [ "$m215" = 1 ]; then
    grep -Fq 'M215_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M215 FAIL] second QEMU did not re-execute five original ELFs' >&2
        exit 1
    }
    # This strict pilot covers only five executable M213 probes. A blocked
    # sixth case or false-positive timeouts cannot be accepted as PASS.
    python3 scripts/m215-compat-report.py \
        scripts/m215-candidate-elfs.tsv \
        "$PWD/$work/m197-live.serial.log" "$reopen" \
        "$PWD/$work/m215-compat-report.json" --require-all
fi
if [ "$m218" = 1 ]; then
    grep -Fq 'M218_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M218 FAIL] second QEMU did not rerun original ifstat' >&2
        exit 1
    }
    python3 scripts/m218-compat-report.py \
        scripts/m218-candidate-elfs.tsv \
        "$PWD/$work/m197-live.serial.log" "$reopen" \
        "$PWD/$work/m218-compat-report.json" --require-all
fi
if [ "$m219" = 1 ]; then
    grep -Fq 'M219_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M219 FAIL] second QEMU did not rerun original utmpdump' >&2
        exit 1
    }
    python3 scripts/m219-compat-report.py \
        scripts/m219-candidate-elfs.tsv \
        "$PWD/$work/m197-live.serial.log" "$reopen" \
        "$PWD/$work/m219-compat-report.json" --require-all
fi
if [ "$m220" = 1 ]; then
    grep -Fq 'M220_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M220 FAIL] second QEMU did not rerun original Nping' >&2
        exit 1
    }
    python3 scripts/m220-compat-report.py \
        scripts/m220-candidate-elfs.tsv \
        "$PWD/$work/m197-live.serial.log" "$reopen" \
        "$PWD/$work/m220-compat-report.json" --require-all
fi
if [ "$m221" = 1 ]; then
    grep -Fq 'M221_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M221 FAIL] second QEMU did not rerun signed nsupdate and host' >&2
        exit 1
    }
    python3 scripts/m221-compat-report.py \
        scripts/m221-candidate-elfs.tsv \
        "$PWD/$work/m197-live.serial.log" "$reopen" \
        "$PWD/$work/m221-compat-report.json" --require-all
fi
if [ "$m222" = 1 ]; then
    grep -Fq 'M222_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M222 FAIL] second original SSH/SCP/SFTP ledger incomplete' >&2; exit 1;
    }
    python3 scripts/m222-compat-report.py \
        scripts/m222-candidate-elfs.tsv \
        "$PWD/$work/m197-live.serial.log" "$reopen" \
        "$PWD/$work/m222-compat-report.json" --require-all
fi
if [ "$m223" = 1 ]; then
    grep -Fq 'M223_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M223 FAIL] second original HTTP client/server ledger incomplete' >&2; exit 1;
    }
    python3 scripts/m223-compat-report.py \
        scripts/m223-candidate-elfs.tsv \
        "$PWD/$work/m197-live.serial.log" "$reopen" \
        "$PWD/$work/m223-compat-report.json" --require-all
fi
if [ "$m198" = 1 ]; then
    grep -Fxq 'M198_GUI_REOPEN_PASS' "$reopen" || {
        echo '[M198 REOPEN FAIL] native editor did not persist' >&2; exit 1;
    }
fi
if [ "$m223" = 1 ]; then
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m223-alpine-disk-check.py verify "$port" "$root" "$disk" \
        "$PWD/$work/m223-host-first" "$PWD/$work/m223-host-reopen"
elif [ "$m222" = 1 ]; then
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m222-alpine-disk-check.py "$port" "$root" "$disk"
elif [ "$m221" = 1 ]; then
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m221-alpine-disk-check.py "$port" "$root" "$disk"
elif [ "$m220" = 1 ]; then
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m220-alpine-disk-check.py "$port" "$root" "$disk"
elif [ "$m219" = 1 ]; then
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m219-alpine-disk-check.py "$port" "$root" "$disk"
elif [ "$m218" = 1 ]; then
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m218-alpine-disk-check.py "$port" "$root" "$disk"
elif [ "$m215" = 1 ]; then
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m215-alpine-disk-check.py "$port" "$root" "$disk"
elif [ "$m206" = 1 ]; then
    # M206 invokes the unchanged M197 physical ext4/Git/SQLite/fsck oracle
    # before separately verifying all 20 new native ELF SHA and APK records.
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m206-alpine-disk-check.py "$port" "$root" "$disk"
else
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m197-alpine-disk-check.py "$port" "$root" "$disk"
fi
if [ "$m198" = 1 ]; then
    sudo python3 scripts/m198-alpine-disk-check.py "$port" "$root" "$disk"
fi
sudo env NAS_M191_DISK_MIB=256 \
    python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
if [ "$m204" = 1 ] && [ "$replay" = 0 ] && [ "${NAS_M204_CAPTURE_CHECKPOINT:-0}" = 1 ]; then
    # ONLY post-SECOND-boot clean ext4 is cached. The first Alpine init UART
    # supervisor intentionally SIGTERMs QEMU and cannot supply unmount proof.
    # The previous second guest ran and persisted all twelve test outcomes.
    capture_log="$PWD/$work/m204-checkpoint-capture.log"
    if sudo env GITHUB_SHA="${GITHUB_SHA:-}" python3 scripts/m204-alpine-checkpoint.py capture "$port" "$root" \
        "$disk" "$PWD/$work/m197-live.serial.log" "$pcap" "$reopen" \
        "$checkpoint" "$checkpoint_meta" "$checkpoint_serial" \
        "$checkpoint_pcap" "$checkpoint_reopen" > "$capture_log" 2>&1; then
        tail -10 "$capture_log"
        if grep -Fq '[M204 CHECKPOINT CAPTURED]' "$capture_log"; then
            : > "$PWD/$work/m204-checkpoint.fresh"
        fi
    else
        # Optional replay cache must never make the original signed HTTPS
        # acceptance fail. A bad cache simply cannot be saved or reused.
        echo '[M204 CHECKPOINT NOT SAVED] optional provenance/fsck oracle rejected snapshot'
        tail -80 "$capture_log" >&2
    fi
fi
if [ "$replay" = 1 ]; then
    echo "[M204 VERIFIED CHECKPOINT REPLAY COMPLETE] $port current kernel, "\
         'one genuine independent QEMU reopen, twelve previously installed ' \
         'official Alpine applications, Git+SQL and clean ext4; NO current-commit cold public install'
else
    echo "[M197 ALPINE PUBLIC SOFTWARE PERSISTENCE PASS] $port original "\
         'Alpine public signed four-package transaction, native Bash/Lua/SQLite/Git, ' \
         'second real QEMU, exact Git+SQL host disk oracle and ext4 fsck'
    if [ "$m204" = 1 ]; then
        echo "[M204 ALPINE COMPAT DISCOVERY COMPLETE] $port official Alpine "\
             'twelve-package results from both QEMU boots and JSON failure inventory'
    fi
    if [ "$m205" = 1 ]; then
        echo "[M205 ALPINE COMPAT DISCOVERY COMPLETE] $port official signed "\
             'new ELF entrypoints from two genuine QEMU boots; failures remain explicit'
    fi
    if [ "$m206" = 1 ]; then
        echo "[M206 ALPINE COMPAT DISCOVERY COMPLETE] $port official signed "\
             'network/storage/markup ELFs from two genuine QEMU boots; failures remain explicit'
    fi
    if [ "$m215" = 1 ]; then
        echo "[M215 ALPINE FIVE-ELF PILOT PASS] $port exact two signed APK "\
             'owners and five real original ELF functions in both native QEMUs; ' \
             'clean durable ext4 and original M197 public HTTPS preserved'
    fi
    if [ "$m218" = 1 ]; then
        echo "[M218 ALPINE IFSTAT PILOT PASS] $port signed original ifstat, "\
             'actual network counters on two independent QEMU boots, clean ' \
             'durable ext4 and original M197 public HTTPS preserved'
    fi
    if [ "$m219" = 1 ]; then
        echo "[M219 ALPINE UTMPDUMP PILOT PASS] $port signed original utmpdump, "\
             'authentic native binary records decoded and reencoded on two ' \
             'independent QEMU boots, durable ext4 and public HTTPS preserved'
    fi
    if [ "$m220" = 1 ]; then
        echo "[M220 ALPINE ORIGINAL NPING WIRE PASS] $port signed native Nping, "\
             'two different genuine host-received UDP probes and independent ' \
             'QEMU Ethernet PCAPs, clean ext4 and public HTTPS preserved'
    fi
    if [ "$m221" = 1 ]; then
        echo "[M221 ALPINE ORIGINAL DNS RFC2136 PASS] $port two signed "\
             'bind-tools ELFs, distinct host authoritative UPDATE/ACK and ' \
             'QUERY/ANSWER, two independent guest PCAPs and durable ext4'
    fi
    if [ "$m223" = 1 ]; then
        echo "[M223 ORIGINAL ALPINE HTTP TWO-BOOT PASS] $port signed wget/aria2c "\
             'real host-random GET + signed lighttpd loopback via wget, ' \
             'independent host logs/PCAP, physical ext4 bytes and M197 public HTTPS'
    fi
fi
if [ "$m198" = 1 ]; then
    echo "[M198 ALPINE GUI REBOOT PASS] $port original PID1, publicly signed "\
        'Bash/Lua/SQLite/Git and desktop dependencies, physical QMP/Vim, ' \
        'second real QEMU, exact host ext4 byte and fsck oracle'
fi
