"""Linux sandbox tests. Android commands and reboots are fully mocked."""
import hashlib, os, pathlib, shutil, subprocess, tempfile

source = pathlib.Path(__file__).resolve().parent
base = pathlib.Path(tempfile.mkdtemp(prefix='tb371fc-bootstrap-test-'))
passed = []

def fixture():
    root = pathlib.Path(tempfile.mkdtemp(dir=base))
    adb = root/'data/adb'; vendor=root/'vendor/etc/tb371fc-enhanced'
    vendor.mkdir(parents=True); (root/'bin').mkdir()
    tag='123456789abc'
    s=adb/('tb371fc_rom/20261101-'+tag); w=s/'payload'; w.mkdir(parents=True)
    (vendor/'payload.sha256').write_text(tag+'0'*52+'  payload.zip\n')
    for name in ['setup.sh','seed.sh']:
        text=(source/name).read_text().replace('/data/adb',str(adb)).replace('/vendor/etc/tb371fc-enhanced',str(vendor))
        text=text.replace('df -k /data', 'df -k '+str(root/'data'))
        text=text.replace('if [ ! /proc/$$/exe -ef "$BB" ]; then exec "$BB" sh "$0" "$@"; fi','true # interpreter re-exec separately tested on device')
        (root/name).write_text(text)
    def executable(path, text):
        path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text);path.chmod(0o755)
    executable(root/'bin/getprop','#!/bin/sh\n[ "$1" != sys.boot_completed ] || echo 1\nexit 0\n')
    # Kill only this sandbox script's parent on mocked reboot: preserve durable phase.
    executable(root/'bin/setprop',f'''#!/usr/bin/env python3
import os,sys,signal
with open({str(root/'properties.log')!r},'a') as f:f.write(' '.join(sys.argv[1:])+'\\n')
if sys.argv[1:]==['sys.powerctl','reboot']:os.kill(os.getppid(),signal.SIGKILL)
''')
    executable(root/'bin/sync','#!/bin/sh\nexit 0\n')
    executable(adb/'ksu/bin/busybox','#!/bin/sh\na=$1; shift; exec "$a" "$@"\n')
    executable(w/'bin/safe-mode','#!/bin/sh\nexit 0\n')
    executable(root/'bin/uname','#!/bin/sh\necho 4.19.325-perf++\n')
    props=b'id=meta-overlayfs\nversionCode=13100\n'
    (w/'meta-prop.sha256').write_text(hashlib.sha256(props).hexdigest()+'\n')
    (root/'meta.prop').write_bytes(props)
    (w/'module-ids.txt').write_text('tb371fc_one\ntb371fc_two\n')
    (w/'effective-files.txt').write_text('')
    (w/'executable-files.txt').write_text('')
    (s/'payload.ok').touch()
    (adb/'modules_update').mkdir()
    executable(adb/'ksud',f'''#!/bin/sh
set -eu
case "$1" in module) ;; *) exit 53;; esac
id=${{3##*/}}; id=${{id%.zip}}
[ ! -e {root}/fail-$id ] || exit 52
mkdir -p {adb}/modules_update/$id {adb}/modules/$id
if [ "$id" = meta-overlayfs-1.3.1 ]; then
 id=meta-overlayfs
 mkdir -p {adb}/modules_update/$id {adb}/modules/$id
 cp {root}/meta.prop {adb}/modules_update/$id/module.prop
 : > {adb}/modules_update/$id/metainstall.sh
else
 echo "id=$id" > {adb}/modules_update/$id/module.prop
fi
cp {adb}/modules_update/$id/module.prop {adb}/modules/$id/module.prop
: > {adb}/modules/$id/update
''')
    (w/'ksud.sha256').write_text(hashlib.sha256((adb/'ksud').read_bytes()).hexdigest()+'\n')
    env=dict(os.environ,PATH=str(root/'bin')+':'+os.environ['PATH'])
    return root,adb,vendor,s,w,env,props

