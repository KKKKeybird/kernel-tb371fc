#!/usr/bin/env python3
from pathlib import Path
import json
import os
import shutil
import stat
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]
SPEAKER = ROOT.parent / "tb371fc_speaker_rotation"
HALL = ROOT.parent / "tb371fc_hall_cover"
OUT = REPO / "outputs" / "TB371FC-SystemCompat-v1.0.zip"
R8_JAR = Path(os.environ.get("R8_JAR", "/private/tmp/android-r8/r8.jar"))

if not R8_JAR.is_file():
    raise SystemExit(f"missing R8 compiler: {R8_JAR}")

with tempfile.TemporaryDirectory(prefix="tb371fc-system-compat-") as tmp:
    stage = Path(tmp) / "stage"
    classes = Path(tmp) / "classes"
    dexout = Path(tmp) / "dex"
    speaker_dst = stage / "system/etc/tb371fc/core/speaker_rotation"
    hall_dst = stage / "system/etc/tb371fc/core/hall_cover"
    speaker_dst.mkdir(parents=True)
    hall_dst.mkdir(parents=True)
    classes.mkdir()
    dexout.mkdir()

    for name in ("module.prop", "customize.sh", "service.sh", "action.sh", "uninstall.sh", "README.md", "CHANGELOG.md"):
        shutil.copy2(ROOT / name, stage / name)
    shutil.copy2(SPEAKER / "supervisor.sh", speaker_dst / "supervisor.sh")
    shutil.copy2(HALL / "service.sh", hall_dst / "service.sh")

    sources = [SPEAKER / name for name in ("SpeakerRotation.java", "RotationMatrix.java", "RotationPolicy.java")]
    subprocess.run(["javac", "--release", "17", "-d", str(classes), *map(str, sources)], check=True)
    subprocess.run(["java", "-cp", str(R8_JAR), "com.android.tools.r8.D8", "--release", "--min-api", "35", "--output", str(dexout), *map(str, classes.glob("*.class"))], check=True)
    manifest = Path(tmp) / "MANIFEST.MF"
    manifest.write_text("Manifest-Version: 1.0\n\n")
    subprocess.run(["jar", "cfm", str(speaker_dst / "rotation.jar"), str(manifest), "-C", str(dexout), "classes.dex"], check=True)

    executable = {"customize.sh", "service.sh", "action.sh", "uninstall.sh", "system/etc/tb371fc/core/speaker_rotation/supervisor.sh", "system/etc/tb371fc/core/hall_cover/service.sh"}
    files = sorted(p.relative_to(stage).as_posix() for p in stage.rglob("*") if p.is_file())
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in files:
            source = stage / name
            info = zipfile.ZipInfo.from_file(source, name)
            info.create_system = 3
            mode = 0o755 if name in executable else 0o644
            info.external_attr = (stat.S_IFREG | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, source.read_bytes())

print(json.dumps({"file": str(OUT), "size": OUT.stat().st_size}, ensure_ascii=False))
