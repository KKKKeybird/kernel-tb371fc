#!/usr/bin/env python3
from pathlib import Path
import json
import os
import stat
import zipfile

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]
BOOT = Path(os.environ.get("TB371FC_BOOT_IMAGE", "/private/tmp/tb371fc-upstream-local-20261113/flash/boot-r8-v25-candidate.img"))
OUT = REPO / "outputs" / "TB371FC-KernelUpdater-v2.2-hyperos25.zip"
EXPECTED = "a9fb2b73e43e8e8bb1b7a8456bf772e6729b68c110ed57bf7f2c1d1cf417b981"

if not BOOT.is_file() or BOOT.stat().st_size != 100663296:
    raise SystemExit(f"invalid boot payload: {BOOT}")
import hashlib
if hashlib.sha256(BOOT.read_bytes()).hexdigest() != EXPECTED:
    raise SystemExit("boot payload hash mismatch")

files = ["module.prop", "customize.sh", "service.sh", "action.sh", "uninstall.sh", "README.md", "CHANGELOG.md"]
OUT.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for name in files:
        source = ROOT / name
        info = zipfile.ZipInfo.from_file(source, name)
        info.create_system = 3
        mode = 0o755 if name.endswith(".sh") else 0o644
        info.external_attr = (stat.S_IFREG | mode) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(info, source.read_bytes())
    info = zipfile.ZipInfo("payload/boot.img")
    info.create_system = 3
    info.external_attr = (stat.S_IFREG | 0o644) << 16
    info.compress_type = zipfile.ZIP_STORED
    archive.writestr(info, BOOT.read_bytes())

print(json.dumps({"file": str(OUT), "size": OUT.stat().st_size}, ensure_ascii=False))
