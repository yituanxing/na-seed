#!/bin/sh
# M141 native-system software cases. Invoke one named case per guest process.
# Only original Alpine ELFs are the target; BusyBox is fixture/oracle plumbing.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
LC_ALL=C
LANG=C
export PATH LC_ALL LANG
busy() { /bin/busybox "$@"; }

[ "$#" -eq 1 ] || exit 2
case_name=$1
work="/tmp/m141-system-$case_name"
busy mkdir -p "$work"
HOME="$work"
export HOME
bgpid=
ipc_id=
ipc_kind=
ssh_agent_live=

cleanup() {
    result=$?
    if [ "$result" -ne 0 ]; then
        busy printf '[M141 SYSTEM DIAG] %s exit=%s\n' "$case_name" "$result"
        case "$case_name" in
          ipc-shared|ipc-semaphore|exch-files|iperf3-loopback|ncat-loopback|ncat-wire|ncat-udp-wire|dig-dns-wire|host-dns-wire|nslookup-dns-wire|mdig-dns-wire)
            # These fixtures own their files; never dump arbitrary user
            # paths. Preserve only small, bounded original-program output.
            for diagnostic in details removed error out a b server.err server client client.json; do
                file="$work/$diagnostic"
                if [ -s "$file" ]; then
                    busy printf '[M141 SYSTEM DIAG] %s\n' "$diagnostic"
                    busy head -n 12 "$file"
                fi
            done ;;
        esac
    fi
    if [ -n "$bgpid" ]; then
        busy kill "$bgpid" >/dev/null 2>&1 || :
        wait "$bgpid" >/dev/null 2>&1 || :
    fi
    if [ -n "$ipc_id" ]; then
        case "$ipc_kind" in
            m) /usr/bin/ipcrm -m "$ipc_id" >/dev/null 2>&1 || : ;;
            s) /usr/bin/ipcrm -s "$ipc_id" >/dev/null 2>&1 || : ;;
            q) /usr/bin/ipcrm -q "$ipc_id" >/dev/null 2>&1 || : ;;
        esac
    fi
    if [ -n "$ssh_agent_live" ]; then
        /usr/bin/ssh-agent -k >/dev/null 2>&1 || :
    fi
}
trap cleanup EXIT

original_elf() {
    candidate=$1
    [ -x "$candidate" ] || return 1
    resolved=$(busy readlink -f "$candidate") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
    printf '%s\n' "$candidate"
}

get_ipc_id() {
    last=${1##* }
    case "$last" in ''|*[!0-9]*) return 1 ;; esac
    printf '%s\n' "$last"
}

start_agent() {
    original_elf /usr/bin/ssh-agent >/dev/null
    # Explicit local socket: no network, host SSH daemon or credentials.
    agent_env=$(/usr/bin/ssh-agent -a "$work/agent.sock" -s)
    case "$agent_env" in *SSH_AUTH_SOCK*SSH_AGENT_PID*) ;; *) return 1 ;; esac
    eval "$agent_env" >/dev/null
    [ -S "$SSH_AUTH_SOCK" ] || return 1
    ssh_agent_live=1
}

