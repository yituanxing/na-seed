#!/usr/bin/env python3
"""Host-only negatives for M196's isolated real Alpine DNS configuration."""

import importlib.util
from pathlib import Path
import shutil
import tempfile


path = Path(__file__).with_name("m196-alpine-stage.py")
spec = importlib.util.spec_from_file_location("naseed_m196_stage", path)
assert spec and spec.loader
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


with tempfile.TemporaryDirectory(prefix="naseed-m196-host-") as temp:
    base = Path(temp)
    signed = base / "signed"
    (signed / "etc/apk/keys").mkdir(parents=True)
    (signed / "etc/ssl").mkdir(parents=True)
    (signed / "etc/apk/keys/alpine.pub").write_text("real key fixture path\n")
    official = ("https://dl-cdn.alpinelinux.org/alpine/v3.23/main\n"
                "https://dl-cdn.alpinelinux.org/alpine/v3.23/community\n")
    (signed / "etc/apk/repositories").write_text(official)
    (signed / "etc/ssl/cert.pem").write_bytes(b"C" * 2048)
    overlay = base / "overlay"
    shutil.copytree(signed, overlay)
    stage.configure(signed, overlay)
    assert not (signed / "etc/resolv.conf").exists()
    assert (overlay / "etc/resolv.conf").read_text().startswith("nameserver 10.0.2.3\n")
    assert (overlay / "etc/apk/repositories").read_text() == official
    assert (overlay / "etc/ssl/cert.pem").read_bytes() == b"C" * 2048
    (overlay / "etc/apk/repositories").write_text("http://10.0.2.2:15417\n")
    try:
        stage.configure(signed, overlay)
    except ValueError as error:
        assert "repos replaced" in str(error)
    else:
        raise AssertionError("host-local HTTP substituted for official HTTPS")
    (overlay / "etc/apk/repositories").write_text(official)
    (overlay / "etc/ssl/cert.pem").unlink()
    try:
        stage.configure(signed, overlay)
    except ValueError as error:
        assert "CA bundle missing" in str(error)
    else:
        raise AssertionError("missing CA was accepted")

print("[M196 PUBLIC HOST CONTRACT PASS] scoped resolver, unchanged signed "
      "APK/CA, host-local HTTP and absent trust rejected")
