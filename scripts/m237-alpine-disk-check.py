#!/usr/bin/env python3
"""M237 independent signed APK SHA and two-boot physical ext4 semantic oracle."""
from __future__ import annotations

import csv
import hashlib
from importlib.util import module_from_spec, spec_from_file_location
import json
import math
from pathlib import Path
import re
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / 'scripts/m237-candidate-elfs.tsv'
PACKAGES = ROOT / 'rootfs/alpine/packages-m237-candidates.txt'
NATIVE_COUNT = 15

source = Path(__file__).with_name('m206-alpine-disk-check.py')
spec = spec_from_file_location('m237_verified_ext4_owner', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M237 must reuse actual M206 original Alpine physical oracle')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)


def signed_sha(port: str, partition: Path, m191, home: Path = ROOT) -> dict[str, str]:
    """Every ext4 executable matches the official signed-host APK census SHA."""
    census = home / f'build/external/m237-candidate-inventory/{port}/candidates.tsv'
    if census.is_symlink():
        raise ValueError('[M237 SIGNED FAIL] symlinked official signed APK inventory')
    with census.open(newline='', encoding='utf-8') as fp:
        rows = list(csv.reader(fp, delimiter='\t'))
    if not rows or rows[0] != ['package', 'elf_path', 'status', 'sha256']:
        raise ValueError('[M237 SIGNED FAIL] missing official original APK census')
    expected = {(row[0], row[1]) for row in csv.reader(
        (line for line in MANIFEST.read_text().splitlines()[1:]
         if line.strip() and not line.startswith('#')), delimiter='\t')}
    if len(expected) != NATIVE_COUNT:
        raise ValueError('[M237 SIGNED FAIL] signed original manifest count wrong')
    seen: set[tuple[str, str]] = set()
    unique: set[str] = set()
    fingerprints: dict[str, str] = {}
    for row in rows[1:]:
        if len(row) != 4:
            raise ValueError('[M237 SIGNED FAIL] malformed census row')
        pkg, elf, status, sha = row
        if ((pkg, elf) not in expected or (pkg, elf) in seen or
                status != 'NATIVE_ELF' or not re.fullmatch('[0-9a-f]{64}', sha)):
            raise ValueError('[M237 SIGNED FAIL] duplicate/non-original signed ELF')
        actual = m191.debugfs_cat(partition, '/' + elf)
        if hashlib.sha256(actual).hexdigest() != sha:
            raise ValueError('[M237 SIGNED FAIL] signed APK SHA differs from physical ELF: ' + elf)
        seen.add((pkg, elf))
        unique.add(sha)
        fingerprints[elf] = sha
    if seen != expected or len(unique) != NATIVE_COUNT:
        raise ValueError('[M237 SIGNED FAIL] 15 distinct signed native ELFs unproven')
    return fingerprints


def verify_geos_binary(phase: str, distance: bytes, intersects_true: bytes,
                       intersects_false: bytes) -> None:
    """Fail closed on original geosop geometry stdout from physical ext4."""
    if not re.fullmatch(rb'5(?:\.0+)?\r?\n', distance):
        raise ValueError(f'[M237 SEMANTIC FAIL] {phase} GEOS binary point distance != 5')
    if intersects_true != b'true\n':
        raise ValueError(f'[M237 SEMANTIC FAIL] {phase} GEOS same-point intersection != true')
    if intersects_false != b'false\n':
        raise ValueError(f'[M237 SEMANTIC FAIL] {phase} GEOS distinct-point intersection != false')


def verify_rec_csv_export(phase: str, output: bytes) -> None:
    """Check the exact original quoted Recutils CSV, not unquoted substrings."""
    # Original signed Alpine recutils 1.9-r2 Linux run #42452263322 proved
    # this 50-byte output; independent physical ext4 must contain those bytes.
    expected = b'"Name","Role"\n"Ada","Engineer"\n"Lin","Researcher"\n'
    try:
        parsed = list(csv.reader(output.decode('utf-8').splitlines()))
    except (UnicodeDecodeError, csv.Error) as exc:
        raise ValueError(f'[M237 SEMANTIC FAIL] {phase} Recutils invalid CSV syntax') from exc
    if (parsed != [['Name', 'Role'], ['Ada', 'Engineer'], ['Lin', 'Researcher']]
            or output != expected):
        raise ValueError(f'[M237 SEMANTIC FAIL] {phase} original Recutils CSV bytes/records wrong')


def real_semantics(partition: Path, m191) -> dict[str, str]:
    read = lambda relative: m191.debugfs_cat(partition, '/root/m237-state/' + relative)
    result = lambda case, phase, name: read(f'{case}.{phase}/{name}')
    source = b'%rec: Contact\n\nName: Ada\nRole: Engineer\n\nName: Lin\nRole: Researcher\n'
    csv_source = b'Name,Role\nAda,Engineer\nLin,Researcher\n'
    if read('contacts.rec') != source or read('contacts.csv') != csv_source:
        raise ValueError('[M237 SEMANTIC FAIL] first-boot record data not persisted')
    expected_json = {'name': 'NaSeed', 'items': [3, 5], 'ready': True}
    if json.loads(read('structure.json')) != expected_json:
        raise ValueError('[M237 SEMANTIC FAIL] first-boot JSON input absent')
    if read('signed.message') != b'M237_ORIGINAL_MINISIGN_DURABLE_SIGNATURE\n':
        raise ValueError('[M237 SEMANTIC FAIL] signed message input absent')
    if (read('geos-origin.wkt') != b'POINT (0 0)\n' or
            read('geos-diagonal.wkt') != b'POINT (3 4)\n'):
        raise ValueError('[M237 SEMANTIC FAIL] original persistent GEOS point inputs absent')
    if read('longitude-latitude.txt') != b'10 20\n':
        raise ValueError('[M237 SEMANTIC FAIL] original geographic input absent')
    private = read('minisign.secret')
    public = read('minisign.pub')
    signature = read('message.minisig')
    if not all((private, public, signature)) or len(set((private, public, signature))) != 3:
        raise ValueError('[M237 SEMANTIC FAIL] real Ed25519 persisted key/signature missing')

    physical_sha: dict[str, str] = {}
    expected_fingerprints = {
        'rec-csv-import': 'imported.rec',
        'rec-csv-export': 'export.csv',
        'rec-remove-selected': 'deleted.rec',
        'rec-structural-check': 'invalid.rec',
        'rec-template-render': 'rendered.txt',
        'rec-record-count': 'info.txt',
        'rec-insert-record': 'inserted.rec',
        'rec-select-expression': 'selected.rec',
        'rec-set-field': 'updated.rec',
        'yajl-parse-reformat': 'reformatted.json',
        'yajl-valid-invalid': 'invalid.json',
        'minisign-persistent-signature': 'verified.log',
        'geos-binary-distance-intersects': 'distance.txt',
        'proj-crs-info': 'crs.txt',
        'proj-mercator-coordinates': 'mercator.xy',
    }
    for phase in ('first', 'reopen'):
        def actual(case: str, file: str) -> bytes:
            data = result(case, phase, file)
            if not data:
                raise ValueError(f'[M237 SEMANTIC FAIL] {phase} missing original {case}/{file}')
            return data

        csv_import = actual('rec-csv-import', 'imported.rec')
        if (any(word not in csv_import for word in
                (b'Ada', b'Lin', b'Engineer', b'Researcher')) or
                result('rec-csv-import', phase, 'import.count').strip() != b'2'):
            raise ValueError('[M237 SEMANTIC FAIL] CSV→Rec conversion wrong')
        csv_export = actual('rec-csv-export', 'export.csv')
        verify_rec_csv_export(phase, csv_export)
        deleted = actual('rec-remove-selected', 'deleted.rec')
        if (b'Name: Ada' not in deleted or b'Name: Lin' in deleted or
                result('rec-remove-selected', phase, 'count.txt').strip() != b'1'):
            raise ValueError('[M237 SEMANTIC FAIL] original recdel removed wrong record')
        invalid_rec = actual('rec-structural-check', 'invalid.rec')
        if (b'Name Ada' not in invalid_rec or invalid_rec == source or
                not actual('rec-structural-check', 'invalid.log')):
            raise ValueError('[M237 SEMANTIC FAIL] recfix invalid detection absent')
        rendered = actual('rec-template-render', 'rendered.txt')
        if b'CONTACT=Ada|Engineer' not in rendered or b'CONTACT=Lin|Researcher' not in rendered:
            raise ValueError('[M237 SEMANTIC FAIL] recfmt substituted wrong fields')
        if not re.search(rb'(?m)^2\s+Contact$', actual('rec-record-count', 'info.txt')):
            raise ValueError('[M237 SEMANTIC FAIL] recinf real count absent')
        inserted = actual('rec-insert-record', 'inserted.rec')
        if (b'Name: Grace' not in inserted or b'Role: Scientist' not in inserted or
                result('rec-insert-record', phase, 'count.txt').strip() != b'3'):
            raise ValueError('[M237 SEMANTIC FAIL] recins new typed record not inserted')
        selected = actual('rec-select-expression', 'selected.rec')
        if b'Name: Lin' not in selected or b'Name: Ada' in selected:
            raise ValueError('[M237 SEMANTIC FAIL] recsel query wrong')
        updated = actual('rec-set-field', 'updated.rec')
        if (b'Role: ResearchLead' not in updated or b'Role: Researcher' not in updated or
                b'Role: ResearchLead' not in actual('rec-set-field', 'selected.rec')):
            raise ValueError('[M237 SEMANTIC FAIL] recset field update wrong')
        if json.loads(actual('yajl-parse-reformat', 'reformatted.json')) != expected_json:
            raise ValueError('[M237 SEMANTIC FAIL] YAJL structured reformat wrong')
        if (actual('yajl-valid-invalid', 'invalid.json') != b'{"broken":}\n' or
                not actual('yajl-valid-invalid', 'invalid.log')):
            raise ValueError('[M237 SEMANTIC FAIL] YAJL invalid JSON rejection missing')
        verified = actual('minisign-persistent-signature', 'verified.log')
        if b'verified' not in verified.lower() or not actual('minisign-persistent-signature', 'forged.log'):
            raise ValueError('[M237 SEMANTIC FAIL] original Ed25519 verify/tamper rejection absent')
        if b'M237_ORIGINAL_MINISIGN_TAMPERED_SIGNATURE' != actual('minisign-persistent-signature', 'tampered.message').strip():
            raise ValueError('[M237 SEMANTIC FAIL] signed-byte tamper did not change contents')
        geos = 'geos-binary-distance-intersects'
        verify_geos_binary(phase, actual(geos, 'distance.txt'),
                           actual(geos, 'intersects-true.txt'),
                           actual(geos, 'intersects-false.txt'))
        crs = actual('proj-crs-info', 'crs.txt')
        if not re.search(rb'(WGS 84|longlat|geographic)', crs, re.I):
            raise ValueError('[M237 SEMANTIC FAIL] real PROJ CRS lookup wrong')
        coordinates = actual('proj-mercator-coordinates', 'mercator.xy').decode().split()
        try:
            x, y = float(coordinates[0]), float(coordinates[1])
        except (ValueError, IndexError) as exc:
            raise ValueError('[M237 SEMANTIC FAIL] PROJ did not return numeric coordinates') from exc
        if not (1113194 < x < 1113196 and 2258423 < y < 2258425 and math.isfinite(x + y)):
            raise ValueError('[M237 SEMANTIC FAIL] real WGS84→Mercator computation wrong')

        for case, file in expected_fingerprints.items():
            physical_sha[f'{case}.{phase}/{file}'] = hashlib.sha256(actual(case, file)).hexdigest()
        for file in ('intersects-true.txt', 'intersects-false.txt'):
            physical_sha[f'{geos}.{phase}/{file}'] = hashlib.sha256(actual(geos, file)).hexdigest()
    return physical_sha


def verify(port: str, signed_root: Path, disk: Path) -> None:
    owner.verify(port, signed_root, disk,
                 candidates=MANIFEST, packages=PACKAGES,
                 expected_count=NATIVE_COUNT, label='M237')
    m191 = owner.load('m191-alpine-root-disk.py', 'm237_m191_physical')
    with tempfile.TemporaryDirectory(prefix='m237-signed-real-ext4-') as tmp:
        partition = Path(tmp) / 'root.ext4'
        m191.extract_partition(disk.resolve(strict=True), partition)
        original = signed_sha(port, partition, m191)
        outputs = real_semantics(partition, m191)
        if len(outputs) != 34:
            raise ValueError('[M237 SEMANTIC FAIL] expected 34 first/reopen physical output witnesses')
        proof = dict(milestone='M237', port=port, original_signed_elf_sha256=original,
                     first_and_reopen_physical_output_sha256=outputs,
                     physical_ext4_real_app_semantics=True,
                     oracle='signed-index-plus-two-native-QEMUs-plus-ext4-original-bytes')
    (disk.parent / 'm237-physical-ext4-evidence.json').write_text(
        json.dumps(proof, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print('[M237 PHYSICAL DISK PASS] 15 unique signed original Alpine ELF SHA '
          'and 34 first/reopen real semantic byte witnesses match actual physical ext4')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('usage: m237-alpine-disk-check.py PORT SIGNED_ROOT DISK')
    verify(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]))
