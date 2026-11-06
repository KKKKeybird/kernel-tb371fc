#!/usr/bin/env python3
"""Patch one verified SystemUI DEX instruction; all other APK members retain bytes."""
import argparse
import hashlib
import json
import struct
import zipfile
import zlib
from pathlib import Path

BASE = "8bb7b6fc36e2bfbe4f3ee2ae91512c95ee32209e86a71e1437040f3eaf45e5bb"
CLASS = "Lcom/android/wm/shell/sosc/SoScUtilsImpl;"


def uleb(data, position):
    value = shift = 0
    while True:
        byte = data[position]
        position += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, position
        shift += 7
        if shift > 35:
            raise ValueError("Invalid uleb128")


def locate(data):
    count, offset = struct.unpack_from("<II", data, 0x38)
    strings = []
    for index in range(count):
        position = struct.unpack_from("<I", data, offset + 4 * index)[0]
        _, position = uleb(data, position)
        strings.append(data[position:data.index(0, position)].decode("utf8", "replace"))
    count, offset = struct.unpack_from("<II", data, 0x40)
    types = [strings[struct.unpack_from("<I", data, offset + 4 * index)[0]] for index in range(count)]
    count, offset = struct.unpack_from("<II", data, 0x58)
    methods = []
    for index in range(count):
        class_index, _, string_index = struct.unpack_from("<HHI", data, offset + 8 * index)
        methods.append((types[class_index], strings[string_index]))
    count, offset = struct.unpack_from("<II", data, 0x60)
    for index in range(count):
        position = offset + 32 * index
        if types[struct.unpack_from("<I", data, position)[0]] != CLASS:
            continue
        position = struct.unpack_from("<I", data, position + 24)[0]
        counts = []
        for _ in range(4):
            value, position = uleb(data, position)
            counts.append(value)
        for _ in range(counts[0] + counts[1]):
            _, position = uleb(data, position)
            _, position = uleb(data, position)
        for method_count in counts[2:]:
            method_index = 0
            for _ in range(method_count):
                difference, position = uleb(data, position)
                method_index += difference
                _, position = uleb(data, position)
                code, position = uleb(data, position)
                if methods[method_index] == (CLASS, "<clinit>"):
                    return code + 16
    raise ValueError("Exact initializer not found")


def patch_dex(data):
    data = bytearray(data)
    position = locate(data)
    site = position + 0xCA
    if data[site:site + 2] != b"\x0a\x00":
        raise ValueError("Expected move-result v0 at +0xca")
    if data[site - 6] != 0x6E or data[site + 2] != 0x6A:
        raise ValueError("Expected contains() then sput-boolean")
    data[site:site + 2] = b"\x12\x10"
    data[12:32] = hashlib.sha1(data[32:]).digest()
    struct.pack_into("<I", data, 8, zlib.adler32(data[12:]) & 0xFFFFFFFF)
    return bytes(data), site


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    raw = args.input.read_bytes()
    if hashlib.sha256(raw).hexdigest() != BASE:
        raise SystemExit("Unsupported SystemUI SHA256; refusing to patch")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.input) as source, zipfile.ZipFile(args.output, "w") as output:
        for info in source.infolist():
            payload = source.read(info.filename)
            if info.filename == "classes5.dex":
                payload, site = patch_dex(payload)
            output.writestr(info, payload)
    with zipfile.ZipFile(args.input) as source, zipfile.ZipFile(args.output) as output:
        changed = [name for name in source.namelist() if source.read(name) != output.read(name)]
        if changed != ["classes5.dex"]:
            raise SystemExit(f"unexpected changed members: {changed}")
        old = source.read(changed[0])
        new = output.read(changed[0])
        difference = [index for index, (left, right) in enumerate(zip(old, new)) if left != right]
        if len(old) != len(new):
            raise SystemExit("DEX length changed")
        if not all(index in range(8, 32) or index in (site, site + 1) for index in difference):
            raise SystemExit(f"unexpected DEX changes: {difference}")
        if new[site:site + 2] != b"\x12\x10":
            raise SystemExit("patched instruction mismatch")
    report = {
        "base_sha256": BASE,
        "patched_sha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
        "member": "classes5.dex",
        "instruction_file_offset": site,
        "changed_members": changed,
        "changed_dex_bytes": difference,
        "apk_bytes": args.output.stat().st_size,
        "scope": "Only IS_TOPBOTTOM_SPLIT_DEVICE result; module must gate device and exact ROM",
    }
    args.output.with_suffix(".patch.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
