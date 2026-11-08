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
m235=${NAS_M235_COMPAT_COHORT:-0}
case "$m235" in 0|1) ;; *) echo '[M235 FAIL] invalid batch selector' >&2; exit 2 ;; esac
m236=${NAS_M236_COMPAT_COHORT:-0}
case "$m236" in 0|1) ;; *) echo '[M236 FAIL] invalid batch selector' >&2; exit 2 ;; esac
m237=${NAS_M237_COMPAT_COHORT:-0}
case "$m237" in 0|1) ;; *) echo '[M237 FAIL] invalid batch selector' >&2; exit 2 ;; esac
m238=${NAS_M238_COMPAT_COHORT:-0}
case "$m238" in 0|1) ;; *) echo '[M238 FAIL] invalid batch selector' >&2; exit 2 ;; esac
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
m224=${NAS_M224_PILOT:-0}
case "$m224" in 0|1) ;; *) echo '[M224 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
m225=${NAS_M225_PILOT:-0}
case "$m225" in 0|1) ;; *) echo '[M225 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
m226=${NAS_M226_PILOT:-0}
case "$m226" in 0|1) ;; *) echo '[M226 FAIL] invalid pilot selector' >&2; exit 2 ;; esac
m227=${NAS_M227_INTEGRATED_OPENRC:-0}
case "$m227" in 0|1) ;; *) echo '[M227 FAIL] invalid integrated OpenRC selector' >&2; exit 2 ;; esac
m228=${NAS_M228_HOST_INBOUND:-0}
case "$m228" in 0|1) ;; *) echo '[M228 FAIL] invalid host ingress selector' >&2; exit 2 ;; esac
m229=${NAS_M229_VALKEY_HOST:-0}
case "$m229" in 0|1) ;; *) echo '[M229 FAIL] invalid host RESP selector' >&2; exit 2 ;; esac
m230=${NAS_M230_DROPBEAR_HOST:-0}
case "$m230" in 0|1) ;; *) echo '[M230 FAIL] invalid host SSH selector' >&2; exit 2 ;; esac
m231=${NAS_M231_MOSQUITTO_HOST:-0}
case "$m231" in 0|1) ;; *) echo '[M231 FAIL] invalid host MQTT selector' >&2; exit 2 ;; esac
m232=${NAS_M232_MEMCACHED_HOST:-0}
case "$m232" in 0|1) ;; *) echo '[M232 FAIL] invalid host cache selector' >&2; exit 2 ;; esac
m232_host_pid=
m232_stop_host() {
    if [ -n "$m232_host_pid" ]; then
        kill "$m232_host_pid" 2>/dev/null || :
        wait "$m232_host_pid" 2>/dev/null || :
        m232_host_pid=
    fi
}
m233=${NAS_M233_DNSMASQ_HOST:-0}
case "$m233" in 0|1) ;; *) echo '[M233 FAIL] invalid host UDP+TCP DNS selector' >&2; exit 2 ;; esac
m233_host_pid=
m233_stop_host() {
    if [ -n "$m233_host_pid" ]; then
        kill "$m233_host_pid" 2>/dev/null || :
        wait "$m233_host_pid" 2>/dev/null || :
        m233_host_pid=
    fi
}
if [ "$m232" = 1 ] || [ "$m233" = 1 ]; then
    if [ "$m232" = 1 ]; then
        trap 'm232_stop_host' EXIT
    else
        trap 'm233_stop_host' EXIT
    fi
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
fi
m231_host_pid=
m231_stop_host() {
    if [ -n "$m231_host_pid" ]; then
        kill "$m231_host_pid" 2>/dev/null || :
        wait "$m231_host_pid" 2>/dev/null || :
        m231_host_pid=
    fi
}
if [ "$m231" = 1 ]; then
    trap 'm231_stop_host' EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
fi
m230_host_pid=
m230_client_dir=
m230_stop_host() {
    if [ -n "$m230_host_pid" ]; then
        kill "$m230_host_pid" 2>/dev/null || :
        wait "$m230_host_pid" 2>/dev/null || :
        m230_host_pid=
    fi
}
m230_cleanup() {
    m230_stop_host
    if [ -n "$m230_client_dir" ]; then
        # Scrub only this run's private host identity, never guest ext4.
        rm -f -- "$m230_client_dir/id_ed25519" \
            "$m230_client_dir/id_ed25519.pub" \
            "$m230_client_dir/known_hosts"
        rmdir -- "$m230_client_dir" 2>/dev/null || :
        m230_client_dir=
    fi
}
if [ "$m230" = 1 ]; then
    trap 'm230_cleanup' EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
fi
m229_host_pid=
m229_secret_file=
m229_stop_host() {
    if [ -n "$m229_host_pid" ]; then
        kill "$m229_host_pid" 2>/dev/null || :
        wait "$m229_host_pid" 2>/dev/null || :
        m229_host_pid=
    fi
}
m229_cleanup() {
    m229_stop_host
    if [ -n "$m229_secret_file" ]; then
        rm -f -- "$m229_secret_file"
        m229_secret_file=
    fi
}

if [ "$m229" = 1 ]; then
    trap 'm229_cleanup' EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
fi
if { [ "$m204" = 1 ] || [ "$m205" = 1 ] || [ "$m206" = 1 ] || [ "$m215" = 1 ] || [ "$m218" = 1 ] || [ "$m219" = 1 ] || [ "$m220" = 1 ] || [ "$m221" = 1 ] || [ "$m222" = 1 ] || [ "$m223" = 1 ] || [ "$m224" = 1 ] || [ "$m225" = 1 ] || [ "$m226" = 1 ] || [ "$m227" = 1 ] || [ "$m228" = 1 ] || [ "$m229" = 1 ] || [ "$m230" = 1 ] || [ "$m231" = 1 ] || [ "$m232" = 1 ] || [ "$m233" = 1 ] || [ "$m235" = 1 ] || [ "$m236" = 1 ] || [ "$m237" = 1 ] || [ "$m238" = 1 ]; } && \
   { [ "$m198" = 1 ] || [ "$m199" = 1 ]; }; then
    echo '[COMPAT FAIL] software cohort cannot enable a desktop' >&2
    exit 2
fi
if [ "$((m204+m205+m206+m215+m218+m219+m220+m221+m222+m223+m224+m225+m226+m227+m228+m229+m230+m231+m232+m233+m235+m236+m237+m238))" -gt 1 ]; then
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
if [ "$m215" = 1 ] || [ "$m218" = 1 ] || [ "$m219" = 1 ] || [ "$m220" = 1 ] || [ "$m221" = 1 ] || [ "$m222" = 1 ] || [ "$m223" = 1 ] || [ "$m224" = 1 ] || [ "$m225" = 1 ] || [ "$m226" = 1 ] || [ "$m227" = 1 ] || [ "$m228" = 1 ] || [ "$m229" = 1 ] || [ "$m230" = 1 ] || [ "$m231" = 1 ] || [ "$m233" = 1 ] || [ "$m235" = 1 ] || [ "$m236" = 1 ] || [ "$m237" = 1 ] || [ "$m238" = 1 ]; then
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
if [ "$m223" = 1 ] || [ "$m235" = 1 ]; then
    # Actual host-only random HTTP witnesses stay outside signed Alpine
    # staging. EXIT always kills this exact independent HTTP server.
    . scripts/m223-http-host.sh
    trap 'm223_cleanup' EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
fi
if [ "$m224" = 1 ]; then
    # Each independent native boot owns unique external host-only TCP bytes,
    # genuine iperf3 server and open/closed Nmap witness sockets.
    . scripts/m224-tcp-host.sh
    trap 'm224_cleanup' EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
fi
if [ "$m225" = 1 ]; then
    # FTP origins and uploads are host-only, per-boot random challenges.
    # Original signed Alpine clients see them only through real slirp TCP.
    . scripts/m225-ftp-host.sh
    trap 'm225_cleanup' EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
fi
if [ "$m226" = 1 ]; then
    # Two separate native QEMUs share only the genuine physical ext4 and
    # official upstream rest-server repository, NEVER a preseeded source.
    . scripts/m226-rest-server-host.sh
    trap 'm226_cleanup' EXIT
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
if [ "$m227" = 1 ]; then
    sudo install -m 0644 tests/m227-alpine-openrc-workload.sh \
        "$overlay/root/m227-alpine-openrc-workload.sh"
    sudo test -s "$overlay/root/m227-alpine-openrc-workload.sh"
    sudo test ! -e "$overlay/var/www/m227" || {
        echo '[M227 FAIL] HTTP response was preseeded' >&2; exit 1;
    }
    sudo test ! -e "$overlay/etc/runlevels/default/lighttpd" || {
        echo '[M227 FAIL] OpenRC service was pre-enabled' >&2; exit 1;
    }
fi
if [ "$m228" = 1 ]; then
    # Keep M227's complete original signed OpenRC guest workload and add an
    # independently challenged real host-to-guest HTTP request window.
    sudo install -m 0644 tests/m227-alpine-openrc-workload.sh \
        "$overlay/root/m227-alpine-openrc-workload.sh"
    sudo install -m 0644 tests/m228-alpine-host-ingress-workload.sh \
        "$overlay/root/m228-alpine-host-ingress-workload.sh"
    sudo test -s "$overlay/root/m227-alpine-openrc-workload.sh"
    sudo test -s "$overlay/root/m228-alpine-host-ingress-workload.sh"
    sudo test ! -e "$overlay/var/www/m227" || {
        echo '[M228 FAIL] original lighttpd document preseeded' >&2; exit 1;
    }
    sudo test ! -e "$overlay/etc/runlevels/default/lighttpd" || {
        echo '[M228 FAIL] OpenRC service pre-enabled' >&2; exit 1;
    }
fi
if [ "$m229" = 1 ]; then
    # Reuse the complete original signed M227 OpenRC/networking workload.
    # The only extra staging input is a fresh one-run Valkey AUTH identity;
    # the actual Valkey binaries, database and OpenRC links MUST be guest-made.
    sudo install -m 0644 tests/m227-alpine-openrc-workload.sh \
        "$overlay/root/m227-alpine-openrc-workload.sh"
    sudo install -m 0644 tests/m229-alpine-valkey-workload.sh \
        "$overlay/root/m229-alpine-valkey-workload.sh"
    sudo test -s "$overlay/root/m227-alpine-openrc-workload.sh"
    sudo test -s "$overlay/root/m229-alpine-valkey-workload.sh"
    sudo test ! -e "$overlay/etc/runlevels/default/valkey" || {
        echo '[M229 FAIL] original OpenRC Valkey service preenabled' >&2; exit 1;
    }
    sudo test ! -e "$overlay/var/lib/valkey" || {
        echo '[M229 FAIL] Valkey database preseeded' >&2; exit 1;
    }
    sudo test ! -e "$overlay/root/m229-auth-token" || {
        echo '[M229 FAIL] Valkey AUTH credential preseeded' >&2; exit 1;
    }
    command -v openssl >/dev/null || {
        echo '[M229 HOST FAIL] openssl random generator missing' >&2; exit 1;
    }
    m229_password=$(openssl rand -hex 32)
    test "${#m229_password}" -eq 64 || {
        echo '[M229 HOST FAIL] invalid random AUTH identity length' >&2; exit 1;
    }
    case "$m229_password" in
        *[!0-9a-f]*|'') echo '[M229 HOST FAIL] invalid random AUTH identity' >&2; exit 1 ;;
    esac
    m229_secret_file=$(mktemp "$PWD/$work/m229-auth.XXXXXXXX")
    printf '%s\n' "$m229_password" > "$m229_secret_file"
    chmod 0600 "$m229_secret_file"
    sudo install -m 0600 -o 0 -g 0 "$m229_secret_file" \
        "$overlay/root/m229-auth-token"
    sudo test -s "$overlay/root/m229-auth-token"
