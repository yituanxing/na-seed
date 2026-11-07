# M147: 29 new original Alpine 3.23 networking ELF identities

**Status: new test cohort prepared; native NaSeed QEMU acceptance pending.**

The v3.23 upstream APKINDEX (main + community) and the actual x86_64 APK
tarballs were inspected. Exactly 29 chosen entries have an ELF header and 29
different original SHA-256 digests, including BIRD, Chrony, Dnsmasq, Fping,
Memcached, Mosquitto broker and clients, MTR, nghttp2 clients/servers, Stunnel,
TCPFlow, Unbound, Websocat and Whois. No existing milestone ELF path is reused;
the already-known signed Ncat is a dependency and is **not** counted again.

`scripts/stage-m147-prebuilt.sh` will demand signed Alpine APK installation,
correct target ELF machine type, a unique actual file hash for every identity,
and no duplicate historical path on each target ISA. No executable is rebuilt,
substituted by a stub or linked inside NaSeed.

One native x86-64 QEMU boot, once available in GitHub Actions, will invoke each
of the 29 ELF images with bounded version/help probes and require actual kernel
`[EXEC]` records. Three stronger separate guest-loopback tests are planned:

1. Actual Memcached ASCII SET/GET using the original Ncat client.
2. MQTT PUBLISH/SUBSCRIBE with signed Mosquitto broker, publisher, subscriber.
3. HTTP/2 h2c static file GET from original nghttpd to original nghttp.

The gate requires actual VirtIO-Net DHCP, all 29 exec records, distinct guest
semantic markers, and preserves signed SHA-256 and QEMU serial/PCAP evidence.
All protocol tests are intentionally strict and may reveal real ABI/lwIP gaps.
This is **not** a claim that the software already passed QEMU. Real external
network, TLS, sustained concurrency and cross-ISA parity are not covered here.
