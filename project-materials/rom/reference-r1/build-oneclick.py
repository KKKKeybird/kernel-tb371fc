"""Build a full flash ZIP from the verified base and private integrated super.

Streams zstd straight into ZIP to avoid a redundant 5GB intermediate.
Does not access ADB, fastboot, block devices, or the user's source checkout.
"""
from pathlib import Path
import hashlib,json,os,subprocess,zipfile

r=Path(__file__).resolve().parent
workspace=r.parents[1]
base=workspace/'work/rom-release-20261101/base/HyperOS3.0.4.0.VMHCNXM_A15_TB371FC[f842f0].zip'
boot=workspace/'work/deepseek-fixes-20261101/boot-reviewed-24.img'
pkg=r/'package';raw=r/'super-working.img'
outdir=workspace/'outputs/ROM-release-20261101';outdir.mkdir(parents=True,exist_ok=True)
out=outdir/'TB371FC-HyperOS3-20261101-oneclick-r1-CANDIDATE.zip'
part=out.with_suffix('.zip.part')
assert not out.exists() and not part.exists(),'Inspect an existing build before restarting'

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(16*1024*1024),b''):h.update(b)
    return h.hexdigest()

assert digest(base)=='ed3f141bb1a0096909c299d6d5410f2251c8d4668e19154311583f7944f842f0'
assert digest(boot)=='a0d3a11eb761dbe63d0175dd4ea597a3d26881c8a3ef264ca2b3fe42d886a5a9'
assert raw.stat().st_size==12884901888
audit=json.loads((r/'partition-audit.json').read_text())
assert {x['name'] for x in audit if x['original_sha256']!=x['candidate_sha256']}=={'vendor_a','system_a'}
assert (pkg/'docs/partition-audit.json').read_bytes()==(r/'partition-audit.json').read_bytes()
raw_sha=digest(raw)
print('Base, boot and integrated raw super hashes checked',flush=True)
records=[]

def info(name,mode=0o644):
    z=zipfile.ZipInfo(name,(2026,11,1,0,0,0));z.create_system=3
    z.external_attr=(0o100000|mode)<<16
    return z

def put_bytes(z,name,data,mode=0o644):
    zi=info(name,mode);zi.compress_type=zipfile.ZIP_DEFLATED
    z.writestr(zi,data,compresslevel=1)
    records.append({'path':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})

def put_file(z,name,p):
    zi=info(name,p.stat().st_mode&0o777)
    zi.compress_type=zipfile.ZIP_STORED if p.suffix in {'.gz','.zip'} else zipfile.ZIP_DEFLATED
    h=hashlib.sha256();n=0
    with p.open('rb') as f,z.open(zi,'w',force_zip64=True) as dst:
        for b in iter(lambda:f.read(16*1024*1024),b''):dst.write(b);h.update(b);n+=len(b)
    records.append({'path':name,'bytes':n,'sha256':h.hexdigest()})

with zipfile.ZipFile(part,'w',allowZip64=True) as z,zipfile.ZipFile(base) as src:
    for zi in src.infolist():
        n=zi.filename
        if zi.is_dir() or n in {'images/super.img.zst','images/boot.img','META-INF/com/google/android/update-binary','点我一键线刷.bat'}:continue
        put_bytes(z,n,src.read(zi),0o755 if n.startswith('bin/android/') else 0o644)
    put_file(z,'images/boot.img',boot)
    reject=b'''#!/sbin/sh
case "$2" in ''|*[!0-9]*) exit 1;; esac
echo 'ui_print This candidate supports FASTBOOT installation only. Use the host one-click launcher.' > /proc/self/fd/$2
echo 'ui_print' > /proc/self/fd/$2
exit 1
'''
    put_bytes(z,'META-INF/com/google/android/update-binary',reject,0o755)
    put_bytes(z,'docs/original-installers/windows-original.txt',src.read('点我一键线刷.bat'))
    put_bytes(z,'docs/original-installers/recovery-original.txt',src.read('META-INF/com/google/android/update-binary'))
    for f in sorted(pkg.rglob('*')):
        if f.is_file() and '__pycache__' not in f.parts:put_file(z,str(f.relative_to(pkg)),f)
    zi=info('images/super.img.zst');zi.compress_type=zipfile.ZIP_STORED
    h=hashlib.sha256();n=0
    with (r/'zstd-build.log').open('wb') as log,z.open(zi,'w',force_zip64=True) as dst:
        proc=subprocess.Popen(['zstd','-T0','-3','-c',str(raw)],stdout=subprocess.PIPE,stderr=log)
        try:
            for b in iter(lambda:proc.stdout.read(16*1024*1024),b''):
                dst.write(b);h.update(b);n+=len(b)
        finally:proc.stdout.close()
        assert proc.wait()==0,'zstd failed; keep partial build for inspection'
    records.append({'path':'images/super.img.zst','bytes':n,'sha256':h.hexdigest()})
    manifest={'release':'20261101-oneclick-r1-CANDIDATE','kernel_number':24,
              'base_rom_sha256':'ed3f141bb1a0096909c299d6d5410f2251c8d4668e19154311583f7944f842f0',
              'upgrade_base_incremental':'OS3.0.4.0.VMHCNXM',
              'root_ksud_sha256':(r/'payload-v2/ksud.sha256').read_text().strip(),
              'super_raw':{'bytes':raw.stat().st_size,'sha256':raw_sha},'files':records}
    data=(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n').encode()
    zi=info('flash-manifest.json');zi.compress_type=zipfile.ZIP_DEFLATED;z.writestr(zi,data)
    (r/'flash-manifest.json').write_bytes(data)
print('ZIP written; verifying all member CRCs',flush=True)
with zipfile.ZipFile(part) as z:
    assert z.testzip() is None
    assert len(z.namelist())==len(set(z.namelist()))
    assert not any(x.startswith(('internal/','payload-v2/','test-tools/')) for x in z.namelist())
print('Full ZIP CRC passed; hashing completed archive',flush=True)
sha=digest(part);part.replace(out)
out.with_suffix('.zip.sha256').write_text(sha+'  '+out.name+'\n')
(r/'build-result.json').write_text(json.dumps({'path':str(out),'bytes':out.stat().st_size,'sha256':sha,'super_raw_sha256':raw_sha},indent=2)+'\n')
print(json.dumps({'path':str(out),'bytes':out.stat().st_size,'sha256':sha}),flush=True)