fi
if [ "$m230" = 1 ]; then
    # Original Alpine APK and OpenRC install and start Dropbear inside the
    # FIRST guest. Only the HOST public client identity crosses to guest.
    # NaSeed still lacks full non-root DAC: this is NOT production-safe SSH.
    sudo install -m 0644 tests/m227-alpine-openrc-workload.sh \
        "$overlay/root/m227-alpine-openrc-workload.sh"
    sudo install -m 0644 tests/m230-alpine-dropbear-workload.sh \
        "$overlay/root/m230-alpine-dropbear-workload.sh"
    sudo test -s "$overlay/root/m227-alpine-openrc-workload.sh"
    sudo test -s "$overlay/root/m230-alpine-dropbear-workload.sh"
    sudo test ! -e "$overlay/etc/runlevels/default/dropbear" || {
        echo '[M230 FAIL] Dropbear OpenRC service preenabled' >&2; exit 1;
    }
    sudo test ! -e "$overlay/root/.ssh/authorized_keys" && \
        sudo test ! -e "$overlay/root/m230-authorized-pubkey" || {
        echo '[M230 FAIL] SSH public identity preseeded' >&2; exit 1;
    }
    command -v ssh-keygen >/dev/null || {
        echo '[M230 HOST FAIL] ssh-keygen missing' >&2; exit 1;
    }
    m230_client_dir=$(mktemp -d "$PWD/$work/m230-host-key.XXXXXXXX")
    chmod 0700 "$m230_client_dir"
    ssh-keygen -q -t ed25519 -a 16 -N '' -f "$m230_client_dir/id_ed25519" \
        >/dev/null
    test -s "$m230_client_dir/id_ed25519" && \
        test -s "$m230_client_dir/id_ed25519.pub" || {
        echo '[M230 HOST FAIL] ephemeral Ed25519 key generation failed' >&2; exit 1;
    }
    sudo install -m 0644 -o 0 -g 0 "$m230_client_dir/id_ed25519.pub" \
        "$overlay/root/m230-authorized-pubkey"
    sudo test -s "$overlay/root/m230-authorized-pubkey"
fi
if [ "$m231" = 1 ]; then
    # Original signed APKs and original Alpine OpenRC are guest-created only;
    # no broker/configuration/database/retained payload is staged in the image.
    sudo install -m 0644 tests/m227-alpine-openrc-workload.sh \
        "$overlay/root/m227-alpine-openrc-workload.sh"
    sudo install -m 0644 tests/m231-alpine-mosquitto-workload.sh \
        "$overlay/root/m231-alpine-mosquitto-workload.sh"
    sudo test -s "$overlay/root/m227-alpine-openrc-workload.sh"
    sudo test -s "$overlay/root/m231-alpine-mosquitto-workload.sh"
    for guest in /etc/mosquitto/m231.conf /var/lib/mosquitto/m231.db \
            /etc/runlevels/default/mosquitto; do
        sudo test ! -e "$overlay$guest" || {
            echo "[M231 FAIL] preseeded MQTT guest state: $guest" >&2; exit 1;
        }
    done
fi
if [ "$m232" = 1 ]; then
    # The signed memcached ELF and original OpenRC unit are installed only
    # inside the original PID1. This temporary cache starts EMPTY on reboot.
    sudo install -m 0644 tests/m227-alpine-openrc-workload.sh \
        "$overlay/root/m227-alpine-openrc-workload.sh"
    sudo install -m 0644 tests/m232-alpine-memcached-workload.sh \
        "$overlay/root/m232-alpine-memcached-workload.sh"
    sudo test -s "$overlay/root/m227-alpine-openrc-workload.sh"
    sudo test -s "$overlay/root/m232-alpine-memcached-workload.sh"
    sudo test ! -e "$overlay/etc/runlevels/default/memcached" || {
        echo '[M232 FAIL] original OpenRC memcached service preenabled' >&2; exit 1;
    }
    sudo test ! -e "$overlay/root/m232-cache-record" || {
        echo '[M232 FAIL] host cache challenge preseeded' >&2; exit 1;
    }
fi
if [ "$m233" = 1 ]; then
    # No original DNS daemon, OpenRC link or DNS fixture config is preseeded.
    # First real signed Alpine PID1 installs and generates them itself.
    sudo install -m 0644 tests/m227-alpine-openrc-workload.sh \
        "$overlay/root/m227-alpine-openrc-workload.sh"
    sudo install -m 0644 tests/m233-alpine-dnsmasq-workload.sh \
        "$overlay/root/m233-alpine-dnsmasq-workload.sh"
    for guest in /etc/runlevels/default/dnsmasq /root/m233-dns-first-receipt; do
        sudo test ! -e "$overlay$guest" || {
            echo "[M233 FAIL] preseeded DNS state: $guest" >&2; exit 1;
        }
    done
fi
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
if [ "$m205" = 1 ] || [ "$m206" = 1 ] || [ "$m215" = 1 ] || [ "$m218" = 1 ] || [ "$m219" = 1 ] || [ "$m220" = 1 ] || [ "$m221" = 1 ] || [ "$m222" = 1 ] || [ "$m223" = 1 ] || [ "$m224" = 1 ] || [ "$m225" = 1 ] || [ "$m226" = 1 ] || [ "$m235" = 1 ] || [ "$m236" = 1 ] || [ "$m237" = 1 ] || [ "$m238" = 1 ]; then
    # Reuse the M205 signed-install/ledger owner for both independent cohorts;
    # the M206 module contributes only new original-program semantic probes.
    sudo install -m 0644 tests/m205-software-workload.sh \
        "$overlay/root/m205-software-workload.sh"
    if [ "$m238" = 1 ]; then
        cohort=m238
        sudo install -m 0644 tests/m238-software-workload.sh \
            "$overlay/root/m238-software-workload.sh"
        sudo install -m 0644 scripts/m238-candidate-elfs.tsv \
            "$overlay/root/m238-cohort.tsv"
        sudo test -s "$overlay/root/m238-software-workload.sh"
        sudo test -s "$overlay/root/m238-cohort.tsv"
    elif [ "$m237" = 1 ]; then
        cohort=m237
        sudo install -m 0644 tests/m237-software-workload.sh \
            "$overlay/root/m237-software-workload.sh"
        sudo install -m 0644 scripts/m237-candidate-elfs.tsv \
            "$overlay/root/m237-cohort.tsv"
        sudo test -s "$overlay/root/m237-software-workload.sh"
        sudo test -s "$overlay/root/m237-cohort.tsv"
    elif [ "$m236" = 1 ]; then
        cohort=m236
        sudo install -m 0644 tests/m236-software-workload.sh \
            "$overlay/root/m236-software-workload.sh"
        sudo install -m 0644 scripts/m236-candidate-elfs.tsv \
            "$overlay/root/m236-cohort.tsv"
        sudo test -s "$overlay/root/m236-software-workload.sh"
        sudo test -s "$overlay/root/m236-cohort.tsv"
    elif [ "$m235" = 1 ]; then
    cohort=m235
    sudo install -m 0644 tests/m235-software-workload.sh \
        "$overlay/root/m235-software-workload.sh"
    sudo install -m 0644 scripts/m235-candidate-elfs.tsv \
        "$overlay/root/m235-cohort.tsv"
    sudo test -s "$overlay/root/m235-software-workload.sh"
    sudo test -s "$overlay/root/m235-cohort.tsv"
    elif [ "$m226" = 1 ]; then
    cohort=m226
    sudo install -m 0644 tests/m226-restic-workload.sh \
        "$overlay/root/m226-restic-workload.sh"
    sudo install -m 0644 tests/m226-software-workload.sh \
        "$overlay/root/m226-software-workload.sh"
    sudo install -m 0644 scripts/m226-candidate-elfs.tsv \
        "$overlay/root/m226-cohort.tsv"
    sudo test -s "$overlay/root/m226-restic-workload.sh"
    sudo test -s "$overlay/root/m226-software-workload.sh"
    sudo test -s "$overlay/root/m226-cohort.tsv"
    sudo python3 scripts/m226-alpine-disk-check.py stage "$port" "$overlay"
    elif [ "$m225" = 1 ]; then
    cohort=m225
    sudo install -m 0644 tests/m225-ftp-workload.sh \
        "$overlay/root/m225-ftp-workload.sh"
    sudo install -m 0644 tests/m225-software-workload.sh \
        "$overlay/root/m225-software-workload.sh"
    sudo install -m 0644 scripts/m225-candidate-elfs.tsv \
        "$overlay/root/m225-cohort.tsv"
    sudo test -s "$overlay/root/m225-ftp-workload.sh"
    sudo test -s "$overlay/root/m225-software-workload.sh"
    sudo test -s "$overlay/root/m225-cohort.tsv"
    sudo python3 scripts/m225-alpine-disk-check.py stage "$port" "$overlay"
    elif [ "$m224" = 1 ]; then
    cohort=m224
    sudo install -m 0644 tests/m224-tcp-workload.sh \
        "$overlay/root/m224-tcp-workload.sh"
    sudo install -m 0644 scripts/m224-candidate-elfs.tsv \
        "$overlay/root/m224-cohort.tsv"
    sudo test -s "$overlay/root/m224-tcp-workload.sh"
    sudo test -s "$overlay/root/m224-cohort.tsv"
    # Signed original native candidates must not appear in the stage,
    # apart from a precisely verified genuine BusyBox alias (if observed).
    sudo python3 scripts/m224-alpine-disk-check.py stage "$port" "$overlay"
    elif [ "$m223" = 1 ]; then
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
if [ "$m228" = 1 ]; then
    # Opt in AFTER probe-m191 has built the exact unchanged target kernel.
    # The runner appends hostfwd to the EXISTING NIC, not a second adapter.
    export NAS_VIRTIO_NET_HOSTFWD_PORT=${NAS_M228_HOST_PORT:-15433}
fi
if [ "$m229" = 1 ]; then
    # M229 is still a single real VirtIO-Net NIC and an isolated localhost
    # hostfwd, but it targets the original signed Valkey RESP port.
    export NAS_VIRTIO_NET_HOSTFWD_PORT=${NAS_M229_HOST_PORT:-15445}
    export NAS_VIRTIO_NET_GUESTFWD_PORT=15445
fi
if [ "$m230" = 1 ]; then
    # Separate host and guest SSH ports, same SINGLE real slirp VirtIO NIC.
    # Host listener remains strictly bound to 127.0.0.1 by QEMU's runner.
    export NAS_VIRTIO_NET_HOSTFWD_PORT=${NAS_M230_HOST_PORT:-12222}
    export NAS_VIRTIO_NET_GUESTFWD_PORT=15446
fi
if [ "$m231" = 1 ]; then
    # The ONE real QEMU VirtIO-Net device exposes broker 15447 exclusively
    # to host loopback: bounded disposable no-auth functional validation.
    export NAS_VIRTIO_NET_HOSTFWD_PORT=${NAS_M231_HOST_PORT:-15447}
    export NAS_VIRTIO_NET_GUESTFWD_PORT=15447
fi
if [ "$m232" = 1 ]; then
    # Host-only TCP 15448 -> the existing single original VirtIO NIC.
    export NAS_VIRTIO_NET_HOSTFWD_PORT=${NAS_M232_HOST_PORT:-15448}
    export NAS_VIRTIO_NET_GUESTFWD_PORT=15448
