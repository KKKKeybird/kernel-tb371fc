#!/usr/bin/env python3
"""Rebuild only the optional FindDevice isOpen() exception guard; no device writes."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import zipfile

ap = argparse.ArgumentParser()
ap.add_argument('original_apk', type=Path, help='Original R8 RtMiCloudSDK.apk, versionCode 20')
ap.add_argument('--output', required=True, type=Path)
args = ap.parse_args()
source = args.original_apk.resolve()
output = args.output.resolve()
if source == output:
    raise SystemExit('Keep the original APK; select a different output path')
with tempfile.TemporaryDirectory(prefix='tb371fc-cloud-sdk-') as directory:
    temporary = Path(directory)
    decoded = temporary / 'decoded'
    subprocess.run(['apktool', 'd', '-f', '-r', str(source), '-o', str(decoded)], check=True)
    matches = list(decoded.glob('smali*/miui/cloud/finddevice/FindDeviceStatusManager.smali'))
    if len(matches) != 1:
        raise SystemExit('Expected exactly one FindDeviceStatusManager class')
    path = matches[0]
    content = path.read_text()
    start = content.index('.method public isOpen()Z')
    end = content.index('.end method', start) + len('.end method')
    method = content[start:end]
    if method.count('getFindDeviceInfoInner()') != 1 or '.catch ' in method:
        raise SystemExit('Unexpected method implementation; refusing to patch')
    call = '    invoke-direct {p0}, Lmiui/cloud/finddevice/FindDeviceStatusManager;->getFindDeviceInfoInner()Lmiui/cloud/finddevice/FindDeviceInfo;'
    if call not in method or method.count('    return v0') != 1:
        raise SystemExit('Unexpected instructions; refusing to patch')
    patched = method.replace(call, '    :compat_try_start\n' + call, 1)
    patched = patched.replace('    return v0\n.end method', '''    :compat_try_end
    return v0

    .catch Ljava/lang/RuntimeException; {:compat_try_start .. :compat_try_end} :compat_unavailable

    :compat_unavailable
    move-exception v0
    const/4 v0, 0x0
    return v0
.end method''', 1)
    path.write_text(content[:start] + patched + content[end:])
    built = temporary / 'built.apk'
    subprocess.run(['apktool', 'b', '-f', str(decoded), '-o', str(built)], check=True)
    with zipfile.ZipFile(built) as archive:
        dex = archive.read('classes.dex')
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(source) as original, zipfile.ZipFile(output, 'w') as result:
        for info in original.infolist():
            result.writestr(info, dex if info.filename == 'classes.dex' else original.read(info.filename))
    with zipfile.ZipFile(source) as original, zipfile.ZipFile(output) as result:
        differences = [name for name in original.namelist() if original.read(name) != result.read(name)]
        if differences != ['classes.dex']:
            raise SystemExit(f'Unexpected changed members: {differences}')
print(f'{output}: only classes.dex changed. This APK is for late module binding, not APK installation.')
