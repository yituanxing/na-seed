#!/usr/bin/env python3
"""Verify a pinned original Debian dash using M245's signed APT index.

This is a host-only acquisition and Linux-control stage, never guest PASS.
"""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess


FETCHER = Path("scripts/m245-fetch-debian-dynamic.py")
spec = importlib.util.spec_from_file_location("m245_signed_fetcher", FETCHER)
assert spec and spec.loader
m245 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m245)

PIN = Path("scripts/m247-debian-dash-version.tsv")
DASH_PATH = Path("usr/bin/dash")


def pinned_version(path: Path) -> str:
    lines = [line.split("\t") for line in path.read_text(encoding="utf-8").splitlines()
             if line.strip() and not line.startswith("#")]
    if len(lines) != 1 or len(lines[0]) != 3 or lines[0][:2] != ["amd64", "dash"]:
        raise ValueError("missing/duplicate exact Debian dash pin")
    version = lines[0][2]
    if not re.fullmatch(r"[0-9][A-Za-z0-9.+:~-]*", version):
        raise ValueError("malformed Debian dash version pin")
    return version


def verify_dash(path: Path) -> str:
    if not path.is_file() or path.is_symlink():
        raise ValueError("missing original Debian dash regular ELF")
    elf = m245.run("readelf", "-h", str(path), capture=True)
    if "ELF64" not in elf or "Advanced Micro Devices X86-64" not in elf:
        raise ValueError("dash ELF ISA/class mismatch")
    if not re.search(r"Type:\s+DYN ", elf):
        raise ValueError("original dash must be a dynamic PIE")
    if m245.elf_interpreter(path) != m245.INTERP:
        raise ValueError("original dash PT_INTERP unexpected")
    dynamic = m245.run("readelf", "-d", str(path), capture=True)
    if re.findall(r"\(NEEDED\).*\[([^\]]+)\]", dynamic) != ["libc.so.6"]:
        raise ValueError("unreviewed dynamic dash dependency closure")
    return m245.sha256_file(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=["amd64"], required=True)
    parser.add_argument("--m245-out", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    m245_out = args.m245_out.resolve()
    manifest_path = m245_out / "manifest.json"
    signed = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (signed.get("status") != "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS" or
        signed.get("architecture") != args.arch or
        set(signed.get("debs", {})) != {"coreutils", "libc6"}):
        raise ValueError("M245 signed upstream manifest absent or incompatible")
    index = list((m245_out / "apt/lists").glob("*_dists_trixie_InRelease"))
    if (len(index) != 1 or
        m245.sha256_file(index[0]) != signed.get("inrelease_sha256")):
        raise ValueError("signed Debian InRelease mismatch")

    version = pinned_version(PIN)
    options = m245.apt_options(m245_out / "apt", arch=args.arch)
    listing = m245.run("apt-cache", *options, "show", "dash", capture=True)
    record = m245.parse_apt_show(listing, package="dash",
                                 version=version, arch=args.arch)
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    deb = output / "debs" / Path(record["Filename"]).name
    m245.fetch_verified_deb(record, deb)
    dash_root = output / "sysroot"
    if dash_root.exists():
        prior = output / "manifest.json"
        if not prior.is_file() or json.loads(prior.read_text()).get("dash_deb_sha256") != record["SHA256"]:
            raise ValueError("existing original dash extraction has incompatible provenance")
    else:
        stage = output / "stage-dash"
        if stage.exists():
            raise ValueError("untrusted stale dash extraction stage")
        stage.mkdir()
        try:
            m245.run("dpkg-deb", "-x", str(deb), str(stage))
            verify_dash(stage / DASH_PATH)
            stage.rename(dash_root)
        finally:
            if stage.exists():
                import shutil
                shutil.rmtree(stage)
    dash = dash_root / DASH_PATH
    binary_sha = verify_dash(dash)
    libdir = m245_out / "sysroot/usr/lib/x86_64-linux-gnu"
    loader = libdir / "ld-linux-x86-64.so.2"
    result = subprocess.run([str(loader), "--library-path", str(libdir),
                             str(dash), "-c", "printf M247-DASH-HOST-ONLY"],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
    if result.returncode or result.stdout != b"M247-DASH-HOST-ONLY":
        raise ValueError(f"host dash control failed rc={result.returncode}, stderr={result.stderr[:300]!r}")
    attestation = {
        "status": "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
        "distribution": "Debian 13 trixie",
        "architecture": args.arch,
        "package": "dash",
        "version": version,
        "dash_deb_sha256": record["SHA256"],
        "dash_deb_size": int(record["Size"]),
        "dash_deb_pool": record["Filename"],
        "dash_elf_sha256": binary_sha,
        "trusted_m245_inrelease_sha256": signed["inrelease_sha256"],
        "program_interpreter": m245.INTERP,
        "dt_needed": ["libc.so.6"],
        "host_control": "M247-DASH-HOST-ONLY",
    }
    (output / "manifest.json").write_text(
        json.dumps(attestation, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    print("[M247 HOST ONLY] official signed Debian dash verified, not guest PASS", flush=True)


if __name__ == "__main__":
    main()
