"""Patch only the two existing linear extents in our private raw super file."""
from pathlib import Path
import hashlib,json,struct,subprocess

r=Path(__file__).resolve().parent
p=r/'super-working.img'
assert p.is_file() and not p.is_symlink() and p.stat().st_size==12884901888
def sha(f,offset,size):
    f.seek(offset);h=hashlib.sha256()
    while size:
        b=f.read(min(size,16*1024*1024));assert b
        size-=len(b);h.update(b)
    return h.hexdigest()
with p.open('rb') as f:
    f.seek(4096);g=f.read(52)
    assert struct.unpack_from('<I',g)[0]==0x616c4467
    gs=struct.unpack_from('<I',g,4)[0]
    assert gs==52
    assert hashlib.sha256(g[:8]+b'\0'*32+g[40:]).digest()==g[8:40]
    maxmeta=struct.unpack_from('<I',g,40)[0]
    for off in range(8192,8192+maxmeta*4,4096):
        f.seek(off)
        if f.read(4)==b'0PLA':break
    else:raise RuntimeError('No LP metadata')
    f.seek(off);h=f.read(256);hs=struct.unpack_from('<I',h,8)[0]
    assert 128<=hs<=256
    assert hashlib.sha256(h[:12]+b'\0'*32+h[44:hs]).digest()==h[12:44]
    size=struct.unpack_from('<I',h,44)[0]
    f.seek(off+hs);tables=f.read(size)
    assert hashlib.sha256(tables).digest()==h[48:80]
    po,pc,pe=struct.unpack_from('<III',h,80)
    eo,ec,ee=struct.unpack_from('<III',h,92)
    assert pe>=52 and ee>=24
    extents=[struct.unpack_from('<QIQI',tables,eo+i*ee) for i in range(ec)]
    parts=[]
    for i in range(pc):
        b=tables[po+i*pe:po+(i+1)*pe]
        name=b[:36].split(b'\0')[0].decode()
        first,num=struct.unpack_from('<II',b,40)
        ex=extents[first:first+num]
        assert len(ex)==num
        if not ex:continue
        assert num==1 and ex[0][1]==0 and ex[0][3]==0,(name,ex)
        n,ty,start,src=ex[0]
        assert start*512+n*512<=p.stat().st_size
        parts.append({'name':name,'offset':start*512,'bytes':n*512,'original_sha256':sha(f,start*512,n*512)})
    metadata=hashlib.sha256((p.open('rb').read(1024*1024))).hexdigest()
for part in parts:
    name=part['name']
    if name not in {'system_a','vendor_a'}:continue
    # Copy candidate images over the original private extracts; source ROM is immutable.
    local=r/'partitions'/(name+'.img')
    assert local.stat().st_size==part['bytes']
    assert hashlib.sha256(local.read_bytes()).hexdigest()==part['original_sha256']
    subprocess.run(['docker','cp','BUILD_CONTAINER:/build/artifacts/oneclick-rom-20261101/'+name+'.img',str(local)],check=True)
    assert local.stat().st_size==part['bytes']
    with p.open('r+b') as dst,local.open('rb') as src:
        dst.seek(part['offset'])
        for b in iter(lambda:src.read(16*1024*1024),b''):dst.write(b)
with p.open('rb') as f:
    assert hashlib.sha256(f.read(1024*1024)).hexdigest()==metadata
    for part in parts:
        part['candidate_sha256']=sha(f,part['offset'],part['bytes'])
        changed=part['candidate_sha256']!=part['original_sha256']
        assert changed==(part['name'] in {'system_a','vendor_a'}),part['name']
(r/'partition-audit.json').write_text(json.dumps(parts,indent=2)+'\n')
print('PASS: LP checksums and metadata preserved; only system_a/vendor_a changed',flush=True)
