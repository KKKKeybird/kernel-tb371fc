#!/usr/bin/env python3
from pathlib import Path
import json
import stat
import zipfile


ROOT = Path(__file__).resolve().parent
OUT = ROOT.parent.parent.parent.parent / "outputs" / "TB371FC-PenLockFix-v1.0.zip"
OUT.parent.mkdir(parents=True, exist_ok=True)

FILES = [
    "module.prop",
    "customize.sh",
    "service.sh",
    "action.sh",
    "uninstall.sh",
    "README.md",
    "CHANGELOG.md",
    "system/etc/tb371fc/core/pen_bridge/supervisor.sh",
]

missing = [name for name in FILES if not (ROOT / name).is_file()]
if missing:
    raise SystemExit(f"missing module files: {missing}")

for name in ("service.sh", "action.sh", "uninstall.sh"):
    (ROOT / name).chmod(0o755)
(ROOT / "system/etc/tb371fc/core/pen_bridge/supervisor.sh").chmod(0o755)

with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for name in FILES:
        source = ROOT / name
        info = zipfile.ZipInfo.from_file(source, name)
        info.create_system = 3
        mode = 0o755 if name.endswith(".sh") else 0o644
        info.external_attr = (stat.S_IFREG | mode) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(info, source.read_bytes())

with zipfile.ZipFile(OUT) as archive:
    if archive.namelist() != FILES:
        raise SystemExit("ZIP member order/content mismatch")

print(json.dumps({"file": str(OUT), "size": OUT.stat().st_size}, ensure_ascii=False))
