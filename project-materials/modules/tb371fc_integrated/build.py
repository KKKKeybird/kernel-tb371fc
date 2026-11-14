#!/usr/bin/env python3
"""Assemble the small integrated module from published component releases."""
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
ARTIFACTS = {
    "account": {
        "file": "TB371FC-XiaomiAccount-Compat-v1.1.zip",
        "sha256": "8f9e7a3d3fee05173c7c2d482cb7dbe67345dd38b1860a46829d504773f7dc2e",
        "url": "https://github.com/KKKKeybird/kernel-tb371fc/releases/download/module-account-compat-v1.1/TB371FC-XiaomiAccount-Compat-v1.1.zip",
        "members": ["payload/RtMiCloudSDK.apk"],
        "destination": "components/tb371fc_account_compat",
    },
    "split": {
        "file": "TB371FC-SplitCompat-v1.1.zip",
        "sha256": "7b6a2339f45d265d96fac8d1cceb9c6c0e3b48d92e4fe173c1b740f4f341e835",
        "url": "https://github.com/KKKKeybird/kernel-tb371fc/releases/download/module-split-compat-v1.1/TB371FC-SplitCompat-v1.1.zip",
        "members": ["payload/MiuiSystemUI.apk", "payload/MiuiSystemUI.patch.json"],
        "destination": "components/tb371fc_split_compat",
    },
    "pps": {
        "file": "TB371FC-PPS-Temp-Test-v0.3.zip",
        "sha256": "433662afa62763d1a35aa9ef3714ad7f127cb8ded83a268f2a57b2e1643812e5",
        "url": "https://github.com/KKKKeybird/kernel-tb371fc/releases/download/module-pps-temp-test-v0.3/TB371FC-PPS-Temp-Test-v0.3.zip",
        "members": ["hvdcp_opti.temp-chain-v3-test"],
        "destination": "components/tb371fc_pps_temp_test",
    },
    "stock": {
        "file": "TB371FC-StockCharge-v1.1-test3.zip",
        "sha256": "4f6f03a80527dcdf285069252438921bacaefb95221bb791311dd517dcf83352",
        "url": "https://github.com/KKKKeybird/kernel-tb371fc/releases/download/module-stock-charge-v1.1-test3/TB371FC-StockCharge-v1.1-test3.zip",
        "members": ["payload/classes3.dex"],
        "destination": "components/tb371fc_stock_charge",
    },
}

parser = argparse.ArgumentParser()
parser.add_argument("--artifact-dir", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
args.artifact_dir.mkdir(parents=True, exist_ok=True)

for item in ARTIFACTS.values():
    artifact = args.artifact_dir / item["file"]
    if not artifact.exists():
        print(f"download {item['file']}")
        urllib.request.urlretrieve(item["url"], artifact)
    actual = hashlib.sha256(artifact.read_bytes()).hexdigest()
    if actual != item["sha256"]:
        raise SystemExit(f"{item['file']}: expected {item['sha256']}, got {actual}")

with tempfile.TemporaryDirectory(prefix="tb371fc-integrated-") as temporary:
    module = Path(temporary) / "module"
    shutil.copytree(ROOT, module, ignore=shutil.ignore_patterns(
        "build.py", "ACCEPTANCE.md", "__pycache__", "*.pyc"
    ))
    for item in ARTIFACTS.values():
        with zipfile.ZipFile(args.artifact_dir / item["file"]) as archive:
            for member in item["members"]:
                destination = module / item["destination"] / member
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
