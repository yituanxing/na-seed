# M229–M239 original Alpine native evidence (2026-11-08 UTC)

This is a durable **evidence index**, not a substitute for the raw GitHub
Actions job logs, serial, packet captures, and ext4 disk oracles. Artifact IDs
and SHA-256 values below refer to the **GitHub artifact ZIP**, as reported by
the Actions artifact API. The files expire under the repository's artifact
retention policy; a matching digest does not imply the bytes remain available.

Acceptance requires actual target-ISA system-QEMU, two independent original
signed Alpine PID1/OpenRC boots, and the named job's final host/guest proof.
Host-only unit tests, a green CI summary, an available Limine cache, the
first UART marker alone, and successful APK installation are not final proof.

## M229: SHA-verified shared Limine pilot, native Valkey on three ISAs

- Source commit: `ae828990192d1416416ca1014ab50c245e3f2f4b`
- Run: <https://github.com/yituanxing/na-seed/actions/runs/42487761251>
- All three original job logs ended with
  `[M229 ORIGINAL ALPINE VALKEY HOST RESP TWO BOOT PASS]`.
- Each proved signed Alpine Valkey/OpenRC, original PID1 reboot, external host
  AUTH/SET/GET/SAVE then AUTH/GET, dual physical NIC PCAP, matching durable
  RDB on actual ext4, and strict read-only fsck.

| ISA | Artifact ID | GitHub ZIP SHA-256 |
| --- | ---: | --- |
| amd64 | `13620906834` | `c10e2bf9537bfba0b7ef48f1c5d91408310773871cf7713b2976ada17ec8d16c` |
| riscv64 | `13620668010` | `e32c1d3077dda98357bad7cd7dd7ec096f1ad76361902304b8dc9451dda48dbc` |
| arm64 | `13620917239` | `68c35e5bad52122ecc9fee791e1a3e6a7310b46c1f5319f990344a495c0d62b0` |

## M239: genuine interactive TTY and four simultaneous OpenRC services

- Source commit: `3ef8d463e1ce213ae2b32b4b8777f58a266e6b43`
- Run: <https://github.com/yituanxing/na-seed/actions/runs/42489084529>
- Each of three original job logs contained **both**
  `[M239 ORIGINAL ALPINE INTERACTIVE TTY PASS]` and
  `[M239 ORIGINAL ALPINE FOUR SERVICE HOST PROTOCOL TWO BOOT PASS]`.
- Real UART accepted canonical erase/read, interrupted an actual foreground
  `/bin/sleep` with injected Ctrl-C, kept all four OpenRC daemons alive,
  Ctrl-D exited to a second genuine getty and password `/bin/login`, and
  preserved the original MQTT graceful-stop/database sync guarantee. The
  independent HTTP, DNS UDP/TCP, retained MQTT, volatile Memcached host
  challenges, PCAP and ext4/fsck oracles also completed after two QEMUs.
- The earlier run `42486051662` **failed** because the old MQTT persistence
  script intentionally stopped Mosquitto before the newly added liveness
  check. That failing run is **not** included as PASS. Commit `3ef8d463`
  re-armed the daemon before the TTY check and restored graceful stop after.

| ISA | Artifact ID | GitHub ZIP SHA-256 |
| --- | ---: | --- |
| amd64 | `13621460697` | `2f3ad2a2976687aa6b0059b4d29b2a5277fb0e357d538a5ac7675dea5eddd055` |
| riscv64 | `13622345554` | `9037d59421a6c0acaef813e481111ed2a5fbacada7dce765fce5128f7c726574` |
| arm64 | `13621852727` | `96379c2e6617e62bcc0c840a20dfbde1a1f539b1bf4a5962e3e05bd36d2c53d9` |

## Five further CI workflows: native 15/15 after shared Limine adoption

- Source commit: `c118c701e73a2ef4b8d793fc013b3866952df6b4`
- All five workflows completed/success with that **exact** `head_sha`.
- All fifteen jobs passed the fixed-source SHA-verified Limine action, then
  completed actual native QEMU and the **named final software-specific proof**.
- The shared action fails closed if neither matching cache nor the pinned
  same-repository historic artifact is available; it does not pull latest.

