#!/usr/bin/env python3
"""Build with an existing JDK 21 and R8/D8; no Android SDK or APK patching."""
from pathlib import Path
import argparse,os,subprocess as sp,tempfile,zipfile
ROOT=Path(__file__).resolve().parent
MODULE=ROOT/'module' if (ROOT/'module').is_dir() else ROOT
ap=argparse.ArgumentParser()
ap.add_argument('--java-home',type=Path,default=os.environ.get('JAVA_HOME','/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home'))
ap.add_argument('--r8-jar',type=Path,default=os.environ.get('R8_JAR','/private/tmp/android-r8/r8.jar'))
args=ap.parse_args();java=args.java_home
if not (java/'bin/javac').is_file() or not args.r8_jar.is_file():ap.error('Provide --java-home and --r8-jar')
def run(*cmd):sp.run([str(x) for x in cmd],check=True)
with tempfile.TemporaryDirectory(prefix='tb371fc-charge-build-') as tmp:
    tmp=Path(tmp);classes=tmp/'classes';dex=tmp/'dex';classes.mkdir();dex.mkdir()
    stub=tmp/'stub';stub.mkdir()
    run(java/'bin/javac','--release','8','-Xlint:-options','-d',stub,*sorted((ROOT/'compile-stubs').rglob('*.java')))
    run(java/'bin/javac','--release','8','-Xlint:-options','-cp',stub,'-d',classes,*sorted((ROOT/'src').glob('*.java')))
    run(java/'bin/java','-cp',classes,'ChargePolicyTest')
    run(java/'bin/java','-cp',args.r8_jar,'com.android.tools.r8.D8','--release','--min-api','35','--lib',java,'--lib',stub,'--output',dex,*sorted(classes.glob('ChargeBridge*.class')),classes/'ChargePolicy.class')
    with zipfile.ZipFile(MODULE/'charge-bridge.jar','w',zipfile.ZIP_DEFLATED) as z:
        i=zipfile.ZipInfo('classes.dex',(2026,11,14,0,0,0));i.compress_type=zipfile.ZIP_DEFLATED
        z.writestr(i,(dex/'classes.dex').read_bytes())
print('Built',MODULE/'charge-bridge.jar')
