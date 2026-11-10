#!/usr/bin/env python3
"""Identity-locked consumer-only CP experiment; not a proven charging fix.

Unlike v2, preserve the charging-status comparison and the complete ISNS error
path. Put direct-branch trampolines in verified unused RX-page padding, extending
only that ELF segment's byte coverage within the already-executable final page.
"""
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / 'current/hvdcp_opti'
OUTPUT = ROOT / 'hvdcp_opti.temp-chain-v3-test'
EXPECTED = 'f3175c83a4f27f7f2e99ccc19a3ea893c1cc15dda20a8881073f5ed4a2c2db8a'

def branch(pc, target, link=False):
    delta = target-pc
    assert delta % 4 == 0 and -(1 << 27) <= delta < (1 << 27)
    return (0x94000000 if link else 0x14000000) | ((delta//4) & 0x3ffffff)

def main():
    original = SOURCE.read_bytes()
    assert hashlib.sha256(original).hexdigest() == EXPECTED, 'Unknown input'
    data = bytearray(original)
    assert data[:6] == b'\x7fELF\x02\x01'
    phoff = struct.unpack_from('<Q', data, 32)[0]
    phsize, phnum = struct.unpack_from('<HH', data, 54)
    rx = []
    for i in range(phnum):
        off = phoff+i*phsize
        ph = struct.unpack_from('<IIQQQQQQ', data, off)
        if ph[0] == 1 and ph[1] == 5: rx.append((off, ph))
    assert len(rx) == 1
    header, ph = rx[0]
    assert ph[2] == ph[3] == 0x16000 and ph[5] == ph[6] == 0x34d20
    gate = ph[3]+ph[6]
    delay = gate+24
    words = [
        0xb9401e69,  # ldr w9,[x19,#0x1c]: CP_STATUS2
        0x37000009 | (3 << 5),  # tbnz w9,#0,gate+16: SWITCHER_OFF
        0x910023e1,  # original add x1,sp,#8
        branch(gate+12, 0x2fdd8),
        0xb9002e7f,  # str wzr,[x19,#0x2c]: no off-state temperature sample
        branch(gate+20, 0x2fe00),
        0x5289c400,  # mov w0,#20000
        branch(delay+4, 0x4ac20, link=True),  # usleep@plt
        0xf9401688,  # original ldr x8,[x20,#0x28] for stack canary
        branch(delay+12, 0x30cb8),
    ]
    code = struct.pack('<'+'I'*len(words), *words)
    assert gate+len(code) <= 0x4b000
    assert original[gate:gate+len(code)] == bytes(len(code)), 'Not unused padding'
    patches = {0x2fdd4:(0x910023e1,branch(0x2fdd4,gate)),
               0x30cb4:(0xf9401688,branch(0x30cb4,delay))}
    for off, (old,new) in patches.items():
        assert struct.unpack_from('<I',data,off)[0] == old
        struct.pack_into('<I',data,off,new)
    data[gate:gate+len(code)] = code
    struct.pack_into('<Q',data,header+32,ph[5]+len(code))
    struct.pack_into('<Q',data,header+40,ph[6]+len(code))
    # Regression guards: charging-state comparison and ISNS failure handling
    # were both accidentally changed by v2. Neither changes in this experiment.
    assert data[0x2fdb0:0x2fdd4] == original[0x2fdb0:0x2fdd4]
    assert data[0x30b50:0x30b98] == original[0x30b50:0x30b98]
    assert data[0x30cd4:0x30cf4] == original[0x30cd4:0x30cf4]
    OUTPUT.write_bytes(data)
    OUTPUT.chmod(0o755)
    (ROOT/'temp-chain-v3-trampoline.bin').write_bytes(code)
    result={'source_sha256':EXPECTED,'output_sha256':hashlib.sha256(data).hexdigest(),
            'file':OUTPUT.name,'gate_vaddr':hex(gate),'delay_vaddr':hex(delay),
            'rx_extension_bytes':len(code),'charging_check_preserved':True,
            'isns_read_and_failure_path_preserved':True,'sysfs_driver_fixed':False}
    (ROOT/'temp-chain-v3-build.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__ == '__main__': main()