fi
if [ "$m233" = 1 ]; then
    # One real slirp NIC; both host loopback protocols target the SAME
    # original signed dnsmasq server. No public listener or extra adapter.
    export NAS_VIRTIO_NET_HOSTFWD_PORT=${NAS_M233_TCP_HOST_PORT:-15453}
    export NAS_VIRTIO_NET_GUESTFWD_PORT=15453
    export NAS_VIRTIO_NET_HOSTFWD_UDP_PORT=${NAS_M233_UDP_HOST_PORT:-15453}
    export NAS_VIRTIO_NET_GUESTFWD_UDP_PORT=15453
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
if [ "$m235" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M235_PUBLIC_QEMU_TIMEOUT:-4500}
fi
if [ "$m236" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M236_PUBLIC_QEMU_TIMEOUT:-4500}
fi
if [ "$m237" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M237_PUBLIC_QEMU_TIMEOUT:-4500}
fi
if [ "$m238" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M238_PUBLIC_QEMU_TIMEOUT:-4500}
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
if [ "$m224" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M224_PUBLIC_QEMU_TIMEOUT:-2500}
fi
if [ "$m225" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M225_PUBLIC_QEMU_TIMEOUT:-3200}
fi
if [ "$m226" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M226_PUBLIC_QEMU_TIMEOUT:-3900}
fi
if [ "$m227" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M227_PUBLIC_QEMU_TIMEOUT:-1800}
fi
if [ "$m228" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M228_PUBLIC_QEMU_TIMEOUT:-2650}
fi
if [ "$m229" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M229_PUBLIC_QEMU_TIMEOUT:-2650}
fi
if [ "$m230" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M230_PUBLIC_QEMU_TIMEOUT:-2850}
fi
if [ "$m231" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M231_PUBLIC_QEMU_TIMEOUT:-2750}
fi
if [ "$m232" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M232_PUBLIC_QEMU_TIMEOUT:-2750}
fi
if [ "$m233" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M233_PUBLIC_QEMU_TIMEOUT:-2750}
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
if [ "$m223" = 1 ] || [ "$m235" = 1 ]; then
    m223_start_host first
fi
if [ "$m224" = 1 ]; then
    command -v iperf3 >/dev/null || {
        echo '[M224 HOST FAIL] original host iperf3 TCP server missing' >&2
        exit 1
    }
    m224_start_host first
fi
if [ "$m225" = 1 ]; then
    m225_start_host first
fi
if [ "$m226" = 1 ]; then
    m226_start_host first
fi

if [ "$m228" = 1 ]; then
    # Exactly one independent host observer per native QEMU. It waits for
    # the signed guest's real UART READY line before initiating host TCP GET.
    m228_host_pid=
    m228_stop_host() {
        if [ -n "$m228_host_pid" ]; then
            kill "$m228_host_pid" 2>/dev/null || :
            wait "$m228_host_pid" 2>/dev/null || :
            m228_host_pid=
        fi
    }
    m228_start_host() {
        phase=$1
        host_serial=$2
        receipt="$PWD/$work/m228-$phase-host-inbound.json"
        host_log="$PWD/$work/m228-$phase-host-inbound.log"
        test ! -e "$receipt" && test ! -e "$host_log" || {
            echo '[M228 HOST FAIL] refusing stale independent host evidence' >&2; exit 1;
        }
        python3 -u scripts/m228-host-inbound-check.py "$phase" "$host_serial" \
            "$NAS_VIRTIO_NET_HOSTFWD_PORT" "$receipt" \
            --ready-timeout "${NAS_M228_HOST_READY_SECONDS:-2650}" \
            --http-timeout "${NAS_M228_HOST_HTTP_SECONDS:-35}" \
            > "$host_log" 2>&1 &
        m228_host_pid=$!
    }
    m228_finish_host() {
        phase=$1
        test -n "$m228_host_pid" || {
            echo '[M228 HOST FAIL] no live host watcher' >&2; exit 1;
        }
        # UART has already completed the guest's bounded 45-second window.
        # An absent host receipt can never be repaired by waiting another
        # 45-minute ready-timeout after QEMU has EXITED. Bound this join.
        m228_join_seconds=0
        while [ ! -s "$PWD/$work/m228-$phase-host-inbound.json" ] && \
              kill -0 "$m228_host_pid" 2>/dev/null && \
              [ "$m228_join_seconds" -lt 8 ]; do
            sleep 1
            m228_join_seconds=$((m228_join_seconds+1))
        done
        if [ ! -s "$PWD/$work/m228-$phase-host-inbound.json" ]; then
            echo "[M228 HOST FAIL] $phase QEMU exited without actual host HTTP receipt" >&2
            m228_stop_host
            tail -100 "$PWD/$work/m228-$phase-host-inbound.log" >&2 || :
            exit 1
        fi
        if wait "$m228_host_pid"; then
            m228_host_pid=
            test -s "$PWD/$work/m228-$phase-host-inbound.json" || {
                echo '[M228 HOST FAIL] actual host HTTP receipt absent' >&2; exit 1;
            }
        else
            m228_host_pid=
            echo "[M228 HOST FAIL] $phase real hostfwd HTTP observation" >&2
            tail -100 "$PWD/$work/m228-$phase-host-inbound.log" >&2 || :
            exit 1
        fi
    }
    trap 'm228_stop_host' EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
fi
if [ "$m229" = 1 ]; then
    # The HOST initiates the signed Valkey RESP transaction only after the
    # actual Alpine PID1 guest prints its readiness line on the real UART.
    # The per-run password is never put in receipts or the command line.
    m229_start_host() {
        phase=$1
        host_serial=$2
        receipt="$PWD/$work/m229-$phase-host-resp.json"
        host_log="$PWD/$work/m229-$phase-host-resp.log"
        test ! -e "$receipt" && test ! -e "$host_log" || {
            echo "[M229 HOST FAIL] stale $phase RESP proof" >&2; exit 1;
        }
        if [ "$phase" = reopen ]; then
            NAS_M229_RESP_PASSWORD="$m229_password" \
                python3 -u scripts/m229-host-resp-check.py "$phase" "$host_serial" \
                "$NAS_VIRTIO_NET_HOSTFWD_PORT" "$receipt" \
                --first-receipt "$PWD/$work/m229-first-host-resp.json" \
                --ready-timeout "${NAS_M229_HOST_READY_SECONDS:-2650}" \
                --resp-timeout "${NAS_M229_HOST_RESP_SECONDS:-35}" \
                > "$host_log" 2>&1 &
        else
            NAS_M229_RESP_PASSWORD="$m229_password" \
                python3 -u scripts/m229-host-resp-check.py "$phase" "$host_serial" \
                "$NAS_VIRTIO_NET_HOSTFWD_PORT" "$receipt" \
                --ready-timeout "${NAS_M229_HOST_READY_SECONDS:-2650}" \
                --resp-timeout "${NAS_M229_HOST_RESP_SECONDS:-35}" \
                > "$host_log" 2>&1 &
        fi
        m229_host_pid=$!
    }
    m229_finish_host() {
        phase=$1
        test -n "$m229_host_pid" || {
            echo '[M229 HOST FAIL] host RESP watcher was never started' >&2; exit 1;
        }
        join_seconds=0
        while [ ! -s "$PWD/$work/m229-$phase-host-resp.json" ] && \
              kill -0 "$m229_host_pid" 2>/dev/null && \
              [ "$join_seconds" -lt 8 ]; do
            sleep 1
            join_seconds=$((join_seconds+1))
        done
        if [ ! -s "$PWD/$work/m229-$phase-host-resp.json" ]; then
            echo "[M229 HOST FAIL] $phase native QEMU had no external RESP receipt" >&2
            m229_stop_host
            tail -100 "$PWD/$work/m229-$phase-host-resp.log" >&2 || :
            exit 1
        fi
        if wait "$m229_host_pid"; then
            m229_host_pid=
        else
            m229_host_pid=
            echo "[M229 HOST FAIL] $phase real guest Valkey RESP transaction" >&2
            tail -100 "$PWD/$work/m229-$phase-host-resp.log" >&2 || :
            exit 1
        fi
    }
fi

if [ "$m230" = 1 ]; then
    # A real host OpenSSH client waits for the original Alpine PID1 guest's
    # live UART marker, then performs real Dropbear host-key/public-key auth
    # and a nonce-bound remote command. No synthetic guest SSH server.
    m230_start_host() {
        phase=$1
        host_serial=$2
        receipt="$PWD/$work/m230-$phase-host-ssh.json"
        host_log="$PWD/$work/m230-$phase-host-ssh.log"
        test ! -e "$receipt" && test ! -e "$host_log" || {
            echo "[M230 HOST FAIL] refusing stale $phase SSH evidence" >&2; exit 1;
        }
        if [ "$phase" = reopen ]; then
            python3 -u scripts/m230-host-ssh-check.py "$phase" "$host_serial" \
                "$NAS_VIRTIO_NET_HOSTFWD_PORT" "$receipt" \
                --client-key "$m230_client_dir/id_ed25519" \
                --known-hosts "$m230_client_dir/known_hosts" \
                --first-receipt "$PWD/$work/m230-first-host-ssh.json" \
                --ready-timeout "${NAS_M230_HOST_READY_SECONDS:-2850}" \
                --ssh-timeout "${NAS_M230_HOST_SSH_SECONDS:-135}" \
                > "$host_log" 2>&1 &
        else
            python3 -u scripts/m230-host-ssh-check.py "$phase" "$host_serial" \
                "$NAS_VIRTIO_NET_HOSTFWD_PORT" "$receipt" \
                --client-key "$m230_client_dir/id_ed25519" \
                --known-hosts "$m230_client_dir/known_hosts" \
                --ready-timeout "${NAS_M230_HOST_READY_SECONDS:-2850}" \
                --ssh-timeout "${NAS_M230_HOST_SSH_SECONDS:-135}" \
                > "$host_log" 2>&1 &
        fi
        m230_host_pid=$!
    }
    m230_finish_host() {
        phase=$1
        test -n "$m230_host_pid" || {
            echo '[M230 HOST FAIL] no live SSH watcher' >&2; exit 1;
        }
        join_seconds=0
        while [ ! -s "$PWD/$work/m230-$phase-host-ssh.json" ] && \
              kill -0 "$m230_host_pid" 2>/dev/null && \
              [ "$join_seconds" -lt 8 ]; do
            sleep 1
            join_seconds=$((join_seconds+1))
        done
        if [ ! -s "$PWD/$work/m230-$phase-host-ssh.json" ]; then
            echo "[M230 HOST FAIL] $phase native QEMU lacks real SSH receipt" >&2
            m230_stop_host
            tail -100 "$PWD/$work/m230-$phase-host-ssh.log" >&2 || :
            exit 1
        fi
        if wait "$m230_host_pid"; then
            m230_host_pid=
        else
            m230_host_pid=
            echo "[M230 HOST FAIL] $phase authenticated Dropbear command" >&2
            tail -100 "$PWD/$work/m230-$phase-host-ssh.log" >&2 || :
            exit 1
        fi
    }
fi

if [ "$m231" = 1 ]; then
    # Real host MQTT clients wait for the currently running original PID1
    # UART ready signal; no synthetic broker or guest-issued challenge.
    m231_start_host() {
        phase=$1
        host_serial=$2
        receipt="$PWD/$work/m231-$phase-host-mqtt.json"
        host_log="$PWD/$work/m231-$phase-host-mqtt.log"
        test ! -e "$receipt" && test ! -e "$host_log" || {
            echo "[M231 HOST FAIL] stale $phase MQTT proof" >&2; exit 1;
        }
        if [ "$phase" = reopen ]; then
            python3 -u scripts/m231-host-mqtt-check.py "$phase" "$host_serial" \
                "$NAS_VIRTIO_NET_HOSTFWD_PORT" "$receipt" \
                --first-receipt "$PWD/$work/m231-first-host-mqtt.json" \
                --ready-timeout "${NAS_M231_HOST_READY_SECONDS:-2750}" \
                --mqtt-timeout "${NAS_M231_HOST_MQTT_SECONDS:-45}" \
                > "$host_log" 2>&1 &
        else
            python3 -u scripts/m231-host-mqtt-check.py "$phase" "$host_serial" \
                "$NAS_VIRTIO_NET_HOSTFWD_PORT" "$receipt" \
                --ready-timeout "${NAS_M231_HOST_READY_SECONDS:-2750}" \
                --mqtt-timeout "${NAS_M231_HOST_MQTT_SECONDS:-45}" \
                > "$host_log" 2>&1 &
        fi
        m231_host_pid=$!
    }
    m231_finish_host() {
        phase=$1
        test -n "$m231_host_pid" || {
            echo '[M231 HOST FAIL] host MQTT observer absent' >&2; exit 1;
        }
        join_seconds=0
        while [ ! -s "$PWD/$work/m231-$phase-host-mqtt.json" ] && \
              kill -0 "$m231_host_pid" 2>/dev/null && \
              [ "$join_seconds" -lt 8 ]; do
            sleep 1
            join_seconds=$((join_seconds+1))
        done
        if [ ! -s "$PWD/$work/m231-$phase-host-mqtt.json" ]; then
            echo "[M231 HOST FAIL] $phase original QEMU lacks real MQTT receipt" >&2
            m231_stop_host
            tail -100 "$PWD/$work/m231-$phase-host-mqtt.log" >&2 || :
            exit 1
        fi
        if wait "$m231_host_pid"; then
            m231_host_pid=
        else
            m231_host_pid=
            echo "[M231 HOST FAIL] $phase true retained MQTT transaction" >&2
            tail -100 "$PWD/$work/m231-$phase-host-mqtt.log" >&2 || :
            exit 1
        fi
    }