def run(x, script='setup.sh'):
    root,adb,v,s,w,env,props=x
    return subprocess.run(['/bin/sh',str(root/script)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20).returncode

def activate_meta(x):
    root,adb,v,s,w,env,props=x
    m=adb/'modules/meta-overlayfs';m.mkdir(parents=True,exist_ok=True)
    (m/'module.prop').write_bytes(props);(m/'metainstall.sh').touch();(m/'modules.img').write_bytes(b'OLD_IMAGE')
    (m/'update').unlink(missing_ok=True)
    shutil.rmtree(adb/'modules_update');(adb/'modules_update').mkdir()

x=fixture(); assert run(x)==-9
root,adb,v,s,w,env,props=x
assert (s/'phase').read_text().strip()=='meta_staged'
assert not (adb/'modules_update/tb371fc_one').exists()
passed.append('metamodule stage reboots without installing regular modules')

activate_meta(x)
for n in ['zz_disk_guard.sh','zz_powermodule_stub.sh','zz_user_tweaks.sh']:
 p=w/'runtime/service.d'/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#!/system/bin/sh\n')
for n in ['service.jar','probe.jar']:
 p=w/'runtime/tb371fc_powermodule'/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'fixture')
assert run(x)==-9
assert (s/'phase').read_text().strip()=='modules_staged'
assert (s/'backup.ok').exists()
passed.append('regular modules and runtime staged only after stable metamodule')
for ident in ['tb371fc_one','tb371fc_two']:
 dst=adb/'modules'/ident
 (dst/'update').unlink()
 (adb/'modules_update'/ident/'module.prop').replace(dst/'module.prop')
shutil.rmtree(adb/'modules_update');(adb/'modules_update').mkdir()
assert run(x)==0 and (s/'phase').read_text().strip()=='complete'
before=(root/'properties.log').read_text().count('sys.powerctl reboot')
assert run(x)==0
assert (root/'properties.log').read_text().count('sys.powerctl reboot')==before==2
passed.append('final verification marks ready and later boots do not reinstall or reboot')
(s/'phase').write_text('modules_staged\n');(s/'attempts').write_text('2\n')
(w/'executable-files.txt').write_text('tb371fc_one/native-tool\n')
(adb/'modules/tb371fc_one/native-tool').write_bytes(b'fixture')
assert run(x)!=0 and (s/'phase').read_text().strip()=='rollback_pending'
passed.append('lost executable permissions prevent a false ready status')

y=fixture(); root,adb,v,s,w,env,props=y;activate_meta(y)
(s/'phase').write_text('meta_staged\n');(root/'fail-tb371fc_two').touch()
assert run(y)!=0
assert (s/'phase').read_text().strip()=='rollback_pending'
assert 'sys.powerctl reboot' not in (root/'properties.log').read_text()
# Interrupted phase is also restored by seed before any staged module load.
(s/'phase').write_text('installing_modules\n')
content=b'fixture';(v/'payload.zip').write_bytes(content)
h=hashlib.sha256(content).hexdigest()
# Use original tag path in this fixture by rewriting cut solely in the fixture.
seed=(root/'seed.sh').read_text().replace('TAG=$(cut -c1-12 "$P/payload.sha256")','TAG=123456789abc')
(root/'seed.sh').write_text(seed)
(v/'payload.sha256').write_text(h+'  payload.zip\n')
assert run(y,'seed.sh')==0
assert (s/'phase').read_text().strip()=='failed_rolled_back'
assert not (adb/'modules/tb371fc_one').exists()
assert not (adb/'modules_update/tb371fc_one').exists()
assert (adb/'modules/meta-overlayfs/modules.img').read_bytes()==b'OLD_IMAGE'
passed.append('failed/interrupted module install restores image and owned files before module loading')

z=fixture();root,adb,v,s,w,env,props=z
(w/'bin/safe-mode').write_text('#!/bin/sh\nexit 10\n')
assert run(z)==0
assert not (s/'attempts').exists() and not (s/'phase').exists()
passed.append('kernel safe mode suppresses installation and automatic reboot')

z=fixture();root,adb,v,s,w,env,props=z
(s/'attempts').write_text('3\n')
assert run(z)!=0
assert (s/'phase').read_text().strip()=='failed'
passed.append('attempt limit stops repeated boot work')

z=fixture();root,adb,v,s,w,env,props=z
(v/'payload.zip').write_bytes(b'corrupt')
assert run(z,'seed.sh')!=0
assert 'payload_checksum' in (root/'properties.log').read_text()
passed.append('payload checksum failure stops before privileged installation')

for p in passed:print('PASS',p)
print('Sandbox:',base)
