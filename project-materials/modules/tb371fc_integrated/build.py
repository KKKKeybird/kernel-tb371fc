#!/usr/bin/env python3
"""Assemble the integrated module using binary assets from the verified 1.4.1 release."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import stat
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent
REFERENCE_NAME = 'TB371FC-Integrated-v1.4.1.zip'
REFERENCE_SHA256 = '1eae8b90f947ac4c7c1fb821e15b7e3297751eb24ac9d9446d2e3ec15dad8880'
REFERENCE_URL = 'https://github.com/KKKKeybird/kernel-tb371fc/releases/download/module-integrated-v1.4.1/TB371FC-Integrated-v1.4.1.zip'
REFERENCE_MEMBERS = (
    'components/tb371fc_account_compat/payload/RtMiCloudSDK.apk',
    'components/tb371fc_split_compat/payload/MiuiSystemUI.apk',
    'components/tb371fc_split_compat/payload/MiuiSystemUI.patch.json',
    'components/tb371fc_pps_temp_test/hvdcp_opti.temp-chain-v3-test',
    'components/tb371fc_stock_charge/payload/classes3.dex',
)

parser = argparse.ArgumentParser()
parser.add_argument("--artifact-dir", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
args.artifact_dir.mkdir(parents=True, exist_ok=True)

reference = args.artifact_dir / REFERENCE_NAME
if not reference.exists():
    print(f"download {REFERENCE_NAME}")
    temporary_reference = reference.with_suffix(".download")
    urllib.request.urlretrieve(REFERENCE_URL, temporary_reference)
    temporary_reference.replace(reference)
actual = hashlib.sha256(reference.read_bytes()).hexdigest()
if actual != REFERENCE_SHA256:
    raise SystemExit(f"{REFERENCE_NAME}: expected {REFERENCE_SHA256}, got {actual}")

with tempfile.TemporaryDirectory(prefix="tb371fc-integrated-") as temporary:
    module = Path(temporary) / "module"
    shutil.copytree(ROOT, module, ignore=shutil.ignore_patterns(
        "build.py", "ACCEPTANCE.md", "__pycache__", "*.pyc"
    ))
    with zipfile.ZipFile(reference) as archive:
        for member in REFERENCE_MEMBERS:
            destination = module / member
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(archive.read(member))

    files = sorted(path for path in module.rglob("*") if path.is_file())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in files:
            relative = path.relative_to(module).as_posix()
            if "payload/boot.img" in relative:
                raise SystemExit("kernel boot image must never enter the core package")
            info = zipfile.ZipInfo(relative, (2026, 11, 14, 0, 0, 0))
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            executable = (
                path.suffix in {".sh", ".py"}
                or relative == "components/tb371fc_pps_temp_test/hvdcp_opti.temp-chain-v3-test"
            )
            info.external_attr = (stat.S_IFREG | (0o755 if executable else 0o644)) << 16
            archive.writestr(info, path.read_bytes())

print(json.dumps({
    "file": str(args.output.resolve()),
    "bytes": args.output.stat().st_size,
    "sha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
}, ensure_ascii=False))