fi

if [ "$m232" = 1 ]; then
    # Only the independent real HOST chooses key/value and proves the cold
    # reboot LOST the previous volatile cache, then re-seeds and GETs it.
    m232_start_host() {
        phase=$1
        host_serial=$2
        receipt="$PWD/$work/m232-$phase-host-cache.json"
        host_log="$PWD/$work/m232-$phase-host-cache.log"
        test ! -e "$receipt" && test ! -e "$host_log" || {
            echo "[M232 HOST FAIL] stale $phase host cache evidence" >&2; exit 1;
        }
        if [ "$phase" = reopen ]; then
            python3 -u scripts/m232-host-cache-check.py "$phase" "$host_serial" \
                "$NAS_VIRTIO_NET_HOSTFWD_PORT" "$receipt" \
                --first-receipt "$PWD/$work/m232-first-host-cache.json" \
                --ready-timeout "${NAS_M232_HOST_READY_SECONDS:-2750}" \
                --cache-timeout "${NAS_M232_HOST_CACHE_SECONDS:-45}" \
                > "$host_log" 2>&1 &
        else
            python3 -u scripts/m232-host-cache-check.py "$phase" "$host_serial" \
                "$NAS_VIRTIO_NET_HOSTFWD_PORT" "$receipt" \
                --ready-timeout "${NAS_M232_HOST_READY_SECONDS:-2750}" \
                --cache-timeout "${NAS_M232_HOST_CACHE_SECONDS:-45}" \
                > "$host_log" 2>&1 &
        fi
        m232_host_pid=$!
    }
    m232_finish_host() {
        phase=$1
        test -n "$m232_host_pid" || {
            echo '[M232 HOST FAIL] external host watcher absent' >&2; exit 1;
        }
        join_seconds=0
        while [ ! -s "$PWD/$work/m232-$phase-host-cache.json" ] && \
              kill -0 "$m232_host_pid" 2>/dev/null && \
              [ "$join_seconds" -lt 8 ]; do
            sleep 1
            join_seconds=$((join_seconds+1))
        done
        if [ ! -s "$PWD/$work/m232-$phase-host-cache.json" ]; then
            echo "[M232 HOST FAIL] $phase QEMU lacks independent real host cache" >&2
            m232_stop_host
            tail -100 "$PWD/$work/m232-$phase-host-cache.log" >&2 || :
            exit 1
        fi
        if wait "$m232_host_pid"; then
            m232_host_pid=
        else
            m232_host_pid=
            echo "[M232 HOST FAIL] $phase true cache miss/SET/GET" >&2
            tail -100 "$PWD/$work/m232-$phase-host-cache.log" >&2 || :
            exit 1
        fi
    }
fi

if [ "$m233" = 1 ]; then
    m233_start_host() {
        phase=$1
        host_serial=$2
        receipt="$PWD/$work/m233-$phase-host-dns.json"
        host_log="$PWD/$work/m233-$phase-host-dns.log"
        test ! -e "$receipt" && test ! -e "$host_log" || {
            echo "[M233 HOST FAIL] stale $phase DNS evidence" >&2; exit 1;
        }
        python3 -u scripts/m233-host-dns-check.py "$phase" "$host_serial" \
            "$NAS_VIRTIO_NET_HOSTFWD_PORT" "$NAS_VIRTIO_NET_HOSTFWD_UDP_PORT" \
            "$receipt" --ready-timeout "${NAS_M233_HOST_READY_SECONDS:-2750}" \
            --request-timeout "${NAS_M233_HOST_REQUEST_SECONDS:-30}" \
            > "$host_log" 2>&1 &
        m233_host_pid=$!
    }
    m233_finish_host() {
        phase=$1
        test -n "$m233_host_pid" || {
            echo '[M233 HOST FAIL] no independent DNS watcher' >&2; exit 1;
        }
        if [ ! -s "$PWD/$work/m233-$phase-host-dns.json" ]; then
            echo "[M233 HOST FAIL] $phase DNS receipt missing" >&2
            m233_stop_host
            tail -100 "$PWD/$work/m233-$phase-host-dns.log" >&2 || :
            exit 1
        fi
        if wait "$m233_host_pid"; then
            m233_host_pid=
        else
            m233_host_pid=
            echo "[M233 HOST FAIL] $phase actual UDP+TCP DNS failed" >&2
            tail -100 "$PWD/$work/m233-$phase-host-dns.log" >&2 || :
            exit 1
        fi
    }
fi

if [ "$m199" = 1 ]; then
    # This is the SAME original /sbin/init manifest on BOTH boots; M198's
    # independent second QEMU previously used a CPIO BusyBox entry instead.
    export BUSYBOX_PARITY_MANIFEST=scripts/m199-alpine-boot-cases.tsv
fi
if [ "$m227" = 1 ]; then
    export BUSYBOX_PARITY_MANIFEST=scripts/m227-alpine-boot-cases.tsv
fi
if [ "$m228" = 1 ]; then
    export BUSYBOX_PARITY_MANIFEST=scripts/m228-alpine-boot-cases.tsv
fi
if [ "$m229" = 1 ]; then
    # Exact same original Alpine /sbin/init as M228, never a synthetic entry.
    export BUSYBOX_PARITY_MANIFEST=scripts/m228-alpine-boot-cases.tsv
fi
if [ "$m230" = 1 ]; then
    export BUSYBOX_PARITY_MANIFEST=scripts/m228-alpine-boot-cases.tsv
fi
if [ "$m231" = 1 ]; then
    export BUSYBOX_PARITY_MANIFEST=scripts/m228-alpine-boot-cases.tsv
fi
if [ "$m232" = 1 ] || [ "$m233" = 1 ]; then
    export BUSYBOX_PARITY_MANIFEST=scripts/m228-alpine-boot-cases.tsv
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
    if [ "$m232" = 1 ]; then
        test ! -e "$serial" || {
            echo '[M232 FAIL] stale original first UART before host cache' >&2; exit 1;
        }
        echo "[M232 FIRST ORIGINAL PID1 UART START] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        m232_start_host first "$serial"
        if python3 scripts/m232-alpine-uart.py first "$runner" "$serial" \
                "$PWD/$work/m232-first-runner.log" "$fifo"; then
            cp "$serial" "$PWD/$work/m232-first.serial.log"
            m232_finish_host first
            echo "[M232 FIRST ORIGINAL PID1 UART COMPLETE] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        else
            m232_first_rc=$?
            if [ -f "$serial" ]; then
                cp "$serial" "$PWD/$work/m232-first.serial.log"
            fi
            echo "[M232 FIRST UART FAIL] rc=$m232_first_rc" >&2
            tail -100 "$PWD/$work/m232-first-host-cache.log" >&2 || :
            exit "$m232_first_rc"
        fi
    elif [ "$m233" = 1 ]; then
        test ! -e "$serial" || {
            echo '[M233 FAIL] stale original first UART' >&2; exit 1;
        }
        echo "[M233 FIRST ORIGINAL PID1 UART START] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        m233_start_host first "$serial"
        if python3 scripts/m233-alpine-uart.py first "$runner" "$serial" \
                "$PWD/$work/m233-first-runner.log" "$fifo"; then
            cp "$serial" "$PWD/$work/m233-first.serial.log"
            m233_finish_host first
            echo "[M233 FIRST ORIGINAL PID1 UART COMPLETE] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        else
            m233_first_rc=$?
            if [ -f "$serial" ]; then cp "$serial" "$PWD/$work/m233-first.serial.log"; fi
            echo "[M233 FIRST UART FAIL] rc=$m233_first_rc; preserving host DNS" >&2
            tail -100 "$PWD/$work/m233-first-host-dns.log" >&2 || :
            exit "$m233_first_rc"
        fi
    elif [ "$m231" = 1 ]; then
        test ! -e "$serial" || {
            echo '[M231 FAIL] stale first UART before MQTT watcher' >&2; exit 1;
        }
        echo "[M231 FIRST ORIGINAL PID1 UART START] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        m231_start_host first "$serial"
        if python3 scripts/m231-alpine-uart.py first "$runner" "$serial" \
                "$PWD/$work/m231-first-runner.log" "$fifo"; then
            cp "$serial" "$PWD/$work/m231-first.serial.log"
            m231_finish_host first
            echo "[M231 FIRST ORIGINAL PID1 UART COMPLETE] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        else
            m231_first_rc=$?
            if [ -f "$serial" ]; then
                cp "$serial" "$PWD/$work/m231-first.serial.log"
            fi
            echo "[M231 FIRST UART FAIL] rc=$m231_first_rc; preserving host MQTT log" >&2
            tail -100 "$PWD/$work/m231-first-host-mqtt.log" >&2 || :
            exit "$m231_first_rc"
        fi
    elif [ "$m230" = 1 ]; then
        test ! -e "$serial" || {
            echo '[M230 FAIL] stale original first UART before SSH watcher' >&2; exit 1;
        }
        echo "[M230 FIRST ORIGINAL PID1 UART START] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        m230_start_host first "$serial"
        if python3 scripts/m230-alpine-uart.py first "$runner" "$serial" \
                "$PWD/$work/m230-first-runner.log" "$fifo"; then
            cp "$serial" "$PWD/$work/m230-first.serial.log"
            m230_finish_host first
            echo "[M230 FIRST ORIGINAL PID1 UART COMPLETE] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        else
            m230_first_rc=$?
            if [ -f "$serial" ]; then
                cp "$serial" "$PWD/$work/m230-first.serial.log"
            fi
            echo "[M230 FIRST UART FAIL] rc=$m230_first_rc; preserving independent host SSH log" >&2
            tail -100 "$PWD/$work/m230-first-host-ssh.log" >&2 || :
            exit "$m230_first_rc"
        fi
    elif [ "$m229" = 1 ]; then
        test ! -e "$serial" || {
            echo '[M229 FAIL] stale original first UART before host RESP watcher' >&2; exit 1;
        }
        echo "[M229 FIRST ORIGINAL PID1 UART START] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        m229_start_host first "$serial"
        if python3 scripts/m229-alpine-uart.py first "$runner" "$serial" \
                "$PWD/$work/m229-first-runner.log" "$fifo"; then
            cp "$serial" "$PWD/$work/m229-first.serial.log"
            m229_finish_host first
            echo "[M229 FIRST ORIGINAL PID1 UART COMPLETE] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        else
            m229_first_rc=$?
            if [ -f "$serial" ]; then
                cp "$serial" "$PWD/$work/m229-first.serial.log"
            fi
            echo "[M229 FIRST UART FAIL] rc=$m229_first_rc; preserving independent host log" >&2
            tail -100 "$PWD/$work/m229-first-host-resp.log" >&2 || :
            exit "$m229_first_rc"
        fi
    elif [ "$m228" = 1 ]; then
        # Physical native QEMU MUST remain alive while host issues the
        # independent nonce-bearing TCP GET into guest eth0/lighttpd.
        # The observer starts BEFORE M194 unlinks its own UART path; never
        # allow it to mistake an older run's FIRST marker for this PID1.
        test ! -e "$serial" || {
            echo '[M228 FAIL] stale original first UART before host watcher' >&2; exit 1;
        }
        echo "[M228 FIRST ORIGINAL PID1 UART START] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        m228_start_host first "$serial"
        if python3 scripts/m228-alpine-uart.py first "$runner" "$serial" \
                "$PWD/$work/m228-first-runner.log" "$fifo"; then
            cp "$serial" "$PWD/$work/m228-first.serial.log"
            m228_finish_host first
            echo "[M228 FIRST ORIGINAL PID1 UART COMPLETE] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        else
            m228_first_rc=$?
            if [ -f "$serial" ]; then
                cp "$serial" "$PWD/$work/m228-first.serial.log"
            fi
            echo "[M228 FIRST UART FAIL] rc=$m228_first_rc; preserving independent host log" >&2
            tail -100 "$PWD/$work/m228-first-host-inbound.log" >&2 || :
            exit "$m228_first_rc"
        fi
    elif [ "$m227" = 1 ]; then
        # Keep the REAL first native UART even if the original signed Alpine
        # workload fails before M227 can reach its second QEMU. The artifact
        # uploader runs on failure, so this path must exist immediately.
        echo "[M227 FIRST UART START] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        if python3 scripts/m227-alpine-uart.py first "$runner" "$serial" \
                "$PWD/$work/m227-first-runner.log" "$fifo"; then
            cp "$serial" "$PWD/$work/m227-first.serial.log"
            echo "[M227 FIRST UART COMPLETE] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        else
            m227_first_rc=$?
            if [ -f "$serial" ]; then
                cp "$serial" "$PWD/$work/m227-first.serial.log"
                echo "[M227 FIRST UART FAILURE SNAPSHOT] $(date -u +%Y-%m-%dT%H:%M:%SZ) rc=$m227_first_rc"
            else
                echo '[M227 FIRST UART FAILURE] original serial not created' >&2
            fi
            exit "$m227_first_rc"
        fi
    elif [ "$m226" = 1 ]; then
        python3 scripts/m226-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m226-first-runner.log" "$fifo"
    elif [ "$m225" = 1 ]; then
        python3 scripts/m225-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m225-first-runner.log" "$fifo"
    elif [ "$m224" = 1 ]; then
        python3 scripts/m224-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m224-first-runner.log" "$fifo"
    elif [ "$m223" = 1 ]; then
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
    elif [ "$m238" = 1 ]; then
        python3 scripts/m238-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m238-first-runner.log" "$fifo"
    elif [ "$m237" = 1 ]; then
        python3 scripts/m237-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m237-first-runner.log" "$fifo"
    elif [ "$m236" = 1 ]; then
        python3 scripts/m236-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m236-first-runner.log" "$fifo"
    elif [ "$m235" = 1 ]; then
        python3 scripts/m235-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m235-first-runner.log" "$fifo"
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
if [ "$m224" = 1 ] || [ "$m225" = 1 ] || [ "$m226" = 1 ]; then
    # A first-boot host/wire/physical proof can fail BEFORE the generic
    # evidence copy below. Preserve actual guest functional FAIL lines and
    # the unedited Nmap stderr on that failure path for one-shot diagnosis.
    cp "$serial" "$PWD/$work/m197-live.serial.log"
