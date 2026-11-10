#!/usr/bin/env python3
"""Read-only physical M223 ext4 symlink forensics, never an acceptance bypass.

Usage: m223-ext4-symlink-diagnostic.py DISK OUT_LOG
       m223-ext4-symlink-diagnostic.py --self-test

DISK is the actual MBR-backed QEMU disk, not a host staging tree. The
existing M191 partition extractor supplies the immutable debugfs input.
All debugfs invocations omit -w; command failures remain visible and cause
this diagnostic to exit nonzero after saving its bounded log.
"""

from __future__ import annotations

from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile


HASH_LINKS = (
    ("3bde41ac.0", "Autoridad_de_Certificacion_Firmaprofesional_CIF_A62634068"),
    ("7719f463.0", "Hellenic_Academic_and_Research_Institutions_ECC_RootCA_2015"),
    ("32888f65.0", "Hellenic_Academic_and_Research_Institutions_RootCA_2015"),
    ("09789157.0", "Starfield_Services_Root_Certificate_Authority_-_G2"),
)
# The first two are *comparison* CA links made by update-ca-certificates,
# whose long absolute targets may distinguish them from c_rehash hash links.
# The final short hash is a control, not a presumed proof of filesystem health.
CONTROLS = (
    "ca-cert-Autoridad_de_Certificacion_Firmaprofesional_CIF_A62634068.pem",
    "ca-cert-Hellenic_Academic_and_Research_Institutions_ECC_RootCA_2015.pem",
    "4042bcee.0",  # ISRG Root X1, short c_rehash target.
)
CERT_DIR = PurePosixPath("/etc/ssl/certs")
MAX_COMMAND_BYTES = 4096
MAX_TARGET_BYTES = 512
MAX_LOG_BYTES = 65536
INODE_LINE = re.compile(rb"(?:^|\n)Inode:\s*(\d+)\b")
DEBUGFS_ERROR = re.compile(
    rb"(?i)(?:file not found|no such file or directory|while trying to resolve|"
    rb"invalid inode number|ext2fs_[a-z_]+:\s*error|permission denied)"
)


def cert_path(basename: str) -> str:
    """Reject accidental debugfs commands/path traversal from basename."""
    if not re.fullmatch(r"[A-Za-z0-9_.=-]+", basename) or ".." in basename:
        raise ValueError("unsafe certificate basename")
    path = CERT_DIR / basename
    if len(str(path)) > 220 or path.parent != CERT_DIR:
        raise ValueError("certificate path outside supported debugfs boundary")
    return str(path)


def predict_hash_target(name: str) -> str:
    return f"ca-cert-{name}.pem"


def bounded(raw: bytes, limit: int = MAX_COMMAND_BYTES) -> str:
    """Print byte-exact escaped evidence with an explicit truncation counter."""
    excerpt = raw[:limit]
    escaped = excerpt.decode("utf-8", "backslashreplace").encode(
        "unicode_escape", "backslashreplace").decode("ascii")
    if len(raw) > limit:
        escaped += f" [TRUNCATED {len(raw) - limit} BYTES]"
    return escaped


def read_m191(disk: Path):
    size = disk.stat().st_size
    mib = size // (1024 * 1024)
    if size != mib * 1024 * 1024 or mib not in (256, 512, 768):
        raise ValueError(f"unsupported M191 physical disk size: {size} bytes")
    # M191 determines its MBR partition geometry at import time. Derive the
    # value from the actual disk, so the 512MiB M223 image is not truncated
    # to M191's ordinary 256MiB baseline when the runner omits the env var.
    os.environ["NAS_M191_DISK_MIB"] = str(mib)
    source = Path(__file__).with_name("m191-alpine-root-disk.py")
    spec = spec_from_file_location("m223_symlink_m191", source)
    if spec is None or spec.loader is None:
        raise RuntimeError("M191 physical ext4 owner unavailable")
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    module.check_mbr(disk)
    return module, mib