case "$case_name" in
  bfs-format)
    exe=$(original_elf /sbin/mkfs.bfs)
    busy dd if=/dev/zero of="$work/bfs.img" bs=1024 count=2048 2>/dev/null
    "$exe" -N 64 -V M141 "$work/bfs.img" > "$work/log" 2>&1
    magic=$(busy od -An -tx1 -N4 "$work/bfs.img" | busy tr -d ' \n')
    [ "$magic" = cefaad1b ]
    busy grep -Fq 'M141' "$work/bfs.img" ;;
  fdisk-image)
    exe=$(original_elf /sbin/fdisk)
    busy dd if=/dev/zero of="$work/disk.img" bs=1024 count=2048 2>/dev/null
    "$exe" -l "$work/disk.img" > "$work/partitions" 2> "$work/error"
    busy grep -Fq "Disk $work/disk.img:" "$work/partitions" ;;
  ipc-shared)
    original_elf /usr/bin/ipcmk >/dev/null
    original_elf /usr/bin/ipcs >/dev/null
    original_elf /usr/bin/ipcrm >/dev/null
    output=$(/usr/bin/ipcmk -M 4096)
    ipc_id=$(get_ipc_id "$output")
    ipc_kind=m
    /usr/bin/ipcs -m -i "$ipc_id" > "$work/details"
    busy grep -Eq 'bytes[[:space:]]*=[[:space:]]*4096' "$work/details"
    /usr/bin/ipcrm -m "$ipc_id"
    ipc_id=
    # util-linux ipcs prints "id N not found" but exits 0 for a missing
    # specifically selected ID. Test the resource's actual absence, not
    # a nonexistent nonzero-exit contract of the original Alpine tool.
    /usr/bin/ipcs -m -i "$(get_ipc_id "$output")" > "$work/removed" 2>&1 || :
    busy grep -Fq "id $(get_ipc_id "$output") not found" "$work/removed"
    ! busy grep -Fq 'Shared memory Segment' "$work/removed" ;;
  ipc-semaphore)
    original_elf /usr/bin/ipcmk >/dev/null
    original_elf /usr/bin/ipcs >/dev/null
    output=$(/usr/bin/ipcmk -S 2)
    ipc_id=$(get_ipc_id "$output")
    ipc_kind=s
    /usr/bin/ipcs -s -i "$ipc_id" > "$work/details"
    busy grep -Eq 'nsems[[:space:]]*=[[:space:]]*2' "$work/details" ;;
  ipc-queue)
    original_elf /usr/bin/ipcmk >/dev/null
    original_elf /usr/bin/ipcs >/dev/null
    output=$(/usr/bin/ipcmk -Q)
    ipc_id=$(get_ipc_id "$output")
    ipc_kind=q
    /usr/bin/ipcs -q -i "$ipc_id" > "$work/details"
    busy grep -Fq "msqid=$ipc_id" "$work/details" ;;
  ipc-list)
    original_elf /usr/bin/ipcmk >/dev/null
    original_elf /usr/bin/lsipc >/dev/null
    output=$(/usr/bin/ipcmk -M 8192)
    ipc_id=$(get_ipc_id "$output")
    ipc_kind=m
    /usr/bin/lsipc -m -J > "$work/ipc.json"
    busy grep -Fq 'sharedmemory' "$work/ipc.json"
    busy grep -Fq "$ipc_id" "$work/ipc.json" ;;
  ionice-child)
    exe=$(original_elf /bin/ionice)
    # Class IDLE must survive exec and be queryable from the child itself;
    # a syscall stub returning success without saving priority fails here.
    "$exe" -c 3 /bin/busybox sh -c \
      'printf "M141_IONICE_CHILD\n"; /bin/ionice -p "$$"' > "$work/out"
    busy grep -Fxq M141_IONICE_CHILD "$work/out"
    busy grep -Fxq idle "$work/out" ;;
  pidwait-child)
    exe=$(original_elf /bin/pidwait)
    /bin/busybox sleep 5 &
    bgpid=$!
    busy printf '%s\n' "$bgpid" > "$work/child.pid"
    "$exe" -F "$work/child.pid" > "$work/out" 2> "$work/error"
    wait "$bgpid"
    bgpid= ;;
  exch-files)
    exe=$(original_elf /usr/bin/exch)
    busy printf 'ALPHA\n' > "$work/a"
    busy printf 'BETA\n' > "$work/b"
    "$exe" "$work/a" "$work/b" > "$work/out" 2> "$work/error"
    busy grep -Fxq BETA "$work/a"
    busy grep -Fxq ALPHA "$work/b" ;;
  lsmem-report)
    exe=$(original_elf /usr/bin/lsmem)
    "$exe" -J > "$work/memory.json"
    busy grep -Fq 'memory' "$work/memory.json"
    busy grep -Fq 'range' "$work/memory.json" ;;
  ssh-agent-socket)
    start_agent
    [ -n "$SSH_AGENT_PID" ]
    busy kill -0 "$SSH_AGENT_PID" ;;
  ssh-add-empty)
    original_elf /usr/bin/ssh-add >/dev/null
    start_agent
    # No external credentials: an empty genuine agent is the expected result.
    if /usr/bin/ssh-add -l > "$work/identities" 2>&1; then
        exit 1
    fi
    busy grep -Fq 'The agent has no identities.' "$work/identities" ;;
  tcpdump-pcap)
    exe=$(original_elf /usr/bin/tcpdump)
    # Little-endian pcap: one Ethernet frame with a private ethertype 0x88b5.
    busy printf '\324\303\262\241\002\000\004\000\000\000\000\000\000\000\000\000\377\377\000\000\001\000\000\000' > "$work/one.pcap"
    busy printf '\001\000\000\000\000\000\000\000\016\000\000\000\016\000\000\000' >> "$work/one.pcap"
    busy printf '\377\377\377\377\377\377\000\001\002\003\004\005\210\265' >> "$work/one.pcap"
    "$exe" -n -r "$work/one.pcap" -c 1 > "$work/decoded" 2> "$work/error"
    busy grep -Fq '0x88b5' "$work/decoded" ;;
  ncat-loopback)
    exe=$(original_elf /usr/bin/ncat)
    (busy printf 'M141_TCP_REPLY\n' | "$exe" -4 --listen 127.0.0.1 15411) > "$work/server" 2> "$work/server.err" &
    bgpid=$!
    busy sleep 1
    busy printf 'M141_TCP_REQUEST\n' | busy timeout 10 "$exe" -4 --recv-only 127.0.0.1 15411 > "$work/client"
    busy grep -Fxq M141_TCP_REPLY "$work/client" ;;
  ncat-wire)
    exe=$(original_elf /usr/bin/ncat)
    # 10.0.2.2 is QEMU user-mode networking's host gateway, not lo.
    # The host HTTP service supplies README.md; it is absent from guest CPIO.
    # DHCP/ARP address-conflict checks are asynchronous. Probe real network
    # readiness for a bounded interval, never assume sleep(4) is a lease.
    wire_ready=false
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
      if busy printf 'GET /README.md HTTP/1.0\r\nHost: 10.0.2.2\r\n\r\n' |
         busy timeout 8 "$exe" -4 10.0.2.2 15413 > "$work/client" 2> "$work/error"; then
        if busy grep -Fq 'HTTP/1.0 200 OK' "$work/client" &&
           busy grep -Fq 'An operating system grown by real software.' "$work/client"; then
          wire_ready=true
          break
        fi
      fi
      busy sleep 1
    done
    [ "$wire_ready" = true ] ;;
  ncat-udp-wire)
    exe=$(original_elf /usr/bin/ncat)
    # Ncat must emit a real UDP datagram through VirtIO-Net, then receive
    # the host's byte-exact response. Keep the pipeline open briefly so the
    # client has an RX window before stdin EOF; neither packet is in CPIO.
    udp_ready=false
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
      (busy printf 'M141_UDP_REQUEST\n'; busy sleep 2) |
        busy timeout 8 "$exe" -4 -u 10.0.2.2 15414 > "$work/client" 2> "$work/error" || :
      if busy grep -Fxq M141_UDP_REPLY "$work/client"; then
        udp_ready=true
        break
      fi
      busy sleep 1
    done
    [ "$udp_ready" = true ] ;;
  dig-dns-wire)
    exe=$(original_elf /usr/bin/dig)
    # Original Alpine BIND dig, not a guest-provided DNS parser: send an
    # actual A question on VirtIO-Net to a deterministic host-only server.
    # The host validates the DNS packet independently before replying.
    dns_ready=false
    for attempt in 1 2 3 4 5 6; do
      busy timeout 9 "$exe" -4 @10.0.2.2 -p 15415 naseed.test. A \
        +tries=1 +time=3 +noedns > "$work/client" 2> "$work/error" || :
      if busy grep -Fq '192.0.2.42' "$work/client" &&
         busy grep -Fq 'NOERROR' "$work/client"; then
        dns_ready=true
        break
      fi
      busy sleep 1
    done
    [ "$dns_ready" = true ] ;;
  host-dns-wire)
    exe=$(original_elf /usr/bin/host)
    host_ready=false
    for attempt in 1 2 3 4 5 6; do
      busy timeout 9 "$exe" -4 -t A -p 15415 -R 1 -W 3 \
        naseed.test. 10.0.2.2 > "$work/client" 2> "$work/error" || :
      if busy grep -Fq '192.0.2.42' "$work/client" &&
         busy grep -Fq 'naseed.test' "$work/client"; then
        host_ready=true
        break
      fi
      busy sleep 1
    done
    [ "$host_ready" = true ]
    busy printf 'M141_SYSTEM_HOST_DNS_WIRE_PASS\n' ;;
  nslookup-dns-wire)
    exe=$(original_elf /usr/bin/nslookup)
    lookup_ready=false
    for attempt in 1 2 3 4 5 6; do
      busy timeout 9 "$exe" -type=A -port=15415 -timeout=3 -retry=1 \
        naseed.test. 10.0.2.2 > "$work/client" 2> "$work/error" || :
      if busy grep -Fq '192.0.2.42' "$work/client" &&
         busy grep -Fq 'naseed.test' "$work/client"; then
        lookup_ready=true
        break
      fi
      busy sleep 1
    done
    [ "$lookup_ready" = true ]
    busy printf 'M141_SYSTEM_NSLOOKUP_DNS_WIRE_PASS\n' ;;
  mdig-dns-wire)
    exe=$(original_elf /usr/bin/mdig)
    mdig_ready=false
    for attempt in 1 2 3 4 5 6; do
      busy timeout 9 "$exe" @10.0.2.2 -4 -p 15415 -t A \
        +tries=1 +timeout=3 naseed.test. > "$work/client" 2> "$work/error" || :
      if busy grep -Fq '192.0.2.42' "$work/client" &&
         busy grep -Fq 'naseed.test' "$work/client"; then
        mdig_ready=true
        break
      fi
      busy sleep 1
    done
    [ "$mdig_ready" = true ]
    busy printf 'M141_SYSTEM_MDIG_DNS_WIRE_PASS\n' ;;
  iperf3-loopback)
    exe=$(original_elf /usr/bin/iperf3)
    "$exe" -4 -s -1 -p 15412 > "$work/server" 2> "$work/server.err" &
    bgpid=$!
    busy sleep 1
    busy timeout 15 "$exe" -4 -c 127.0.0.1 -p 15412 -t 1 -J > "$work/client.json"
    busy grep -Fq 'bits_per_second' "$work/client.json"
    busy grep -Fq 'bytes' "$work/client.json" ;;
  nmap-loopback)
    nmap=$(original_elf /usr/bin/nmap)
    ncat=$(original_elf /usr/bin/ncat)
    "$ncat" -4 --listen --keep-open 127.0.0.1 15413 > "$work/server" 2> "$work/server.err" &
    bgpid=$!
    busy sleep 1
    busy timeout 20 "$nmap" -4 -n --unprivileged -sT -Pn -p 15413 127.0.0.1 > "$work/scan"
    busy grep -Eq '15413/tcp[[:space:]]+open' "$work/scan" ;;
  *) echo "[M141 SYSTEM UNKNOWN CASE] $case_name" >&2; exit 2 ;;
esac

exit 0