fi
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
if [ "$m223" = 1 ] || [ "$m235" = 1 ]; then
    m223_stop_host
    if [ "$m223" = 1 ]; then
        NAS_M191_DISK_MIB="$disk_mib" python3 scripts/m223-http-wire-check.py first "$port" \
            "$pcap" "$PWD/$work/m223-first-host-http.log" \
            "$PWD/$work/m223-host-first" "$disk" \
            "$PWD/$work/m223-first-wire.json"
    else
        NAS_M191_DISK_MIB="$disk_mib" python3 scripts/m235-http-proof.py first "$port" \
            "$pcap" "$PWD/$work/m223-first-host-http.log" \
            "$PWD/$work/m223-host-first" "$disk" \
            "$PWD/$work/m235-first-http-proof.json"
    fi
fi
if [ "$m224" = 1 ]; then
    m224_stop_host
    NAS_M191_DISK_MIB="$disk_mib" python3 scripts/m224-tcp-wire.py first "$port" \
        "$pcap" "$PWD/$work/m224-first-host-tcp.log" \
        "$PWD/$work/m224-host-first" "$disk" \
        "$PWD/$work/m224-first-wire.json"
fi
if [ "$m225" = 1 ]; then
    m225_stop_host
    NAS_M191_DISK_MIB="$disk_mib" python3 scripts/m225-host-proof.py first "$port" \
        "$pcap" "$PWD/$work/m225-first-host-ftp.log" \
        "$PWD/$work/m225-host-first" "$disk" \
        "$PWD/$work/m225-first-wire.json"
fi
if [ "$m226" = 1 ]; then
    m226_stop_host
    NAS_M191_DISK_MIB="$disk_mib" python3 scripts/m226-host-proof.py first "$port" \
        "$pcap" "$PWD/$work/m226-first-source.log" \
        "$PWD/$work/m226-first-rest-server.log" \
        "$PWD/$work/m226-host-first" "$PWD/$work/m226-rest-server-storage" \
        "$disk" "$PWD/$work/m226-first-wire.json"
    m226_host_check first
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
if [ "$m227" = 1 ]; then
    grep -Fq 'M227_FIRST_OPENRC_HTTP_PASS' "$serial" || {
        echo '[M227 FAIL] original lighttpd OpenRC/HTTP first-boot proof missing' >&2
        exit 1
    }
fi
if [ "$m228" = 1 ]; then
    grep -Fq 'M227_FIRST_OPENRC_HTTP_PASS' "$serial" || {
        echo '[M228 FAIL] original first OpenRC lighttpd proof missing' >&2
        exit 1
    }
    grep -Fq 'M228_FIRST_HOST_INGRESS_WINDOW_COMPLETE' "$serial" || {
        echo '[M228 FAIL] first real host HTTP opportunity absent' >&2
        exit 1
    }
    test -s "$PWD/$work/m228-first-host-inbound.json" || {
        echo '[M228 FAIL] independent first host TCP response missing' >&2
        exit 1
    }
fi
if [ "$m229" = 1 ]; then
    grep -Fq 'M229_FIRST_HOST_RESP_WINDOW_COMPLETE' "$serial" || {
        echo '[M229 FAIL] original guest did not complete the live Valkey window' >&2
        exit 1
    }
    test -s "$PWD/$work/m229-first-host-resp.json" || {
        echo '[M229 FAIL] no independent real-host first RESP receipt' >&2
        exit 1
    }
fi
if [ "$m230" = 1 ]; then
    grep -Fq 'M230_FIRST_HOST_SSH_WINDOW_COMPLETE' "$serial" || {
        echo '[M230 FAIL] original guest did not complete SSH first window' >&2
        exit 1
    }
    test -s "$PWD/$work/m230-first-host-ssh.json" || {
        echo '[M230 FAIL] no independent first host SSH command receipt' >&2
        exit 1
    }
fi
if [ "$m231" = 1 ]; then
    grep -Fq 'M231_FIRST_HOST_MQTT_WINDOW_COMPLETE' "$serial" || {
        echo '[M231 FAIL] original first broker host window did not complete' >&2
        exit 1
    }
    test -s "$PWD/$work/m231-first-host-mqtt.json" || {
        echo '[M231 FAIL] independent retained MQTT host receipt absent' >&2
        exit 1
    }
fi
if [ "$m232" = 1 ]; then
    grep -Fq 'M232_FIRST_HOST_CACHE_WINDOW_COMPLETE' "$serial" || {
        echo '[M232 FAIL] first original OpenRC host cache window incomplete' >&2
        exit 1
    }
    test -s "$PWD/$work/m232-first-host-cache.json" || {
        echo '[M232 FAIL] independent real-host cache transaction missing' >&2
        exit 1
    }
fi
if [ "$m233" = 1 ]; then
    grep -Fq 'M233_FIRST_HOST_DNS_WINDOW_COMPLETE' "$serial" || {
        echo '[M233 FAIL] original DNS daemon first host window incomplete' >&2
        exit 1
    }
    test -s "$PWD/$work/m233-first-host-dns.json" || {
        echo '[M233 FAIL] independent first host UDP+TCP DNS proof missing' >&2
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
if [ "$m235" = 1 ]; then
    grep -Fq 'M235_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M235 FAIL] first 12-ELF batch incomplete' >&2
        exit 1
    }
fi
if [ "$m236" = 1 ]; then
    grep -Fq 'M236_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M236 FAIL] first seven-ELF batch incomplete' >&2
        exit 1
    }
fi
if [ "$m237" = 1 ]; then
    grep -Fq 'M237_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M237 FAIL] first 15-ELF batch incomplete' >&2
        exit 1
    }
fi
if [ "$m238" = 1 ]; then
    grep -Fq 'M238_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M238 FAIL] first 15-ELF batch incomplete' >&2
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
if [ "$m224" = 1 ]; then
    grep -Fq 'M224_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M224 FAIL] first original TCP client ledger incomplete' >&2; exit 1;
    }
fi
if [ "$m225" = 1 ]; then
    grep -Fq 'M225_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M225 FAIL] first original FTP upload/download ledger incomplete' >&2; exit 1;
    }
fi
if [ "$m226" = 1 ]; then
    grep -Fq 'M226_FIRST_DISCOVERY_COMPLETE' "$serial" || {
        echo '[M226 FAIL] first native signed restic real REST repository incomplete' >&2; exit 1;
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

if [ "$m232" = 1 ]; then
    # Original signed OpenRC service must auto-start after a fully different
    # native QEMU reopens the SAME physical ext4; old cache key must be gone.
    test -s "$PWD/$work/m232-first.serial.log" && \
        test -s "$PWD/$work/m232-first-host-cache.json" || {
        echo '[M232 FAIL] original first independent cache proof missing' >&2
        exit 1
    }
    echo "[M232 REOPEN ORIGINAL PID1 UART START] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    unset NAS_QEMU_SERIAL_FIFO NAS_VIRTIO_NET_PCAP NAS_QEMU_QMP_SOCKET
    export NAS_QEMU_SERIAL_FIFO="$PWD/$work/m232-reopen-input.fifo"
    export NAS_VIRTIO_NET_PCAP="$PWD/$work/m232-reopen-wire.pcap"
    test ! -e "$NAS_VIRTIO_NET_PCAP" || {
        echo '[M232 FAIL] stale second independent real NIC PCAP' >&2; exit 1;
    }
    export BUSYBOX_PARITY_MANIFEST=scripts/m228-alpine-boot-cases.tsv
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M232_REOPEN_QEMU_TIMEOUT:-1100}
    m232_start_host reopen "$serial"
    if python3 scripts/m232-alpine-uart.py reopen "$runner" "$serial" \
            "$PWD/$work/m232-reopen-runner.log" "$NAS_QEMU_SERIAL_FIFO"; then
        cp "$serial" "$PWD/$work/m232-reopen.serial.log"
        m232_finish_host reopen
    else
        m232_reopen_rc=$?
        if [ -f "$serial" ]; then
            cp "$serial" "$PWD/$work/m232-reopen.serial.log"
        fi
        echo "[M232 REOPEN UART FAIL] rc=$m232_reopen_rc" >&2
        tail -100 "$PWD/$work/m232-reopen-host-cache.log" >&2 || :
        exit "$m232_reopen_rc"
    fi
    grep -Fq 'M232_REOPEN_HOST_CACHE_WINDOW_COMPLETE' "$serial" || {
        echo '[M232 FAIL] second original OpenRC host cache window incomplete' >&2
        exit 1
    }
    grep -Fq 'M197_REOPEN_WORKLOAD_PASS' "$serial" || {
        echo '[M232 FAIL] signed Bash/Lua/SQLite/Git durability absent' >&2
        exit 1
    }
    test -s "$PWD/$work/m232-reopen-host-cache.json" || {
        echo '[M232 FAIL] second true real-host memcached receipt absent' >&2
        exit 1
    }
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m232-memcached-disk-check.py "$port" "$root" "$disk" \
        "$PWD/$work/m232-first-host-cache.json" \
        "$PWD/$work/m232-reopen-host-cache.json" \
        "$pcap" "$PWD/$work/m232-reopen-wire.pcap" \
        "$PWD/$work/m232-memcached-final-oracle.json"
    sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
    echo "[M232 ORIGINAL ALPINE MEMCACHED HOST VOLATILE TWO BOOT PASS] $port real "\
         'host memcached miss/SET/GET then reboot miss/new SET/GET, ' \
         'original signed Alpine PID1/OpenRC, dual NIC PCAP, physical ext4/fsck; root/localhost test only'
    exit 0