def invoke(partition: Path, command: str) -> tuple[int, bytes, bytes]:
    result = subprocess.run(
        ["debugfs", "-R", command, str(partition)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=30, check=False)
    return result.returncode, result.stdout, result.stderr


def self_test() -> None:
    assert [len(predict_hash_target(name)) for _, name in HASH_LINKS] == [69, 71, 67, 62]
    assert len({cert_path(name) for name, _ in HASH_LINKS}) == 4
    assert cert_path("4042bcee.0") == "/etc/ssl/certs/4042bcee.0"
    for bad in ("../x", "a/b", "x y", "foo;stat", "", "a" * 300):
        try:
            cert_path(bad)
        except ValueError:
            pass
        else:
            raise AssertionError("unsafe path accepted: " + repr(bad))
    assert bounded(b"a\n\x00") == r"a\n\x00"
    assert "TRUNCATED 2 BYTES" in bounded(b"ABCD", 2)
    assert INODE_LINE.search(b"Inode: 1506   Type: symlink") is not None
    print("[M223 SYMLINK DIAGNOSTIC SELFTEST PASS] 4 long CA targets and bounded paths")


def diagnose(disk: Path, output: Path) -> int:
    disk = disk.resolve(strict=True)
    if not disk.is_file() or output.is_symlink() or output.resolve() == disk:
        raise ValueError("input disk must be a regular file, output a distinct non-symlink")
    if not output.parent.is_dir():
        raise ValueError("diagnostic output directory must already exist")
    entries: list[str] = ["[M223 EXT4 SYMLINK DIAGNOSTIC] READ ONLY; not an fsck waiver"]
    failed = False

    try:
        m191, mib = read_m191(disk)
        entries.append(f"disk={disk} bytes={disk.stat().st_size} MBR={mib}MiB valid")
        with tempfile.TemporaryDirectory(prefix="m223-ca-symlink-") as temp:
            part = Path(temp) / "root.ext4"
            m191.extract_partition(disk, part)
            entries.append(f"extracted-partition-bytes={part.stat().st_size}")
            observations = [(name, predict_hash_target(cert)) for name, cert in HASH_LINKS]
            observations += [(name, "CONTROL") for name in CONTROLS]
            for name, predicted_target in observations:
                path = cert_path(name)
                entries.append(f"\n[path] {path} expected-relative-target={predicted_target}")
                stat_rc, stat_out, stat_err = invoke(part, "stat " + path)
                entries.append(f"[stat] rc={stat_rc} stdout={bounded(stat_out)} stderr={bounded(stat_err)}")
                found = INODE_LINE.search(stat_out)
                if stat_rc or found is None or DEBUGFS_ERROR.search(stat_out + stat_err):
                    failed = True
                    entries.append("[ERROR] cannot establish inode from debugfs stat")
                    # Other commands still run, preserving evidence even for
                    # a dangling/damaged directory entry.
                inode = int(found.group(1)) if found else None
                commands = ["blocks " + path, "cat " + path]
                if inode is not None:
                    commands.append("ncheck " + str(inode))
                for command in commands:
                    rc, stdout, stderr = invoke(part, command)
                    label = command.partition(" ")[0]
                    if label == "cat":
                        entries.append(f"[target-cat] rc={rc} bytes={len(stdout)} "
                                       f"content={bounded(stdout, MAX_TARGET_BYTES)} "
                                       f"stderr={bounded(stderr)}")
                    else:
                        entries.append(f"[{label}] rc={rc} stdout={bounded(stdout)} "
                                       f"stderr={bounded(stderr)}")
                    if rc or DEBUGFS_ERROR.search(stdout + stderr):
                        failed = True
                        entries.append(f"[ERROR] debugfs {label} command failed or reported an error")
    except Exception as exc:
        failed = True
        entries.append(f"[ERROR] diagnostic interrupted: {type(exc).__name__}: {exc}")

    entries.append("[M223 EXT4 SYMLINK DIAGNOSTIC END] " +
                   ("FAILED / INCOMPLETE" if failed else "CAPTURED (fsck status unchanged)"))
    report = "\n".join(entries) + "\n"
    if len(report.encode("utf-8")) > MAX_LOG_BYTES:
        report = report.encode("utf-8")[:MAX_LOG_BYTES].decode("utf-8", "ignore") + \
            "\n[TRUNCATED DIAGNOSTIC LOG; CAPTURE INCOMPLETE]\n"
        failed = True
    output.write_text(report, encoding="utf-8")
    print(report, end="")
    return 1 if failed else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    elif len(sys.argv) == 3:
        sys.exit(diagnose(Path(sys.argv[1]), Path(sys.argv[2])))
    else:
        raise SystemExit("usage: m223-ext4-symlink-diagnostic.py DISK OUT_LOG | --self-test")
