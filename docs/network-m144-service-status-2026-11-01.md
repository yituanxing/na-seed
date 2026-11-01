# NaSeed M144 signed native-service cohort — 2026-11-01

Status: **3 of 4 independent QEMU network cases PASS; 4 of 5 new ELF identities PASS.**
This records observed results, not a claim that all M144 acceptance criteria
passed. All executed targets are genuine, different, unmodified official
Alpine v3.23 signed ELF binaries.

| Original ELF(s) | Real x86 QEMU test | Result |
|---|---|---|
| `/usr/bin/ab` | Four real HTTP requests to host oracle; zero failed requests and four host GET records | PASS |
| `/usr/sbin/nginx` | Guest server listens on native loopback and serves real content to original curl | PASS |
| `/usr/bin/valkey-server`, `/usr/bin/valkey-cli` | Guest native TCP RESP PING / SET / GET with exact value verification | PASS |
| `/usr/sbin/haproxy` | Native frontend 127.0.0.1:15444 forwards /haproxy to independent host 10.0.2.2:15441 | FAIL |

- Signed original ELF import: five distinct SHA-256 hashes, PASS.
- The complete native upstream lwIP/SocketPort/ABI regression gate: PASS.
- M143 Wget, aria2c, lighttpd HTTP program cohort: already PASS.
- M144 3/4 guest result: [GitHub Actions 41303412089](https://github.com/yituanxing/na-seed/actions/runs/41303412089).
- Updated differential traces: [41303896675](https://github.com/yituanxing/na-seed/actions/runs/41303896675).
- Genuine HAProxy TCP-mode / reduced-concurrency differentiation did not
  produce host HTTP evidence: [41304313495](https://github.com/yituanxing/na-seed/actions/runs/41304313495).
- This PR remains draft; do not record M144 as complete or merge based on partial success.

## Actual kernel fixes contributing to M144

1. `nas_lwip_socket_tcp_nodelay` queues native PCB Nagle option updates
   after nonblocking TCP CONNECT rather than incorrectly returning EBUSY.
   The original Valkey CLI now completes its RESP requests.
2. Repeated TCP connect after an established nonblocking handshake returns
   `EISCONN` instead of re-queuing native CONNECT and reporting EINPROGRESS
   indefinitely. Original ApacheBench now completes all four requests.
3. Preserve M143's indexed native Netconn final-close slot quarantine.
   The accepted cap remains eight; no unbounded FD/socket workaround.
4. Bring the host-only M106 ABI fixture in sync with real filtered slot
   reservations and held FileOps, keeping the native foundation gate active.

## Remaining HAProxy blocker and next bounded investigation

HAProxy's binary executes and reaches real socket creation, option controls,
bind and GETNAME, but the independent host HTTP oracle sees zero /haproxy
requests. A poll-only run (`-de`) and HAProxy TCP relay mode with a small
maxconn did not pass. Therefore do not label it only an epoll bug or only an
HTTP parser issue.

Target the producer/consumer edge of genuine native sockets: instrument the
original HAProxy's bind, listen, accept, connect and epoll/poll readiness
completion **without guessing a Linux socket success value**. Preserve complete
guest serial and host evidence; run the one M144 QEMU gate after a coherent
batch of fixes. Do not weaken the host /haproxy GET requirement.

Permanent syscall-completion debug printouts were removed again; retain
the existing opt-in syscall ingress trace and test-specific HAProxy output
capture as needed for differential diagnosis.