fi

if [ "$m233" = 1 ]; then
    test -s "$PWD/$work/m233-first.serial.log" && \
        test -s "$PWD/$work/m233-first-host-dns.json" || {
        echo '[M233 FAIL] independent first QEMU DNS witness missing' >&2; exit 1;
    }
    echo "[M233 REOPEN ORIGINAL PID1 UART START] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    unset NAS_QEMU_SERIAL_FIFO NAS_VIRTIO_NET_PCAP NAS_QEMU_QMP_SOCKET
    export NAS_QEMU_SERIAL_FIFO="$PWD/$work/m233-reopen-input.fifo"
    export NAS_VIRTIO_NET_PCAP="$PWD/$work/m233-reopen-wire.pcap"
    test ! -e "$NAS_VIRTIO_NET_PCAP" || {
        echo '[M233 FAIL] stale second original NIC PCAP' >&2; exit 1;
    }
    export BUSYBOX_PARITY_MANIFEST=scripts/m228-alpine-boot-cases.tsv
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M233_REOPEN_QEMU_TIMEOUT:-1100}
    m233_start_host reopen "$serial"
    if python3 scripts/m233-alpine-uart.py reopen "$runner" "$serial" \
            "$PWD/$work/m233-reopen-runner.log" "$NAS_QEMU_SERIAL_FIFO"; then
        cp "$serial" "$PWD/$work/m233-reopen.serial.log"
        m233_finish_host reopen
    else
        m233_reopen_rc=$?
        if [ -f "$serial" ]; then cp "$serial" "$PWD/$work/m233-reopen.serial.log"; fi
        echo "[M233 REOPEN UART FAIL] rc=$m233_reopen_rc" >&2
        tail -100 "$PWD/$work/m233-reopen-host-dns.log" >&2 || :
        exit "$m233_reopen_rc"
    fi
    grep -Fq 'M233_REOPEN_HOST_DNS_WINDOW_COMPLETE' "$serial" || {
        echo '[M233 FAIL] original DNS OpenRC reboot host window missing' >&2; exit 1;
    }
    grep -Fq 'M197_REOPEN_WORKLOAD_PASS' "$serial" || {
        echo '[M233 FAIL] original signed APK persistence regression' >&2; exit 1;
    }
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m233-dnsmasq-disk-check.py "$port" "$root" "$disk" \
        "$PWD/$work/m233-first-host-dns.json" \
        "$PWD/$work/m233-reopen-host-dns.json" \
        "$pcap" "$PWD/$work/m233-reopen-wire.pcap" \
        "$PWD/$work/m233-dnsmasq-final-oracle.json"
    sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
    echo "[M233 ORIGINAL ALPINE DNSMASQ OPENRC HOST UDP+TCP TWO BOOT PASS] $port "\
         'two original PID1/OpenRC, independent host UDP and TCP DNS queries, ' \
         'two physical NIC PCAPs, signed APK/ELF and clean read-only ext4 fsck'
    exit 0
fi

if [ "$m231" = 1 ]; then
    # Different native QEMU, SAME physical ext4, original Alpine PID1 must
    # auto-start the broker and deliver the FIRST host's retained message.
    test -s "$PWD/$work/m231-first.serial.log" && \
        test -s "$PWD/$work/m231-first-host-mqtt.json" || {
        echo '[M231 FAIL] first independent MQTT evidence absent' >&2
        exit 1
    }
    echo "[M231 REOPEN ORIGINAL PID1 UART START] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    unset NAS_QEMU_SERIAL_FIFO NAS_VIRTIO_NET_PCAP NAS_QEMU_QMP_SOCKET
    export NAS_QEMU_SERIAL_FIFO="$PWD/$work/m231-reopen-input.fifo"
    export NAS_VIRTIO_NET_PCAP="$PWD/$work/m231-reopen-wire.pcap"
    test ! -e "$NAS_VIRTIO_NET_PCAP" || {
        echo '[M231 FAIL] stale second real NIC PCAP' >&2; exit 1;
    }
    export BUSYBOX_PARITY_MANIFEST=scripts/m228-alpine-boot-cases.tsv
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M231_REOPEN_QEMU_TIMEOUT:-1100}
    m231_start_host reopen "$serial"
    if python3 scripts/m231-alpine-uart.py reopen "$runner" "$serial" \
            "$PWD/$work/m231-reopen-runner.log" "$NAS_QEMU_SERIAL_FIFO"; then
        cp "$serial" "$PWD/$work/m231-reopen.serial.log"
        m231_finish_host reopen
    else
        m231_reopen_rc=$?
        if [ -f "$serial" ]; then
            cp "$serial" "$PWD/$work/m231-reopen.serial.log"
        fi
        echo "[M231 REOPEN UART FAIL] rc=$m231_reopen_rc; preserving host MQTT log" >&2
        tail -100 "$PWD/$work/m231-reopen-host-mqtt.log" >&2 || :
        exit "$m231_reopen_rc"
    fi
    grep -Fq 'M231_REOPEN_HOST_MQTT_WINDOW_COMPLETE' "$serial" || {
        echo '[M231 FAIL] second native broker MQTT window incomplete' >&2; exit 1;
    }
    grep -Fq 'M197_REOPEN_WORKLOAD_PASS' "$serial" || {
        echo '[M231 FAIL] original signed Bash/Lua/SQLite/Git durability absent' >&2; exit 1;
    }
    test -s "$PWD/$work/m231-reopen-host-mqtt.json" || {
        echo '[M231 FAIL] second host real retained MQTT receipt absent' >&2; exit 1;
    }
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m231-mosquitto-disk-check.py "$port" "$root" "$disk" \
        "$PWD/$work/m231-first-host-mqtt.json" \
        "$PWD/$work/m231-reopen-host-mqtt.json" \
        "$pcap" "$PWD/$work/m231-reopen-wire.pcap" \
        "$PWD/$work/m231-mosquitto-final-oracle.json"
    sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
    echo "[M231 ORIGINAL ALPINE MOSQUITTO HOST RETAINED MQTT TWO BOOT PASS] $port real "\
         'host MQTT QoS1 retained PUB and independent SUB before/after reboot, ' \
         'original signed Alpine PID1/OpenRC + persisted broker DB, ' \
         'two NIC PCAPs and read-only fsck; anonymous/root test only'
    exit 0
fi

if [ "$m230" = 1 ]; then
    # Original Alpine PID1 reopens the *same* physical ext4. Its own OpenRC
    # service, not this harness, must auto-start the original Dropbear ELF.
    test -s "$PWD/$work/m230-first.serial.log" && \
        test -s "$PWD/$work/m230-first-host-ssh.json" || {
        echo '[M230 FAIL] first native UART/SSH witness absent before reboot' >&2
        exit 1
    }
    echo "[M230 REOPEN ORIGINAL PID1 UART START] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    unset NAS_QEMU_SERIAL_FIFO NAS_VIRTIO_NET_PCAP NAS_QEMU_QMP_SOCKET
    export NAS_QEMU_SERIAL_FIFO="$PWD/$work/m230-reopen-input.fifo"
    export NAS_VIRTIO_NET_PCAP="$PWD/$work/m230-reopen-wire.pcap"
    test ! -e "$NAS_VIRTIO_NET_PCAP" || {
        echo '[M230 FAIL] stale independent second NIC PCAP' >&2; exit 1;
    }
    export BUSYBOX_PARITY_MANIFEST=scripts/m228-alpine-boot-cases.tsv
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M230_REOPEN_QEMU_TIMEOUT:-1300}
    m230_start_host reopen "$serial"
    if python3 scripts/m230-alpine-uart.py reopen "$runner" "$serial" \
            "$PWD/$work/m230-reopen-runner.log" "$NAS_QEMU_SERIAL_FIFO"; then
        cp "$serial" "$PWD/$work/m230-reopen.serial.log"
        m230_finish_host reopen
    else
        m230_reopen_rc=$?
        if [ -f "$serial" ]; then
            cp "$serial" "$PWD/$work/m230-reopen.serial.log"
        fi
        echo "[M230 REOPEN UART FAIL] rc=$m230_reopen_rc; preserving SSH log" >&2
        tail -100 "$PWD/$work/m230-reopen-host-ssh.log" >&2 || :
        exit "$m230_reopen_rc"
    fi
    echo "[M230 REOPEN ORIGINAL PID1 UART COMPLETE] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    grep -Fq 'M230_REOPEN_HOST_SSH_WINDOW_COMPLETE' "$serial" || {
        echo '[M230 FAIL] original Dropbear SSH reopen window incomplete' >&2
        exit 1
    }
    grep -Fq 'M197_REOPEN_WORKLOAD_PASS' "$serial" || {
        echo '[M230 FAIL] original signed applications did not persist' >&2
        exit 1
    }
    test -s "$PWD/$work/m230-reopen-host-ssh.json" || {
        echo '[M230 FAIL] no second true host SSH auth/command receipt' >&2
        exit 1
    }
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m230-dropbear-disk-check.py "$port" "$root" "$disk" \
        "$PWD/$work/m230-first-host-ssh.json" \
        "$PWD/$work/m230-reopen-host-ssh.json" \
        "$pcap" "$PWD/$work/m230-reopen-wire.pcap" \
        "$PWD/$work/m230-dropbear-final-oracle.json"
    sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
    echo "[M230 ORIGINAL ALPINE DROPBEAR HOST SSH TWO BOOT PASS] $port real "\
         'host-only Ed25519 client authentication and nonce remote command, ' \
         'two original signed Alpine PID1/OpenRC boots, persisted host key, ' \
         'two independent NIC PCAPs, original APK and ext4/fsck oracle'
    exit 0
fi

