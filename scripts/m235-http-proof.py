#!/usr/bin/env python3
"""Real xh TCP GET+200+full body: host, native QEMU PCAP, physical ext4.

Reuse M223's existing independently reviewed source, HTTP and TCP frame
oracles, scoped to one unpredictable per-boot route for original Alpine xh.
"""
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path
import sys
import tempfile


def module(name: str, alias: str):
    spec = spec_from_file_location(alias, Path(__file__).with_name(name))
    if spec is None or spec.loader is None:
        raise RuntimeError('M235 missing original M223 host-only HTTP oracle')
    child = module_from_spec(spec)
    spec.loader.exec_module(child)
    return child


def verify(phase: str, port: str, pcap: Path, log: Path, host_dir: Path,
           disk: Path) -> dict:
    if phase not in ('first', 'reopen'):
        raise ValueError('[M235 HOST FAIL] invalid independent boot phase')
    m223 = module('m223-http-wire-check.py', 'm235_m223_wire')
    m223.ROUTES = ('wget',)
    source = m223.disk_source.source_bytes(phase, host_dir, 'wget')
    sources = {'wget': source}
    host_text = log.read_text()
    if any(not entry.startswith(f'M223_HOST_GET phase={phase} path=/wget ')
           for entry in host_text.splitlines() if entry.startswith('M223_HOST_GET ')):
        raise ValueError('[M235 HOST FAIL] unexpected phase/route in HTTP receipt')
    result = m223.host_log_oracle(phase, host_text, sources)
    result.update(m223.real_http_flows(pcap.read_bytes(), sources))
    m191 = m223.disk_source.owner.load('m191-alpine-root-disk.py', 'm235_m191_http')
    if port not in m191.ARCH_MACHINE:
        raise ValueError('[M235 HOST FAIL] invalid ISA')
    with tempfile.TemporaryDirectory(prefix='m235-http-ext4-') as tmp:
        partition = Path(tmp) / 'root.ext4'
        m191.extract_partition(disk.resolve(strict=True), partition)
        actual = m191.debugfs_cat(partition, f'/root/m235-state/xh.{phase}.body')
        original = m191.debugfs_cat(partition,
                                   f'/root/m235-state/http-response-body.{phase}/download.body')
        if actual != source or original != source:
            raise ValueError('[M235 HTTP FAIL] actual xh response bytes differ from host-only random body')
    result.update(phase=phase, port=port, original_elf='/usr/bin/xh',
                  host_random_bytes=len(source), physical_ext4_exact_body=True)
    return result


def main() -> None:
    if len(sys.argv) != 8:
        raise SystemExit('usage: m235-http-proof.py PHASE ISA PCAP HOST_LOG HOST_DIR DISK OUT.json')
    phase, port = sys.argv[1:3]
    capture, log, host_dir, disk, out = map(Path, sys.argv[3:])
    if out.exists() or any(item.is_symlink() for item in (capture, log, host_dir, disk, out)):
        raise ValueError('[M235 HTTP FAIL] stale/symlinked independent proof')
    payload = verify(phase, port, capture, log, host_dir, disk)
    out.write_text(json.dumps(payload, indent=2, sort_keys=True) + '\n')
    print(f'[M235 REAL XH HTTP WIRE+DISK PASS] {phase} {port} original xh '\
          'GET/200/full host-random body in actual QEMU TCP, server receipt, ext4')


if __name__ == '__main__':
    main()
