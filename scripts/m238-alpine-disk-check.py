#!/usr/bin/env python3
"""M238 strict signed-APK SHA and actual first/reopen physical ext4 software oracle."""
from __future__ import annotations

import csv
import hashlib
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path
import re
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / 'scripts/m238-candidate-elfs.tsv'
PACKAGES = ROOT / 'rootfs/alpine/packages-m238-candidates.txt'
COUNT = 15

source = Path(__file__).with_name('m206-alpine-disk-check.py')
spec = spec_from_file_location('m238_verified_ext4_owner', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M238 must reuse M206 physical ext4 and original APK oracle')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)


def signed_sha(port: str, partition: Path, m191, home: Path = ROOT) -> dict[str, str]:
    """Match all 15 distinct physical ELF bytes against signed APK census."""
    census = home / f'build/external/m238-candidate-inventory/{port}/candidates.tsv'
    if census.is_symlink():
        raise ValueError('[M238 SIGNED FAIL] signed APK census symlink')
    with census.open(newline='', encoding='utf-8') as fp:
        rows = list(csv.reader(fp, delimiter='\t'))
    if not rows or rows[0] != ['package', 'elf_path', 'status', 'sha256']:
        raise ValueError('[M238 SIGNED FAIL] invalid signed APK census')
    expected = {(row[0], row[1]) for row in csv.reader(
        (line for line in MANIFEST.read_text().splitlines()[1:]
         if line.strip() and not line.startswith('#')), delimiter='\t')}
    if len(expected) != COUNT:
        raise ValueError('[M238 SIGNED FAIL] wrong native ELF count')
    seen: set[tuple[str, str]] = set()
    unique: set[str] = set()
    fingerprints: dict[str, str] = {}
    for row in rows[1:]:
        if len(row) != 4:
            raise ValueError('[M238 SIGNED FAIL] malformed census row')
        package, elf, status, sha = row
        if ((package, elf) not in expected or (package, elf) in seen or
                status != 'NATIVE_ELF' or not re.fullmatch('[0-9a-f]{64}', sha)):
            raise ValueError('[M238 SIGNED FAIL] duplicate or nonnative original ELF')
        actual = m191.debugfs_cat(partition, '/' + elf)
        if hashlib.sha256(actual).hexdigest() != sha:
            raise ValueError('[M238 SIGNED FAIL] physical ELF differs from signed APK: ' + elf)
        seen.add((package, elf))
        unique.add(sha)
        fingerprints[elf] = sha
    if seen != expected or len(unique) != COUNT:
        raise ValueError('[M238 SIGNED FAIL] 15 unique original signed APK ELFs unproven')
    return fingerprints


def require_real_function_cfg(dot: bytes) -> None:
    # The Khronos fixed DOT legend contains digraph and label even when NO
    # parsed function blocks were emitted; require a real function entry box.
    if b'Fn ' not in dot or b' entry' not in dot or b'shape=box' not in dot:
        raise ValueError('[M238 SEMANTIC FAIL] spirv-cfg generated only graph legend, not real function CFG')


def real_semantics(partition: Path, m191) -> dict[str, str]:
    """Read actual physical ext4 bytes; do not trust guest PASS printouts."""
    def read(relative: str) -> bytes:
        return m191.debugfs_cat(partition, '/root/m238-state/' + relative)

    wat = read('add.wat')
    if b'(export "add")' not in wat or b'i32.add' not in wat or read('add.wasm')[:4] != b'\0asm':
        raise ValueError('[M238 SEMANTIC FAIL] actual source WASM module absent')
    asm = read('compute.spvasm')
    if (b'OpEntryPoint GLCompute' not in asm or
            b'OpMemoryModel Logical GLSL450' not in asm or
            read('compute.spv')[:4] != b'\x03\x02\x23\x07'):
        raise ValueError('[M238 SEMANTIC FAIL] actual compute SPIR-V module absent')
    if (not read('sample.git/.git/HEAD').startswith(b'ref: ') or
            read('sample.git/alpha.txt') != b'NaSeed M238 real persisted git blob alpha\n' or
            read('sample.git/beta.txt') != b'NaSeed M238 real persisted git blob beta\n'):
        raise ValueError('[M238 SEMANTIC FAIL] original git commit/repository state absent')

    primary = {
        'wasm-assemble': 'assembled.wasm',
        'wasm-disassemble': 'roundtrip.wat',
        'wasm-optimize': 'optimized.wasm',
        'wasm-merge-modules': 'merged.wasm',
        'wasm-to-javascript': 'translated.js',
        'wasm-spec-assert': 'assertions.log',
        'spirv-assemble': 'assembled.spv',
        'spirv-disassemble': 'disassembled.spvasm',
        'spirv-validate': 'invalid.spv',
        'spirv-optimize': 'optimized.spv',
        'spirv-control-flow': 'control-flow.dot',
        'capstone-decode': 'decoded.txt',
        'cppcheck-errors': 'unsafe.c',
        'git-repo-size': 'sizer.json',
        'actions-yaml-lint': 'invalid.yml',
    }
    witness_sha: dict[str, str] = {}
    for phase in ('first', 'reopen'):
        def actual(case: str, name: str) -> bytes:
            contents = read(f'{case}.{phase}/{name}')
            if not contents:
                raise ValueError(f'[M238 SEMANTIC FAIL] physical {phase}/{case}/{name} missing')
            return contents

        if actual('wasm-assemble', 'assembled.wasm')[:4] != b'\0asm':
            raise ValueError('[M238 SEMANTIC FAIL] wasm-as did not compile real wasm bytes')
        wdis = actual('wasm-disassemble', 'roundtrip.wat')
        wopt = actual('wasm-optimize', 'optimized.wat')
        wmerge = actual('wasm-merge-modules', 'merged.wat')
        js = actual('wasm-to-javascript', 'translated.js')
        if (b'i32.add' not in wdis or b'add' not in wdis or
                b'i32.add' not in wopt or b'add' not in wopt or
                b'add' not in wmerge or b'ten' not in wmerge or
                b'add' not in js or not re.search(rb'(\bfunction\b|=>|\bexport\b)', js)):
            raise ValueError('[M238 SEMANTIC FAIL] original WASM transform lacks real function')
        for case, filename in [('wasm-optimize', 'optimized.wasm'), ('wasm-merge-modules', 'merged.wasm')]:
            if actual(case, filename)[:4] != b'\0asm':
                raise ValueError('[M238 SEMANTIC FAIL] transformed wasm has no ELF-independent magic')
        if (b'checks passed' not in actual('wasm-spec-assert', 'assertions.log').lower() or
                b'const 10' not in actual('wasm-spec-assert', 'false.wast') or
                not actual('wasm-spec-assert', 'rejected.log')):
            raise ValueError('[M238 SEMANTIC FAIL] wasm-shell did not prove positive and wrong-result rejection')
        if actual('spirv-assemble', 'assembled.spv')[:4] != b'\x03\x02\x23\x07':
            raise ValueError('[M238 SEMANTIC FAIL] spirv-as did not produce actual magic')
        if (b'OpEntryPoint GLCompute' not in actual('spirv-disassemble', 'disassembled.spvasm') or
                b'OpEntryPoint GLCompute' not in actual('spirv-optimize', 'optimized.spvasm') or
                actual('spirv-optimize', 'optimized.spv')[:4] != b'\x03\x02\x23\x07'):
            raise ValueError('[M238 SEMANTIC FAIL] SPIR-V roundtrip/optimizer wrong')
        if (b'OpMemoryModel' in actual('spirv-validate', 'invalid.spvasm') or
                not actual('spirv-validate', 'rejected.log')):
            raise ValueError('[M238 SEMANTIC FAIL] SPIR-V missing memory model not rejected')
        require_real_function_cfg(actual('spirv-control-flow', 'control-flow.dot'))
        if b'nop' not in actual('capstone-decode', 'decoded.txt').lower():
            raise ValueError('[M238 SEMANTIC FAIL] real Capstone instruction decode absent')
        if (b'a[3]' not in actual('cppcheck-errors', 'unsafe.c') or
                not re.search(rb'(arrayIndexOutOfBounds|out of bounds)',
                              actual('cppcheck-errors', 'rejected.log'), re.I)):
            raise ValueError('[M238 SEMANTIC FAIL] cppcheck genuine OOB diagnostic absent')
        metrics = json.loads(actual('git-repo-size', 'sizer.json'))
        if (not isinstance(metrics, dict) or metrics.get('unique_commit_count') != 1 or
                metrics.get('unique_blob_count') != 2):
            raise ValueError('[M238 SEMANTIC FAIL] git-sizer real commit/blob metrics wrong')
        if (b'runs-on: [foo' not in actual('actions-yaml-lint', 'invalid.yml') or
                not actual('actions-yaml-lint', 'rejected.log')):
            raise ValueError('[M238 SEMANTIC FAIL] actionlint malformed GitHub workflow not rejected')
        for case in primary:
            actual(case, 'rejected.log') if case not in ('git-repo-size',) else None
            content = actual(case, primary[case])
            witness_sha[f'{case}.{phase}/{primary[case]}'] = hashlib.sha256(content).hexdigest()
    return witness_sha


def verify(port: str, signed_root: Path, disk: Path) -> None:
    owner.verify(port, signed_root, disk, candidates=MANIFEST,
                 packages=PACKAGES, expected_count=COUNT, label='M238')
    m191 = owner.load('m191-alpine-root-disk.py', 'm238_m191_physical')
    with tempfile.TemporaryDirectory(prefix='m238-signed-real-ext4-') as temp:
        partition = Path(temp) / 'root.ext4'
        m191.extract_partition(disk.resolve(strict=True), partition)
        original = signed_sha(port, partition, m191)
        outputs = real_semantics(partition, m191)
        proof = dict(milestone='M238', port=port, original_signed_elf_sha256=original,
                     first_and_reopen_physical_output_sha256=outputs,
                     physical_ext4_real_app_semantics=True,
                     oracle='signed-APK-census-plus-two-native-QEMUs-plus-physical-ext4-bytes')
    (disk.parent / 'm238-physical-ext4-evidence.json').write_text(
        json.dumps(proof, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print('[M238 PHYSICAL DISK PASS] 15 unique signed original Alpine ELF SHA '
          'and 30 first/reopen original function bytes verified from ext4')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('usage: m238-alpine-disk-check.py PORT SIGNED_ROOT DISK')
    verify(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]))
