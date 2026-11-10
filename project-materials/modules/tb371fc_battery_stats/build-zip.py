#!/usr/bin/env python3
"""Build a small, deterministic KernelSU module with no rebuildable artifacts."""
from pathlib import Path
import argparse
import hashlib
import json
import stat
import zipfile

ROOT = Path(__file__).resolve().parent
FILES = ('module.prop', 'customize.sh', 'service.sh', 'daemon.sh', 'gauge.sh',
         'uninstall.sh', 'action.sh', 'README.md', 'CHANGELOG.md')
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for name in FILES:
        payload = (ROOT/name).read_bytes()
        if b'\r' in payload:
            raise SystemExit(f'CRLF not allowed: {name}')
        info = zipfile.ZipInfo(name, (2026, 11, 10, 0, 0, 0))
        info.create_system = 3
        info.external_attr = (stat.S_IFREG | (0o755 if name.endswith('.sh') else 0o644)) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, payload, compresslevel=9)
with zipfile.ZipFile(args.output) as z:
    assert tuple(z.namelist()) == FILES and z.testzip() is None
print(json.dumps({'file': str(args.output.resolve()), 'size': args.output.stat().st_size,
                  'sha256': hashlib.sha256(args.output.read_bytes()).hexdigest()}))