| Workload | Run ID | Final native proof marker | Verified behavior |
| --- | ---: | --- | --- |
| M197 | [`42489535857`](https://github.com/yituanxing/na-seed/actions/runs/42489535857) | `M197 ALPINE PUBLIC SOFTWARE PERSISTENCE PASS` | signed public Bash/Lua/SQLite/Git; second native QEMU, Git+SQL disk oracle and fsck |
| M230 | [`42489535875`](https://github.com/yituanxing/na-seed/actions/runs/42489535875) | `M230 ORIGINAL ALPINE DROPBEAR HOST SSH TWO BOOT PASS` | host Ed25519 remote command, persistent host key, two PCAPs/ext4 |
| M231 | [`42489535845`](https://github.com/yituanxing/na-seed/actions/runs/42489535845) | `M231 ORIGINAL ALPINE MOSQUITTO HOST RETAINED MQTT TWO BOOT PASS` | QoS1 retained publish/subscribe before and after reboot, broker DB, two PCAPs/fsck |
| M232 | [`42489535848`](https://github.com/yituanxing/na-seed/actions/runs/42489535848) | `M232 ORIGINAL ALPINE MEMCACHED HOST VOLATILE TWO BOOT PASS` | host miss/SET/GET, reboot misses old key, new SET/GET, PCAPs/ext4 |
| M233 | [`42489535852`](https://github.com/yituanxing/na-seed/actions/runs/42489535852) | `M233 ORIGINAL ALPINE DNSMASQ OPENRC HOST UDP+TCP TWO BOOT PASS` | independent host UDP and TCP DNS queries, two PCAPs/ext4 fsck |

| Workload | ISA | Artifact ID | GitHub ZIP SHA-256 |
| --- | --- | ---: | --- |
| M197 | amd64 | `13621182639` | `cc514e9e26a2af5ed4b349b9d6f969cc989895a432216a4557a506b7436dbac5` |
| M197 | riscv64 | `13622025044` | `979efee2077b74a84935119f7fe549791d70ef04bd636571181bdb45d47cd18c` |
| M197 | arm64 | `13621591646` | `6180df920da764bb1481b54cdea1384511cfda462aa045bf63d92cd603107dd9` |
| M230 | amd64 | `13621162788` | `ab2afd0eb9432c0b18edefae39c67d6ff22e56d941a7e76aafe6ac48270fb3ea` |
| M230 | riscv64 | `13622175202` | `80d3435eac5f959df01566b413507e18b36cc30d99141308d334bc16ec7b8528` |
| M230 | arm64 | `13622040518` | `11156228ba3934ce2ac997dae2933f182fe092ba7c1d7b13e08a8db24b957001` |
| M231 | amd64 | `13620959711` | `5bc0c89ca41ecb44570ebff8acd96b930b65ac6e477dffaea7edb694dade39c8` |
| M231 | riscv64 | `13621169443` | `041fdae960205f7dd12016fef221e31a3e45e06a9b49e2227a3d42325f261bd1` |
| M231 | arm64 | `13621650490` | `e8923e9c821728a43e0639ac1cae5f55f574d7f50f5397292e3f6f540753d883` |
| M232 | amd64 | `13621371635` | `6ea02cc52a4d09861145192b9b607d2a90d0fc3e3a3af65a4de233a5421456a5` |
| M232 | riscv64 | `13621288958` | `5ff64f8c10f2419f83ff311eb5cb3118dd89e07e32ba4019864663fb004224ee` |
| M232 | arm64 | `13621585836` | `50214efb71dafc82b843f943a32f4b1ebe152c9ec0a0068bfe20389e894b5355` |
| M233 | amd64 | `13620938005` | `2f7d31d2333cef6dec8ffe2e85d0121ec6076b40bf134cd815f5c72a93acb63f` |
| M233 | riscv64 | `13621686029` | `548fae65a3334e0693a8076108ed86494fb4a802b657c4e84644a038f39e793f` |
| M233 | arm64 | `13621635711` | `1de4f8e829ee530c250bf0fe61ea315152a63d6d46fb8df4aaf96bd3851be876` |

## Evidence scope and future replay

The M239 run was built from `3ef8d463`; the five software workflows were
built from the later `c118c701`, whose five changed files were workflow YAML
only. Do **not** relabel the M239 artifact as a fresh `c118c701` binary run.
The later commit did not change M239's tested source.

Before replay, recheck artifact expiry and source SHA. Native QEMU acceptance
must reproduce guest UART final markers and host protocol/PCAP/ext4 oracles,
not merely successful artifact restore or host static checks.
