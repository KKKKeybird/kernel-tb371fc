#!/usr/bin/env python3
"""Package the module with executable script permissions and no host data."""
import argparse
from pathlib import Path
import stat
import zipfile

root = Path(__file__).resolve().parent
ap = argparse.ArgumentParser()
ap.add_argument('--output', required=True, type=Path)
args = ap.parse_args()
names = ['module.prop', 'service.sh', 'customize.sh', 'skip_mount',
         'payload/RtMiCloudSDK.apk', 'README.md', 'CHANGELOG.md']
args.output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for name in names:
        path = root / name
        info = zipfile.ZipInfo.from_file(path, name)
        info.create_system = 3
        info.external_attr = (stat.S_IFREG | (0o755 if name == 'service.sh' else 0o644)) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(info, path.read_bytes())
print(f'{args.output}: {args.output.stat().st_size} bytes')