if [ "$m229" = 1 ]; then
    # Independent SECOND original signed Alpine PID1 on the same physical
    # ext4: OpenRC, not this harness, must restore both real services.
    test -s "$PWD/$work/m229-first.serial.log" && \
        test -s "$PWD/$work/m229-first-host-resp.json" || {
        echo '[M229 FAIL] first native UART/RESP witness missing before reboot' >&2
        exit 1
    }
    echo "[M229 REOPEN ORIGINAL PID1 UART START] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    unset NAS_QEMU_SERIAL_FIFO NAS_VIRTIO_NET_PCAP NAS_QEMU_QMP_SOCKET
    export NAS_QEMU_SERIAL_FIFO="$PWD/$work/m229-reopen-input.fifo"
    export NAS_VIRTIO_NET_PCAP="$PWD/$work/m229-reopen-wire.pcap"
    test ! -e "$NAS_VIRTIO_NET_PCAP" || {
        echo '[M229 FAIL] stale second independent NIC PCAP' >&2; exit 1;
    }
    export BUSYBOX_PARITY_MANIFEST=scripts/m228-alpine-boot-cases.tsv
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M229_REOPEN_QEMU_TIMEOUT:-1100}
    m229_start_host reopen "$serial"
    if python3 scripts/m229-alpine-uart.py reopen "$runner" "$serial" \
            "$PWD/$work/m229-reopen-runner.log" "$NAS_QEMU_SERIAL_FIFO"; then
        cp "$serial" "$PWD/$work/m229-reopen.serial.log"
        m229_finish_host reopen
    else
        m229_reopen_rc=$?
        if [ -f "$serial" ]; then
            cp "$serial" "$PWD/$work/m229-reopen.serial.log"
        fi
        echo "[M229 REOPEN UART FAIL] rc=$m229_reopen_rc; preserving host log" >&2
        tail -100 "$PWD/$work/m229-reopen-host-resp.log" >&2 || :
        exit "$m229_reopen_rc"
    fi
    echo "[M229 REOPEN ORIGINAL PID1 UART COMPLETE] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    grep -Fq 'M229_REOPEN_HOST_RESP_WINDOW_COMPLETE' "$serial" || {
        echo '[M229 FAIL] second native guest Valkey host window incomplete' >&2
        exit 1
    }
    grep -Fq 'M197_REOPEN_WORKLOAD_PASS' "$serial" || {
        echo '[M229 FAIL] signed original Bash/Lua/SQLite/Git persistence absent' >&2
        exit 1
    }
    test -s "$PWD/$work/m229-reopen-host-resp.json" || {
        echo '[M229 FAIL] no second independent authenticated host RESP GET' >&2
        exit 1
    }
    # One atomic final oracle owns BOTH real TCP/PCAP evidence and ext4:
    # neither a host response alone nor an OpenRC UART marker is sufficient.
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m229-valkey-disk-check.py "$port" "$root" "$disk" \
        "$PWD/$work/m229-first-host-resp.json" \
        "$PWD/$work/m229-reopen-host-resp.json" \
        "$pcap" "$PWD/$work/m229-reopen-wire.pcap" \
        "$PWD/$work/m229-valkey-final-oracle.json"
    sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
    echo "[M229 ORIGINAL ALPINE VALKEY HOST RESP TWO BOOT PASS] $port real "\
         'host AUTH/SET/GET/SAVE and second host AUTH/GET over one NIC, ' \
         'two original signed PID1/OpenRC boots, two independent PCAPs and ' \
         'host receipts, exact Valkey RDB on physical ext4 and strict fsck'
    exit 0
fi

if [ "$m228" = 1 ]; then
    # A genuinely independent SECOND original Alpine /sbin/init on the same
    # physical ext4 image. OpenRC must auto-start the original lighttpd;
    # no second manual service start or host-side fake server is permitted.
    test -s "$PWD/$work/m228-first.serial.log" || {
        echo '[M228 FAIL] first real native UART snapshot absent' >&2; exit 1;
    }
    test -s "$PWD/$work/m228-first-host-inbound.json" || {
        echo '[M228 FAIL] first host TCP response receipt absent' >&2; exit 1;
    }
    echo "[M228 REOPEN ORIGINAL PID1 UART START] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    unset NAS_QEMU_SERIAL_FIFO NAS_VIRTIO_NET_PCAP NAS_QEMU_QMP_SOCKET
    export NAS_QEMU_SERIAL_FIFO="$PWD/$work/m228-reopen-input.fifo"
    export NAS_VIRTIO_NET_PCAP="$PWD/$work/m228-reopen-wire.pcap"
    test ! -e "$NAS_VIRTIO_NET_PCAP" || {
        echo '[M228 FAIL] stale second independent NIC PCAP' >&2; exit 1;
    }
    export BUSYBOX_PARITY_MANIFEST=scripts/m228-alpine-boot-cases.tsv
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M228_REOPEN_QEMU_TIMEOUT:-1050}
    m228_start_host reopen "$serial"
    if python3 scripts/m228-alpine-uart.py reopen "$runner" "$serial" \
            "$PWD/$work/m228-reopen-runner.log" "$NAS_QEMU_SERIAL_FIFO"; then
        cp "$serial" "$PWD/$work/m228-reopen.serial.log"
        m228_finish_host reopen
    else
        m228_reopen_rc=$?
        if [ -f "$serial" ]; then
            cp "$serial" "$PWD/$work/m228-reopen.serial.log"
        fi
        echo "[M228 REOPEN UART FAIL] rc=$m228_reopen_rc; preserving independent host log" >&2
        tail -100 "$PWD/$work/m228-reopen-host-inbound.log" >&2 || :
        exit "$m228_reopen_rc"
    fi
    echo "[M228 REOPEN ORIGINAL PID1 UART COMPLETE] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    grep -Fq 'M227_SECOND_OPENRC_AUTO_HTTP_PASS' "$serial" || {
        echo '[M228 FAIL] second true PID1 OpenRC auto-restoration absent' >&2
        exit 1
    }
    grep -Fq 'M197_REOPEN_WORKLOAD_PASS' "$serial" || {
        echo '[M228 FAIL] original public signed software persistence absent' >&2
        exit 1
    }
    grep -Fq 'M228_REOPEN_HOST_INGRESS_WINDOW_COMPLETE' "$serial" || {
        echo '[M228 FAIL] second real host HTTP opportunity absent' >&2
        exit 1
    }
    test -s "$PWD/$work/m228-reopen-host-inbound.json" || {
        echo '[M228 FAIL] independent second host TCP response receipt absent' >&2
        exit 1
    }
    # M228's disposable signed-service disk is 512MiB; the immutable M191
    # signed baseline remains 256MiB.  Pass exact geometry through sudo,
    # which otherwise strips the environment and falsely rejects real ext4.
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m228-inbound-oracle.py "$port" "$root" "$disk" \
        "$PWD/$work/m228-first-host-inbound.json" \
        "$PWD/$work/m228-reopen-host-inbound.json" \
        "$pcap" "$PWD/$work/m228-reopen-wire.pcap" \
        "$PWD/$work/m228-inbound-oracle.json"
    sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
    echo "[M228 ORIGINAL ALPINE HOST INGRESS TWO BOOT PASS] $port real "\
         'host TCP GET via single VirtIO-Net hostfwd into original signed ' \
         'OpenRC/lighttpd, two native PID1, two independent host nonce receipts, ' \
         'two physical Ethernet PCAPs, and original signed ext4/fsck'
    exit 0
fi

if [ "$m227" = 1 ]; then
    # M199's true second original PID1, without M199's GUI/QMP. A separate
    # UART log and FIFO prevent first-boot bytes from faking auto-restart.
    test -s "$PWD/$work/m227-first.serial.log" || {
        echo '[M227 FAIL] first raw UART snapshot missing before reboot' >&2
        exit 1
    }
    echo "[M227 REOPEN UART START] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    unset NAS_QEMU_SERIAL_FIFO NAS_VIRTIO_NET_PCAP NAS_QEMU_QMP_SOCKET
    export NAS_QEMU_SERIAL_FIFO="$PWD/$work/m227-reopen-input.fifo"
    export BUSYBOX_PARITY_MANIFEST=scripts/m227-alpine-boot-cases.tsv
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M227_REOPEN_QEMU_TIMEOUT:-650}
    python3 scripts/m227-alpine-uart.py reopen "$runner" "$serial" \
        "$PWD/$work/m227-reopen-runner.log" "$NAS_QEMU_SERIAL_FIFO"
    echo "[M227 REOPEN UART COMPLETE] $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    grep -Fq 'M227_SECOND_OPENRC_AUTO_HTTP_PASS' "$serial" || {
        echo '[M227 FAIL] second genuine OpenRC auto-start HTTP proof absent' >&2
        exit 1
    }
    grep -Fq 'M197_REOPEN_WORKLOAD_PASS' "$serial" || {
        echo '[M227 FAIL] original Bash/Lua/SQLite/Git persistence missing' >&2
        exit 1
    }
    # As in the other large-cohort disk oracles, the verifier must know the
    # explicit 512MiB disposable geometry; sudo drops ordinary env vars.
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m227-alpine-disk-check.py "$port" "$root" "$disk"
    sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
    echo "[M227 ORIGINAL ALPINE OPENRC TWO BOOT PASS] $port signed public "\
         'Bash Lua SQLite Git wget aria2 lighttpd lighttpd-openrc, two real ' \
         'original PID1 boots, automatic guest-loopback HTTP service restart, ' \
         'SQLite/Git and independent physical ext4 fsck'
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
if [ "$m223" = 1 ] || [ "$m235" = 1 ]; then
    export NAS_VIRTIO_NET_PCAP="$PWD/$work/m223-reopen-wire.pcap"
    test ! -e "$NAS_VIRTIO_NET_PCAP" || {
        echo '[M223 HOST FAIL] stale independent second HTTP QEMU PCAP' >&2; exit 1;
    }
    m223_start_host reopen
fi
if [ "$m224" = 1 ]; then
    export NAS_VIRTIO_NET_PCAP="$PWD/$work/m224-reopen-wire.pcap"
    test ! -e "$NAS_VIRTIO_NET_PCAP" || {
        echo '[M224 HOST FAIL] stale independent second TCP QEMU PCAP' >&2; exit 1;
    }
    m224_start_host reopen
fi
if [ "$m225" = 1 ]; then
    export NAS_VIRTIO_NET_PCAP="$PWD/$work/m225-reopen-wire.pcap"
    test ! -e "$NAS_VIRTIO_NET_PCAP" || {
        echo '[M225 HOST FAIL] stale independent second FTP QEMU PCAP' >&2; exit 1;
    }
    m225_start_host reopen
fi
if [ "$m226" = 1 ]; then
    export NAS_VIRTIO_NET_PCAP="$PWD/$work/m226-reopen-wire.pcap"
    test ! -e "$NAS_VIRTIO_NET_PCAP" || {
        echo '[M226 HOST FAIL] stale independent second native REST PCAP' >&2; exit 1;
    }
    m226_start_host reopen
fi
if [ "$m198" = 1 ]; then
    export BUSYBOX_PARITY_MANIFEST=scripts/m198-alpine-reopen-cases.tsv
