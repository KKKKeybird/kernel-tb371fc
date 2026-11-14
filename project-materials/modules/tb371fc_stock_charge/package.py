#!/usr/bin/env python3
from pathlib import Path
import argparse,hashlib,json,stat,zipfile
base=Path(__file__).resolve().parent
root=base/'module' if (base/'module').is_dir() else base
ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();a.output.parent.mkdir(parents=True,exist_ok=True)
files=('module.prop','customize.sh','post-fs-data.sh','service.sh','supervisor.sh','uninstall.sh','action.sh','charge-bridge.jar','README.md','CHANGELOG.md')
with zipfile.ZipFile(a.output,'w',zipfile.ZIP_DEFLATED) as z:
 for name in files:
  i=zipfile.ZipInfo(name,(2026,11,14,0,0,0));i.create_system=3;i.compress_type=zipfile.ZIP_DEFLATED
  i.external_attr=(stat.S_IFREG|(0o755 if name.endswith('.sh') else 0o644))<<16
  z.writestr(i,(root/name).read_bytes())
with zipfile.ZipFile(a.output) as z:assert z.testzip() is None and tuple(z.namelist())==files
print(json.dumps(dict(file=str(a.output.resolve()),bytes=a.output.stat().st_size,sha256=hashlib.sha256(a.output.read_bytes()).hexdigest())))
