#!/usr/bin/env python3
"""Read one DEX only; preserve every original prediction method and APK resource."""
import argparse,hashlib,json,os,subprocess as sp,tempfile,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent
ap=argparse.ArgumentParser();ap.add_argument('apk',type=Path,nargs='?');ap.add_argument('--base-dex',type=Path);ap.add_argument('--output-dex',type=Path,required=True)
ap.add_argument('--java-home',type=Path,default='/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home')
ap.add_argument('--r8-jar',type=Path,default='/private/tmp/android-r8/r8.jar');ap.add_argument('--deps',type=Path,default='/private/tmp/tb371fc-four-charge')
a=ap.parse_args();j=a.java_home
BASE='ab7fea0478fcc99b18b1c546efab0ea281f3e744d4ca35d67ba33ad48c26ba6e'
BASE_DEX='ca3219d74a94829904cd1adab6edcce7cb97f8db7b0c9f02261dccfb88e81537'
if a.base_dex:
 assert a.apk is None,'Pass either the original APK or --base-dex, not both'
 assert hashlib.sha256(a.base_dex.read_bytes()).hexdigest()==BASE_DEX,'Unrecognized R8 Security Center classes3.dex'
else:
 assert a.apk is not None,'Pass the original APK or --base-dex'
 assert hashlib.sha256(a.apk.read_bytes()).hexdigest()==BASE,'Unrecognized R8 Security Center'
def run(*args):sp.run([str(x) for x in args],check=True)
with tempfile.TemporaryDirectory(prefix='tb371fc-ui-') as t:
 t=Path(t);classes=t/'classes';dex=t/'dex';tools=t/'tools';patcher=t/'patcher'
 for d in (classes,dex,tools,patcher):d.mkdir()
 run(j/'bin/javac','--release','8','-Xlint:-options','-d',classes,ROOT/'ui-src/ChargeUi.java',ROOT/'ui-src/ApkPatch.java')
 run(j/'bin/java','-cp',a.r8_jar,'com.android.tools.r8.D8','--release','--min-api','35','--lib',j,'--output',dex,classes/'ChargeUi.class')
 cp=os.pathsep.join(str(p) for p in sorted(a.deps.glob('*.jar')))
 run(j/'bin/javac','-cp',cp,'-d',tools,ROOT/'ui-src/PatchChargeDex.java',ROOT/'ui-src/AuditChargeDex.java')
 if a.base_dex:
  (t/'original.dex').write_bytes(a.base_dex.read_bytes())
 else:
  with zipfile.ZipFile(a.apk) as z:(t/'original.dex').write_bytes(z.read('classes3.dex'))
 a.output_dex.parent.mkdir(parents=True,exist_ok=True)
 run(j/'bin/java','-Xmx1g','-cp',str(tools)+os.pathsep+cp,'PatchChargeDex',t/'original.dex',dex/'classes.dex',a.output_dex)
 run(j/'bin/java','-Xmx1g','-cp',str(tools)+os.pathsep+cp,'AuditChargeDex',t/'original.dex',a.output_dex)
 run(j/'bin/java','-cp',a.r8_jar,'com.android.tools.r8.D8','--release','--min-api','35','--lib',j,'--output',patcher,*sorted(classes.glob('ApkPatch*.class')))
 module=ROOT/'module' if (ROOT/'module').is_dir() else ROOT
 with zipfile.ZipFile(module/'apk-patcher.jar','w',zipfile.ZIP_DEFLATED) as z:
  i=zipfile.ZipInfo('classes.dex',(2026,11,14,0,0,0));i.compress_type=zipfile.ZIP_DEFLATED;z.writestr(i,(patcher/'classes.dex').read_bytes())
 print(json.dumps(dict(base_sha256=BASE,dex_sha256=hashlib.sha256(a.output_dex.read_bytes()).hexdigest(),changed_methods=8,scope='UI and hardware-command adapter only; original NightChargeProtectManager unchanged')))
