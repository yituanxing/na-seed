#!/bin/sh
# Single host coordinator for real M218-M225 original signed Alpine native QEMU.
# Profile-specific host negatives and guest checks remain independent; only
# repeated orchestration and installation scripts are consolidated.
set -eu
[ "$#" -eq 3 ] || { echo 'usage: coordinator PHASE PROFILE PORT' >&2; exit 2; }
phase=$1 profile=$2 port=$3
case "$profile" in m218|m219|m220|m221|m222|m223|m224|m225) ;; *) exit 2 ;; esac
case "$port" in amd64) isa=amd64 ;; riscv64|arm64) isa=cross ;; *) exit 2 ;; esac
case "$phase" in contract|install|extra|verify|runtime) ;; *) exit 2 ;; esac
if [ "$phase" = contract ]; then
    python3 scripts/check-three-isa-core.py
    python3 scripts/check-architecture-boundaries.py
fi
case "$phase:$profile:$isa" in
    contract:m218:amd64)
        python3 scripts/test-m218-ifstat-host.py
        python3 scripts/test-m215-alpine-host.py
        python3 scripts/test-m213-alpine-host.py
        sh scripts/check-lwip-socket-abi.sh
        ;;
    contract:m218:cross)
        python3 scripts/test-m218-ifstat-host.py
        ;;
    contract:m219:amd64)
        python3 scripts/test-m219-utmpdump-host.py
        python3 scripts/test-m218-ifstat-host.py
        python3 scripts/test-m215-alpine-host.py
        sh scripts/check-lwip-socket-abi.sh
        ;;
    contract:m219:cross)
        python3 scripts/test-m219-utmpdump-host.py
        ;;
    contract:m220:amd64)
        python3 scripts/test-m220-nping-host.py
        python3 scripts/test-m218-ifstat-host.py
        python3 scripts/test-m219-utmpdump-host.py
        python3 scripts/test-m215-alpine-host.py
        sh scripts/check-lwip-socket-abi.sh
        ;;
    contract:m220:cross)
        python3 scripts/test-m220-nping-host.py
        ;;
    contract:m221:amd64)
        python3 scripts/test-m221-dns-host.py
        python3 scripts/test-m220-nping-host.py
        python3 scripts/test-m219-utmpdump-host.py
        python3 scripts/test-m218-ifstat-host.py
        python3 scripts/test-m215-alpine-host.py
        sh scripts/check-lwip-socket-abi.sh
        ;;
    contract:m221:cross)
        python3 scripts/test-m221-dns-host.py
        ;;
    contract:m222:amd64)
        python3 scripts/test-m222-ssh-host.py
        python3 scripts/test-m221-dns-host.py
        python3 scripts/test-m220-nping-host.py
        python3 scripts/test-m219-utmpdump-host.py
        python3 scripts/test-m218-ifstat-host.py
        sh scripts/check-lwip-socket-abi.sh
        ;;
    contract:m222:cross)
        python3 scripts/test-m222-ssh-host.py
        ;;
    contract:m223:amd64)
        python3 scripts/test-m223-http-host.py
        python3 scripts/test-m222-ssh-host.py
        sh scripts/check-lwip-socket-abi.sh
        ;;
    contract:m223:cross)
        python3 scripts/test-m223-http-host.py
        ;;
    contract:m224:amd64)
        python3 scripts/test-m224-tcp-host.py
        python3 scripts/test-m223-http-host.py
        python3 scripts/test-m222-ssh-host.py
        sh scripts/check-lwip-socket-abi.sh
        ;;
    contract:m224:cross)
        python3 scripts/test-m224-tcp-host.py
        python3 scripts/test-m223-http-host.py
        ;;
    contract:m225:amd64)
        python3 scripts/m225-test-host.py
        python3 scripts/check-m141-candidates.py --profile m225
        sh -n scripts/probe-m197-alpine-software-cohort.sh
        sh -n tests/m225-ftp-workload.sh
        sh -n tests/m225-software-workload.sh
        ;;
    contract:m225:cross)
        python3 scripts/check-m141-candidates.py --profile m225
        python3 scripts/m225-test-host.py
        ;;
    install:m218:amd64|install:m219:amd64|install:m220:amd64|install:m221:amd64)
        sh scripts/ci-install-signed-alpine-qemu-deps.sh amd64 --explicit-tools cmake:cmake g++:g++ clang:clang ld.lld:lld readelf:binutils cpio:cpio mke2fs:e2fsprogs debugfs:e2fsprogs e2fsck:e2fsprogs xorriso:xorriso mformat:mtools sgdisk:gdisk curl:curl python3:python3 openssl:openssl
        ;;
    install:m218:cross|install:m219:cross|install:m220:cross|install:m221:cross)
        sh scripts/ci-install-signed-alpine-qemu-deps.sh $PORT --arm64-efi-fallback --cross-cxx --explicit-tools cmake:cmake g++:g++ clang:clang ld.lld:lld readelf:binutils cpio:cpio mke2fs:e2fsprogs debugfs:e2fsprogs e2fsck:e2fsprogs xorriso:xorriso mformat:mtools sgdisk:gdisk curl:curl python3:python3 openssl:openssl
        command -v docker
        ;;
    install:m222:amd64)
        PATH=/usr/sbin:/sbin:$PATH
        export PATH
        sh scripts/ci-ubuntu-mirror.sh
        missing=''
        for spec in 'cmake:cmake' 'g++:g++' 'clang:clang' 'ld.lld:lld' \
                    'qemu-system-x86_64:qemu-system-x86' \
                    'readelf:binutils' 'cpio:cpio' 'mke2fs:e2fsprogs' \
                    'debugfs:e2fsprogs' 'e2fsck:e2fsprogs' \
                    'xorriso:xorriso' 'mformat:mtools' 'sgdisk:gdisk' \
                    'curl:curl' 'python3:python3' 'openssl:openssl' \
                    'sshd:openssh-server' 'ssh-keygen:openssh-client'; do
          bin=${spec%%:*}; pkg=${spec#*:}
          command -v "$bin" >/dev/null || missing="$missing $pkg"
        done
        test -f /usr/share/OVMF/OVMF_CODE_4M.fd || \
          test -f /usr/share/OVMF/OVMF_CODE.fd || missing="$missing ovmf"
        if [ -n "$missing" ]; then
          sudo apt-get -o Acquire::Retries=1 -o Acquire::http::Timeout=30 update -qq
          sudo DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=1 \
            -o Acquire::http::Timeout=30 install -y --no-install-recommends $missing
        fi
        sudo install -d -m 0755 /run/sshd
        command -v docker
        command -v sshd
        command -v ssh-keygen
        ;;
    install:m222:cross)
        PATH=/usr/sbin:/sbin:$PATH
        export PATH
        sh scripts/ci-install-signed-alpine-qemu-deps.sh $PORT --arm64-efi-fallback --cross-cxx --explicit-tools cmake:cmake g++:g++ clang:clang ld.lld:lld readelf:binutils cpio:cpio mke2fs:e2fsprogs debugfs:e2fsprogs e2fsck:e2fsprogs xorriso:xorriso mformat:mtools sgdisk:gdisk curl:curl python3:python3 openssl:openssl sshd:openssh-server ssh-keygen:openssh-client
        sudo install -d -m 0755 /run/sshd
        command -v docker
        command -v sshd
        ;;
    install:m223:amd64)
        PATH=/usr/sbin:/sbin:$PATH
        export PATH
        sh scripts/ci-install-signed-alpine-qemu-deps.sh amd64 --explicit-tools cmake:cmake g++:g++ clang:clang ld.lld:lld readelf:binutils cpio:cpio mke2fs:e2fsprogs debugfs:e2fsprogs e2fsck:e2fsprogs xorriso:xorriso mformat:mtools sgdisk:gdisk curl:curl python3:python3 openssl:openssl
        ;;
    install:m223:cross)
        PATH=/usr/sbin:/sbin:$PATH
        export PATH
        sh scripts/ci-install-signed-alpine-qemu-deps.sh $PORT --arm64-efi-fallback --cross-cxx --explicit-tools cmake:cmake g++:g++ clang:clang ld.lld:lld readelf:binutils cpio:cpio mke2fs:e2fsprogs debugfs:e2fsprogs e2fsck:e2fsprogs xorriso:xorriso mformat:mtools sgdisk:gdisk curl:curl python3:python3 openssl:openssl
        command -v docker
        ;;
    install:m224:amd64)
        PATH=/usr/sbin:/sbin:$PATH
        export PATH
        sh scripts/ci-install-signed-alpine-qemu-deps.sh amd64 --explicit-tools cmake:cmake g++:g++ clang:clang ld.lld:lld iperf3:iperf3 readelf:binutils cpio:cpio mke2fs:e2fsprogs debugfs:e2fsprogs e2fsck:e2fsprogs xorriso:xorriso mformat:mtools sgdisk:gdisk curl:curl python3:python3 openssl:openssl
        command -v iperf3
        ;;
    install:m224:cross)
        PATH=/usr/sbin:/sbin:$PATH
        export PATH
        sh scripts/ci-install-signed-alpine-qemu-deps.sh $PORT --arm64-efi-fallback --cross-cxx --explicit-tools cmake:cmake g++:g++ clang:clang ld.lld:lld iperf3:iperf3 readelf:binutils cpio:cpio mke2fs:e2fsprogs debugfs:e2fsprogs e2fsck:e2fsprogs xorriso:xorriso mformat:mtools sgdisk:gdisk curl:curl python3:python3 openssl:openssl
        command -v docker
        command -v iperf3
        ;;
    install:m225:amd64)
        PATH=/usr/sbin:/sbin:$PATH
        export PATH
        sh scripts/ci-install-signed-alpine-qemu-deps.sh amd64 --explicit-tools cmake:cmake g++:g++ clang:clang ld.lld:lld lftp:lftp rclone:rclone readelf:binutils cpio:cpio mke2fs:e2fsprogs debugfs:e2fsprogs e2fsck:e2fsprogs xorriso:xorriso mformat:mtools sgdisk:gdisk curl:curl python3:python3 openssl:openssl
        command -v lftp
        command -v rclone
        NAS_M225_REQUIRE_HOST_LFTP=1 python3 scripts/m225-test-host.py
        ;;
    install:m225:cross)
        PATH=/usr/sbin:/sbin:$PATH
        export PATH
        sh scripts/ci-install-signed-alpine-qemu-deps.sh $PORT --arm64-efi-fallback --cross-cxx --explicit-tools cmake:cmake g++:g++ clang:clang ld.lld:lld rclone:rclone readelf:binutils cpio:cpio mke2fs:e2fsprogs debugfs:e2fsprogs e2fsck:e2fsprogs xorriso:xorriso mformat:mtools sgdisk:gdisk curl:curl python3:python3 openssl:openssl
        command -v docker
        ;;
    extra:m223:amd64)
        sh scripts/check-lwext4-symlink-fsck.sh
        ;;
    extra:m224:amd64)
        sh scripts/check-lwip-foundation.sh
        ;;
    verify:m218:amd64)
        sh scripts/inventory-m141-signed-apks.sh amd64 m218
        tsv=build/external/m218-candidate-inventory/amd64/candidates.tsv
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF" || length($4) != 64 || $4 ~ /[^0-9a-f]/) bad++}
          END {if (n != 1 || bad) exit 1}' "$tsv"
        echo '[M218 SIGNED ELF PASS] original Alpine ifstat, no guest success claimed'
        ;;
    verify:m218:cross)
        sh scripts/inventory-m141-signed-apks.sh "$PORT" m218
        tsv="build/external/m218-candidate-inventory/$PORT/candidates.tsv"
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF") bad++}
          END {if (n != 1 || bad) exit 1}' "$tsv"
        ;;
    verify:m219:amd64)
        sh scripts/inventory-m141-signed-apks.sh amd64 m219
        tsv=build/external/m219-candidate-inventory/amd64/candidates.tsv
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF" || length($4) != 64 || $4 ~ /[^0-9a-f]/) bad++}
          END {if (n != 1 || bad) exit 1}' "$tsv"
        echo '[M219 SIGNED ELF PASS] host preflight only, no guest acceptance'
        ;;
    verify:m219:cross)
        sh scripts/inventory-m141-signed-apks.sh "$PORT" m219
        tsv="build/external/m219-candidate-inventory/$PORT/candidates.tsv"
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF") bad++}
          END {if (n != 1 || bad) exit 1}' "$tsv"
        ;;
    verify:m220:amd64)
        sh scripts/inventory-m141-signed-apks.sh amd64 m220
        tsv=build/external/m220-candidate-inventory/amd64/candidates.tsv
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF" || length($4) != 64 || $4 ~ /[^0-9a-f]/) bad++}
          END {if (n != 1 || bad) exit 1}' "$tsv"
        echo '[M220 SIGNED HOST ELF PASS] no guest UDP success claimed'
        ;;
    verify:m220:cross)
        sh scripts/inventory-m141-signed-apks.sh "$PORT" m220
        tsv="build/external/m220-candidate-inventory/$PORT/candidates.tsv"
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF" || length($4) != 64 || $4 ~ /[^0-9a-f]/) bad++}
          END {if (n != 1 || bad) exit 1}' "$tsv"
        ;;
    verify:m221:amd64)
        sh scripts/inventory-m141-signed-apks.sh amd64 m221
        tsv=build/external/m221-candidate-inventory/amd64/candidates.tsv
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF" || length($4) != 64 || $4 ~ /[^0-9a-f]/) bad++}
          END {if (n != 2 || bad) exit 1}' "$tsv"
        ;;
    verify:m221:cross)
        sh scripts/inventory-m141-signed-apks.sh "$PORT" m221
        tsv="build/external/m221-candidate-inventory/$PORT/candidates.tsv"
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF" || length($4) != 64 || $4 ~ /[^0-9a-f]/) bad++}
          END {if (n != 2 || bad) exit 1}' "$tsv"
        ;;
    verify:m222:amd64)
        sh scripts/inventory-m141-signed-apks.sh amd64 m222
        tsv=build/external/m222-candidate-inventory/amd64/candidates.tsv
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF" || length($4) != 64 || $4 ~ /[^0-9a-f]/) bad++}
          END {if (n != 3 || bad) exit 1}' "$tsv"
        ;;
    verify:m222:cross)
        sh scripts/inventory-m141-signed-apks.sh "$PORT" m222
        tsv="build/external/m222-candidate-inventory/$PORT/candidates.tsv"
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF" || length($4) != 64 || $4 ~ /[^0-9a-f]/) bad++}
          END {if (n != 3 || bad) exit 1}' "$tsv"
        ;;
    verify:m223:amd64)
        sh scripts/inventory-m141-signed-apks.sh amd64 m223
        tsv=build/external/m223-candidate-inventory/amd64/candidates.tsv
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF" || length($4) != 64 || $4 ~ /[^0-9a-f]/) bad++}
          END {if (n != 3 || bad) exit 1}' "$tsv"
        ;;
    verify:m223:cross)
        sh scripts/inventory-m141-signed-apks.sh "$PORT" m223
        tsv="build/external/m223-candidate-inventory/$PORT/candidates.tsv"
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF" || length($4) != 64 || $4 ~ /[^0-9a-f]/) bad++}
          END {if (n != 3 || bad) exit 1}' "$tsv"
        ;;
    verify:m224:amd64)
        sh scripts/inventory-m141-signed-apks.sh amd64 m224
        tsv=build/external/m224-candidate-inventory/amd64/candidates.tsv
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF" || length($4) != 64 || $4 ~ /[^0-9a-f]/) bad++}
          END {if (n != 3 || bad) exit 1}' "$tsv"
              # Capture real guest serial on physical Nmap rejection before uploading
              # fail-closed host/PCAP/ext4 evidence; no acceptance check is disabled.
        ;;
    verify:m224:cross)
        sh scripts/inventory-m141-signed-apks.sh "$PORT" m224
        tsv="build/external/m224-candidate-inventory/$PORT/candidates.tsv"
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF" || length($4) != 64 || $4 ~ /[^0-9a-f]/) bad++}
          END {if (n != 3 || bad) exit 1}' "$tsv"
        ;;
    verify:m225:amd64)
        sh scripts/inventory-m141-signed-apks.sh amd64 m225
        tsv=build/external/m225-candidate-inventory/amd64/candidates.tsv
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF" || length($4) != 64 || $4 ~ /[^0-9a-f]/) bad++}
          END {if (n != 2 || bad) exit 1}' "$tsv"
        ;;
    verify:m225:cross)
        sh scripts/inventory-m141-signed-apks.sh "$PORT" m225
        tsv="build/external/m225-candidate-inventory/$PORT/candidates.tsv"
        awk -F '\t' 'NR > 1 {n++; if ($3 != "NATIVE_ELF" || length($4) != 64 || $4 ~ /[^0-9a-f]/) bad++}
          END {if (n != 2 || bad) exit 1}' "$tsv"
        ;;
    contract:*|install:*|extra:*|verify:*) : ;; # No extra gate for this profile
    runtime:m218:amd64)
        export NAS_M218_PUBLIC_QEMU_TIMEOUT=3000
        export NAS_M218_PUBLIC_SECONDS=2900
        export NAS_M218_REOPEN_SECONDS=900
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    runtime:m218:cross)
        export NAS_M218_PUBLIC_QEMU_TIMEOUT=4500
        export NAS_M218_PUBLIC_SECONDS=4400
        export NAS_M218_REOPEN_SECONDS=900
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    runtime:m219:amd64)
        export NAS_M219_PUBLIC_QEMU_TIMEOUT=3000
        export NAS_M219_PUBLIC_SECONDS=2900
        export NAS_M219_REOPEN_SECONDS=900
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    runtime:m219:cross)
        export NAS_M219_PUBLIC_QEMU_TIMEOUT=4500
        export NAS_M219_PUBLIC_SECONDS=4400
        export NAS_M219_REOPEN_SECONDS=900
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    runtime:m220:amd64)
        export NAS_M220_PUBLIC_QEMU_TIMEOUT=3000
        export NAS_M220_PUBLIC_SECONDS=2900
        export NAS_M220_REOPEN_SECONDS=900
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    runtime:m220:cross)
        export NAS_M220_PUBLIC_QEMU_TIMEOUT=4500
        export NAS_M220_PUBLIC_SECONDS=4400
        export NAS_M220_REOPEN_SECONDS=900
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    runtime:m221:amd64)
        export NAS_M221_PUBLIC_QEMU_TIMEOUT=3000
        export NAS_M221_PUBLIC_SECONDS=2900
        export NAS_M221_REOPEN_SECONDS=900
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    runtime:m221:cross)
        export NAS_M221_PUBLIC_QEMU_TIMEOUT=4500
        export NAS_M221_PUBLIC_SECONDS=4400
        export NAS_M221_REOPEN_SECONDS=900
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    runtime:m222:amd64)
        export NAS_M222_PUBLIC_QEMU_TIMEOUT=3150
        export NAS_M222_PUBLIC_SECONDS=3000
        export NAS_M222_REOPEN_SECONDS=1100
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    runtime:m222:cross)
        export NAS_M222_PUBLIC_QEMU_TIMEOUT=4800
        export NAS_M222_PUBLIC_SECONDS=4700
        export NAS_M222_REOPEN_SECONDS=1500
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    runtime:m223:amd64)
        export NAS_M223_PUBLIC_QEMU_TIMEOUT=3300
        export NAS_M223_PUBLIC_SECONDS=3150
        export NAS_M223_REOPEN_SECONDS=1100
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    runtime:m223:cross)
        export NAS_M223_PUBLIC_QEMU_TIMEOUT=4850
        export NAS_M223_PUBLIC_SECONDS=4700
        export NAS_M223_REOPEN_SECONDS=1600
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    runtime:m224:amd64)
        export NAS_M224_PUBLIC_QEMU_TIMEOUT=3400
        export NAS_M224_PUBLIC_SECONDS=3250
        export NAS_M224_REOPEN_SECONDS=1150
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    runtime:m224:cross)
        export NAS_M224_PUBLIC_QEMU_TIMEOUT=5000
        export NAS_M224_PUBLIC_SECONDS=4850
        export NAS_M224_REOPEN_SECONDS=1700
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    runtime:m225:amd64)
        export NAS_M225_PUBLIC_QEMU_TIMEOUT=3500
        export NAS_M225_PUBLIC_SECONDS=3300
        export NAS_M225_REOPEN_SECONDS=1250
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    runtime:m225:cross)
        export NAS_M225_PUBLIC_QEMU_TIMEOUT=5600
        export NAS_M225_PUBLIC_SECONDS=5300
        export NAS_M225_REOPEN_SECONDS=2100
        sh scripts/probe-alpine-profile.sh "$profile" "$port"
        ;;
    *) echo "unsupported native phase $phase:$profile:$isa" >&2; exit 2 ;;
esac
