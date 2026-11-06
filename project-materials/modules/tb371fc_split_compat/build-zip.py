#!/usr/bin/env python3
from pathlib import Path
import json
import stat
import zipfile


ROOT = Path(__file__).resolve().parent
OUT = ROOT.parent.parent.parent.parent / "outputs" / "TB371FC-SplitCompat-v1.1.zip"
OUT.parent.mkdir(parents=True, exist_ok=True)

FILES = [
    "module.prop",
    "customize.sh",
    "service.sh",
    "rollback.sh",
    "action.sh",
    "uninstall.sh",
    "skip_mount",
    "README.md",
    "CHANGELOG.md",
    "payload/MiuiSystemUI.apk",
    "payload/MiuiSystemUI.patch.json",
    "patch-systemui.py",
]

missing = [name for name in FILES if not (ROOT / name).is_file()]
if missing:
    raise SystemExit(f"missing module files: {missing}")

for name in (
    "service.sh",
    "rollback.sh",
    "action.sh",
    "uninstall.sh",
    "patch-systemui.py",
):
    (ROOT / name).chmod(0o755)

with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for name in FILES:
        source = ROOT / name
        info = zipfile.ZipInfo.from_file(source, name)
        info.create_system = 3
        mode = 0o755 if name in {
            "service.sh",
            "rollback.sh",
            "action.sh",
            "uninstall.sh",
            "patch-systemui.py",
        } else 0o644
        info.external_attr = (stat.S_IFREG | mode) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(info, source.read_bytes())

with zipfile.ZipFile(OUT) as archive:
    if archive.namelist() != FILES:
        raise SystemExit("ZIP member order/content mismatch")

print(json.dumps({"file": str(OUT), "size": OUT.stat().st_size}, ensure_ascii=False))
