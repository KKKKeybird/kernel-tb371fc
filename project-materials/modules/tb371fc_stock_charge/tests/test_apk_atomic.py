#!/usr/bin/env python3
import os
from pathlib import Path
import stat
import shutil
import struct
import subprocess
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "ui-src" / "ApkPatch.java"


def java_tool(name):
    home = os.environ.get("JAVA_HOME")
    candidates = []
    if home:
        candidates.append(Path(home) / "bin" / name)
    candidates.append(Path("/opt/homebrew/opt/openjdk@21/bin") / name)
    found = shutil.which(name)
    if found:
        candidates.append(Path(found))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit("missing " + name)


def add_member(archive, name, payload, stored=False):
    entry = zipfile.ZipInfo(name, (2024, 1, 1, 0, 0, 0))
    entry.compress_type = zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED
    entry.create_system = 3
    entry.external_attr = 0o100644 << 16
    archive.writestr(entry, payload)


def make_fixture(path, include_target=True):
    with zipfile.ZipFile(path, "w") as archive:
        add_member(archive, "META-INF/MANIFEST.MF", b"Manifest-Version: 1.0\r\n\r\n")
        add_member(archive, "stored.bin", b"stored-payload" * 17, stored=True)
        add_member(archive, "lib/arm64-v8a/libfixture.so", b"\x7fELFfixture" * 29, stored=True)
        if include_target:
            add_member(archive, "classes3.dex", b"old-dex-payload" * 31)
        add_member(archive, "resources.arsc", b"resource-payload" * 23)


def local_data_offset(path, name):
    with zipfile.ZipFile(path) as archive:
        entry = archive.getinfo(name)
    with path.open("rb") as stream:
        stream.seek(entry.header_offset)
        header = stream.read(30)
    assert header[:4] == b"PK\x03\x04", name + " missing local header"
    name_length, extra_length = struct.unpack_from("<HH", header, 26)
    return entry.header_offset + 30 + name_length + extra_length


def run_patcher(java, classes, source_zip, replacement, target):
    return subprocess.run(
        [str(java), "-cp", str(classes), "ApkPatch", str(source_zip), str(replacement), str(target)],
        text=True,
        capture_output=True,
    )


def assert_alignment(path):
    with zipfile.ZipFile(path) as archive:
        assert archive.getinfo("lib/arm64-v8a/libfixture.so").compress_type == zipfile.ZIP_STORED
        assert archive.getinfo("stored.bin").compress_type == zipfile.ZIP_STORED
    assert local_data_offset(path, "lib/arm64-v8a/libfixture.so") % 16384 == 0
    assert local_data_offset(path, "stored.bin") % 4 == 0


def leftover_temps(target):
    return list(target.parent.glob("." + target.name + ".*.tmp"))


def main():
    javac = java_tool("javac")
    java = java_tool("java")
    with tempfile.TemporaryDirectory(prefix="tb371fc-apk-atomic-") as temporary:
        work = Path(temporary)
        classes = work / "classes"
        classes.mkdir()
        subprocess.run(
            [str(javac), "--release", "8", "-Xlint:all", "-Xlint:-options", "-Werror",
             "-d", str(classes), str(SOURCE)],
            check=True,
        )

        source_zip = work / "source.apk"
        replacement = work / "classes3.dex"
        target = work / "securitycenter.apk"
        replacement_bytes = b"replacement-dex-payload" * 41
        replacement.write_bytes(replacement_bytes)
        make_fixture(source_zip)

        target.write_bytes(b"original-target-before-success")
        old_inode = target.stat().st_ino
        success = run_patcher(java, classes, source_zip, replacement, target)
        assert success.returncode == 0, success.stderr
        assert target.stat().st_ino != old_inode, "atomic replacement retained old inode"
        target_mode = stat.S_IMODE(target.stat().st_mode)
        assert target_mode == 0o644, "output mode is %o, expected 644" % target_mode
        assert target_mode & 0o111 == 0, "output unexpectedly has execute permission"
        with zipfile.ZipFile(target) as archive:
            assert archive.read("classes3.dex") == replacement_bytes
            assert archive.read("stored.bin") == b"stored-payload" * 17
            assert archive.read("lib/arm64-v8a/libfixture.so") == b"\x7fELFfixture" * 29
        assert_alignment(target)
        assert not leftover_temps(target)

        before_bytes = b"original-target-before-failure"
        target.write_bytes(before_bytes)
        os.chmod(target, 0o640)
        before_inode = target.stat().st_ino
        before_mode = stat.S_IMODE(target.stat().st_mode)
        bad_zip = work / "missing-target.apk"
        make_fixture(bad_zip, include_target=False)
        failure = run_patcher(java, classes, bad_zip, replacement, target)
        assert failure.returncode != 0, "patch unexpectedly succeeded without target"
        assert target.read_bytes() == before_bytes, "failed patch changed target bytes"
        assert target.stat().st_ino == before_inode, "failed patch replaced target inode"
        assert stat.S_IMODE(target.stat().st_mode) == before_mode, "failed patch changed target mode"
        assert not leftover_temps(target), "failed patch left temporary file"

    print("PASS atomic APK replacement, 0644 output, failure preservation, and stored alignment")


if __name__ == "__main__":
    main()