else
    if [ "$m226" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m226-alpine-reopen-cases.tsv
    elif [ "$m225" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m225-alpine-reopen-cases.tsv
    elif [ "$m224" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m224-alpine-reopen-cases.tsv
    elif [ "$m223" = 1 ]; then
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
    elif [ "$m238" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m238-alpine-reopen-cases.tsv
    elif [ "$m237" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m237-alpine-reopen-cases.tsv
    elif [ "$m236" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m236-alpine-reopen-cases.tsv
    elif [ "$m235" = 1 ]; then
        export BUSYBOX_PARITY_MANIFEST=scripts/m235-alpine-reopen-cases.tsv
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
if [ "$m235" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M235_REOPEN_SECONDS:-1200}
fi
if [ "$m236" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M236_REOPEN_SECONDS:-1200}
fi
if [ "$m237" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M237_REOPEN_SECONDS:-1200}
fi
if [ "$m238" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M238_REOPEN_SECONDS:-1200}
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
if [ "$m224" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M224_REOPEN_SECONDS:-900}
fi
if [ "$m225" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M225_REOPEN_SECONDS:-1100}
fi
if [ "$m226" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M226_REOPEN_SECONDS:-1500}
fi
if [ "$m224" = 1 ]; then
    # Both native QEMU runners reuse the m197-batch serial basename. The
    # first guest UART is already retained in $work; discard only the old
    # runner-generated copies so a failed second boot cannot upload a stale
    # first-boot log under a reopen name.
    rm -f "$PWD/$logs/m197-batch.serial.log" "$PWD/$logs/m197-batch.clean.log"
fi
if [ "$m225" = 1 ]; then
    # M225's second native QEMU uses the same runner basename; never upload
    # stale first-boot UART when the independent reopen crashes or times out.
    rm -f "$PWD/$logs/m197-batch.serial.log" "$PWD/$logs/m197-batch.clean.log"
fi
if [ "$m226" = 1 ]; then
    # No stale first-boot UART copies may masquerade as second native QEMU.
    rm -f "$PWD/$logs/m197-batch.serial.log" "$PWD/$logs/m197-batch.clean.log"
fi
m224_retain_reopen_serial() {
    [ "$m224" = 1 ] || return 0
    # The existing M224 workflow uploads build/acceptance/**/m197-batch*.log.
    # Preserve BOTH the untouched UART and the normalized strict-report input
    # on success or failure without changing the original validation path.
    evidence="$PWD/build/acceptance/m224-reopen-$port"
    mkdir -p "$evidence"
    [ ! -s "$PWD/$logs/m197-batch.serial.log" ] ||
        cp "$PWD/$logs/m197-batch.serial.log" "$evidence/m197-batch.serial.log"
    [ ! -s "$PWD/$logs/m197-batch.clean.log" ] ||
        cp "$PWD/$logs/m197-batch.clean.log" "$evidence/m197-batch.clean.log"
}
m225_retain_reopen_serial() {
    [ "$m225" = 1 ] || return 0
    evidence="$PWD/build/acceptance/m225-reopen-$port"
    mkdir -p "$evidence"
    [ ! -s "$PWD/$logs/m197-batch.serial.log" ] ||
        cp "$PWD/$logs/m197-batch.serial.log" "$evidence/m197-batch.serial.log"
    [ ! -s "$PWD/$logs/m197-batch.clean.log" ] ||
        cp "$PWD/$logs/m197-batch.clean.log" "$evidence/m197-batch.clean.log"
}
m226_retain_reopen_serial() {
    [ "$m226" = 1 ] || return 0
    evidence="$PWD/build/acceptance/m226-reopen-$port"
    mkdir -p "$evidence"
    [ ! -s "$PWD/$logs/m197-batch.serial.log" ] ||
        cp "$PWD/$logs/m197-batch.serial.log" "$evidence/m197-batch.serial.log"
    [ ! -s "$PWD/$logs/m197-batch.clean.log" ] ||
        cp "$PWD/$logs/m197-batch.clean.log" "$evidence/m197-batch.clean.log"
}
if ! sh "$runner" > "$PWD/$work/m197-reopen-runner.log" 2>&1; then
    m224_retain_reopen_serial
    m225_retain_reopen_serial
    m226_retain_reopen_serial
    echo "[M197 ALPINE REOPEN FAIL] $port second native QEMU" >&2
    tail -120 "$PWD/$work/m197-reopen-runner.log" >&2
    exit 1
fi
m224_retain_reopen_serial
m225_retain_reopen_serial
m226_retain_reopen_serial
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
if [ "$m223" = 1 ] || [ "$m235" = 1 ]; then
    m223_stop_host
    if [ "$m223" = 1 ]; then
        NAS_M191_DISK_MIB="$disk_mib" python3 scripts/m223-http-wire-check.py reopen "$port" \
            "$PWD/$work/m223-reopen-wire.pcap" \
            "$PWD/$work/m223-reopen-host-http.log" \
            "$PWD/$work/m223-host-reopen" "$disk" \
            "$PWD/$work/m223-reopen-wire.json"
    else
        NAS_M191_DISK_MIB="$disk_mib" python3 scripts/m235-http-proof.py reopen "$port" \
            "$PWD/$work/m223-reopen-wire.pcap" \
            "$PWD/$work/m223-reopen-host-http.log" \
            "$PWD/$work/m223-host-reopen" "$disk" \
            "$PWD/$work/m235-reopen-http-proof.json"
    fi
fi
if [ "$m224" = 1 ]; then
    m224_stop_host
    NAS_M191_DISK_MIB="$disk_mib" python3 scripts/m224-tcp-wire.py reopen "$port" \
        "$PWD/$work/m224-reopen-wire.pcap" \
        "$PWD/$work/m224-reopen-host-tcp.log" \
        "$PWD/$work/m224-host-reopen" "$disk" \
        "$PWD/$work/m224-reopen-wire.json"
fi
if [ "$m225" = 1 ]; then
    m225_stop_host
    NAS_M191_DISK_MIB="$disk_mib" python3 scripts/m225-host-proof.py reopen "$port" \
        "$PWD/$work/m225-reopen-wire.pcap" \
        "$PWD/$work/m225-reopen-host-ftp.log" \
        "$PWD/$work/m225-host-reopen" "$disk" \
        "$PWD/$work/m225-reopen-wire.json"
fi
if [ "$m226" = 1 ]; then
    m226_stop_host
    NAS_M191_DISK_MIB="$disk_mib" python3 scripts/m226-host-proof.py reopen "$port" \
        "$PWD/$work/m226-reopen-wire.pcap" \
        "$PWD/$work/m226-reopen-source.log" \
        "$PWD/$work/m226-reopen-rest-server.log" \
        "$PWD/$work/m226-host-reopen" "$PWD/$work/m226-rest-server-storage" \
        "$disk" "$PWD/$work/m226-reopen-wire.json"
    m226_host_check reopen
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
if [ "$m235" = 1 ]; then
    grep -Fq 'M235_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M235 FAIL] second native QEMU omitted the 12 original ELF reopens' >&2
        exit 1
    }
    python3 scripts/m235-compat-report.py \
        scripts/m235-candidate-elfs.tsv \
        "$PWD/$work/m197-live.serial.log" "$reopen" \
        "$PWD/$work/m235-compat-report.json" --require-all
fi
if [ "$m236" = 1 ]; then
    grep -Fq 'M236_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M236 FAIL] second native QEMU omitted seven original ELF reopens' >&2
        exit 1
    }
    python3 scripts/m236-compat-report.py \
        scripts/m236-candidate-elfs.tsv \
        "$PWD/$work/m197-live.serial.log" "$reopen" \
        "$PWD/$work/m236-compat-report.json" --require-all
fi
if [ "$m237" = 1 ]; then
    grep -Fq 'M237_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M237 FAIL] second native QEMU omitted fifteen original ELF reopens' >&2
        exit 1
    }
    python3 scripts/m237-compat-report.py \
        scripts/m237-candidate-elfs.tsv \
        "$PWD/$work/m197-live.serial.log" "$reopen" \
        "$PWD/$work/m237-compat-report.json" --require-all
fi
if [ "$m238" = 1 ]; then
    grep -Fq 'M238_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M238 FAIL] second native QEMU omitted fifteen original ELF reopens' >&2
        exit 1
    }
    python3 scripts/m238-compat-report.py \
        scripts/m238-candidate-elfs.tsv \
        "$PWD/$work/m197-live.serial.log" "$reopen" \
        "$PWD/$work/m238-compat-report.json" --require-all
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
if [ "$m224" = 1 ]; then
    grep -Fq 'M224_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M224 FAIL] second original TCP nmap/iperf3/ncat ledger incomplete' >&2
        exit 1
    }
    python3 scripts/m224-compat-report.py \
        scripts/m224-candidate-elfs.tsv \
        "$PWD/$work/m197-live.serial.log" "$reopen" \
        "$PWD/$work/m224-compat-report.json" --require-all
fi
if [ "$m225" = 1 ]; then
    grep -Fq 'M225_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M225 FAIL] second original signed lftp/rclone ledger incomplete' >&2
        exit 1
    }
    python3 scripts/m225-compat-report.py \
        scripts/m225-candidate-elfs.tsv \
        "$PWD/$work/m197-live.serial.log" "$reopen" \
        "$PWD/$work/m225-compat-report.json" --require-all
fi
if [ "$m226" = 1 ]; then
    grep -Fq 'M226_REOPEN_DISCOVERY_COMPLETE' "$reopen" || {
        echo '[M226 FAIL] second native original restic remote repository incomplete' >&2
        exit 1
    }
    python3 scripts/m226-compat-report.py \
        scripts/m226-candidate-elfs.tsv \
        "$PWD/$work/m197-live.serial.log" "$reopen" \
        "$PWD/$work/m226-compat-report.json" --require-all
fi
if [ "$m198" = 1 ]; then
    grep -Fxq 'M198_GUI_REOPEN_PASS' "$reopen" || {
        echo '[M198 REOPEN FAIL] native editor did not persist' >&2; exit 1;
    }
fi
if [ "$m226" = 1 ]; then
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m226-alpine-disk-check.py verify "$port" "$root" "$disk" \
        "$PWD/$work/m226-host-first" "$PWD/$work/m226-host-reopen"
elif [ "$m225" = 1 ]; then
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m225-alpine-disk-check.py verify "$port" "$root" "$disk" \
        "$PWD/$work/m225-host-first" "$PWD/$work/m225-host-reopen"
elif [ "$m224" = 1 ]; then
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m224-alpine-disk-check.py verify "$port" "$root" "$disk" \
        "$PWD/$work/m224-host-first" "$PWD/$work/m224-host-reopen"
elif [ "$m223" = 1 ]; then
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
elif [ "$m238" = 1 ]; then
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m238-alpine-disk-check.py "$port" "$root" "$disk"
elif [ "$m237" = 1 ]; then
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m237-alpine-disk-check.py "$port" "$root" "$disk"
elif [ "$m236" = 1 ]; then
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m236-alpine-disk-check.py "$port" "$root" "$disk"
elif [ "$m235" = 1 ]; then
    sudo env NAS_M191_DISK_MIB="$disk_mib" \
        python3 scripts/m235-alpine-disk-check.py "$port" "$root" "$disk"
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
    if [ "$m235" = 1 ]; then
        echo "[M235 ALPINE ORIGINAL 12/12 PASS] $port signed Alpine first+second "\
             'native QEMU, 12 unique original ELF functional/reopen PASS, ' \
             'host-random xh wire/physical ext4 proof and clean disk fsck'
    fi
    if [ "$m236" = 1 ]; then
        echo "[M236 ALPINE ORIGINAL 7/7 PASS] $port signed Alpine first+second "\
             'native QEMU, seven unique original APK ELF SHA proven against signed inventory, ' \
             'strict real filesystem+cryptography+archive+DFU functional witnesses and clean ext4'
    fi
    if [ "$m237" = 1 ]; then
        echo "[M237 ALPINE ORIGINAL 15/15 PASS] $port first+second native QEMU, "\
             'fifteen distinct original signed-APK ELF SHA, authentic record/JSON/signing/' \
             'geometry/projection functions, independent persisted physical ext4 and fsck'
    fi
    if [ "$m238" = 1 ]; then
        echo "[M238 ALPINE ORIGINAL 15/15 PASS] $port first+second native QEMU, "\
             'fifteen distinct original signed-APK ELF SHA, authentic WASM/SPIR-V/toolchain/' \
             'repository/workflow functions, independent persisted physical ext4 and fsck'
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
    if [ "$m224" = 1 ]; then
        echo "[M224 ORIGINAL ALPINE TCP TWO-BOOT PASS] $port three signed "\
             'nmap/iperf3/ncat ELF real connect/open-closed, throughput and ' \
             'phase-unique host random TCP duplex, two PCAPs, physical ext4, ' \
             'original signed SHA and M197 public HTTPS'
    fi
    if [ "$m225" = 1 ]; then
        echo "[M225 ORIGINAL ALPINE FTP TWO-BOOT PASS] $port two signed "\
             'lftp/rclone ELFs, real host-random FTP upload+download on each ' \
             'independent QEMU, exact PCAP and native physical ext4 bytes, ' \
             'official APK SHA and preserved M197 public HTTPS'
    fi
    if [ "$m226" = 1 ]; then
        echo "[M226 ORIGINAL ALPINE RESTIC REST TWO-BOOT PASS] $port historical M137 "\
             'signed restic ELF (0 NEW ELFs), real upstream rest-server init/' \
             'backup/restore/check encrypted repository, persistent across ' \
             'two native QEMUs, independent host restic check --read-data, ' \
             'random source wire/physical ext4 restores, official APK SHA ' \
             'and preserved M197 public HTTPS'
    fi
fi
if [ "$m198" = 1 ]; then
    echo "[M198 ALPINE GUI REBOOT PASS] $port original PID1, publicly signed "\
        'Bash/Lua/SQLite/Git and desktop dependencies, physical QMP/Vim, ' \
        'second real QEMU, exact host ext4 byte and fsck oracle'
fi
