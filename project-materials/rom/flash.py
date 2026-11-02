#!/usr/bin/env python3
"""TB371FC full-ROM installer. Default is local verification, never flashing."""
import argparse, hashlib, json, os, pathlib, re, shutil, subprocess, sys, time

ROOT=pathlib.Path(__file__).resolve().parent
FIRMWARE='abl aop bluetooth boot cmnlib64 cmnlib devcfg dsp dtbo hyp imagefv keymaster modem qupfw recovery tz xbl xbl_config'.split()

def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(16*1024*1024),b''):h.update(b)
    return h.hexdigest()

def verify():
    data=json.loads((ROOT/'flash-manifest.json').read_text())
    for row in data['files']:
        p=ROOT/row['path']
        if not p.is_file() or p.stat().st_size!=row['bytes'] or digest(p)!=row['sha256']:
            raise RuntimeError('校验失败，未刷写：'+row['path'])
    return data

def tool(name):
    bundled=ROOT/'bin/windows'/(name+'.exe')
    if os.name=='nt' and bundled.exists():return str(bundled)
    p=shutil.which(name)
    if not p:raise RuntimeError('缺少工具：'+name)
    return p

def run(argv, capture=False, timeout=300):
    p=subprocess.run(argv,stdout=subprocess.PIPE if capture else None,
                     stderr=subprocess.STDOUT if capture else None,text=True,timeout=timeout)
    if p.returncode:
        shown=list(map(str,argv));output=p.stdout or ''
        for i,value in enumerate(shown[:-1]):
            if value in {'-s','--serial'}:
                serial=shown[i+1]
                if serial:output=output.replace(serial,'<DEVICE>')
                shown[i+1]='<DEVICE>'
        shown[0]=pathlib.Path(shown[0]).name
        message=(' '.join(shown)+'\n'+output).replace(str(ROOT),'<ROM_DIR>')
        raise RuntimeError('命令失败，已停止：'+message)
    return p.stdout or ''

def only_serial(output, state):
    ids=[m.group(1) for line in output.splitlines() if (m:=re.fullmatch(r'(\S+)\s+'+state+r'\s*',line))]
    if len(ids)!=1:raise RuntimeError('请只连接一台平板，并确认 USB 调试或 fastboot 驱动。')
    return ids[0]

def getvar(fb,serial,name):
    out=run([fb,'-s',serial,'getvar',name],True)
    m=re.search(r'(?:\(bootloader\)\s*)?'+re.escape(name)+r':\s*(\S+)',out)
    if not m:raise RuntimeError('无法读取 bootloader 参数：'+name)
    return m.group(1)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--flash',action='store_true');args=ap.parse_args()
    print('仅 TB371FC / 小新 Pad Pro 12.7 2023 骁龙870；一键 ROM 候选。',flush=True)
    data=verify();print('本地文件校验通过。',flush=True)
    if not args.flash:
        print('只读检查完成，未访问或修改设备。');return
    if input('确认实际型号，输入 TB371FC：').strip()!='TB371FC':raise RuntimeError('已取消。')
    mode=input('1 保数据升级（相同移植底包）；2 清除全部数据首次安装。输入 1 或 2：').strip()
    if mode not in {'1','2'}:raise RuntimeError('已取消。')
    if mode=='2' and input('会清除 userdata 和 metadata，输入 ERASE 确认：').strip()!='ERASE':raise RuntimeError('已取消。')
    fb,adb,zstd=tool('fastboot'),tool('adb'),tool('zstd')
    if mode=='1':
        upgrade_serial=only_serial(run([adb,'devices'],True),'device')
        base=run([adb,'-s',upgrade_serial,'shell','getprop','ro.build.version.incremental'],True).strip()
        root=run([adb,'-s',upgrade_serial,'shell','su','0','sha256sum','/data/adb/ksud'],True,timeout=15)
        if base!=data['upgrade_base_incremental'] or not re.search(r'\b'+re.escape(data['root_ksud_sha256'])+r'\s+/data/adb/ksud\b',root):
            raise RuntimeError('保数据升级只支持已适配的同底包及匹配 KSU；旧 Magisk/其他底包不能直接保数据迁移。未刷写。')
    raw=ROOT/'images/super.img'
    expected=data['super_raw']
    if raw.exists():
        if raw.stat().st_size!=expected['bytes'] or digest(raw)!=expected['sha256']:
            raise RuntimeError('已有 super.img 与本包不匹配，请移走后重试。')
    else:
        if shutil.disk_usage(ROOT).free<expected['bytes']+512*1024*1024:raise RuntimeError('解压空间不足，需要约 13GiB 可用空间。')
        part=raw.with_suffix('.img.part')
        run([zstd,'-d','-f',str(ROOT/'images/super.img.zst'),'-o',str(part)],timeout=900)
        if part.stat().st_size!=expected['bytes'] or digest(part)!=expected['sha256']:raise RuntimeError('解压后 super 校验失败，未刷写。')
        part.replace(raw)
    devices=run([fb,'devices'],True)
    if not devices.strip():
        serial=only_serial(run([adb,'devices'],True),'device')
        run([adb,'-s',serial,'reboot','bootloader'])
        for _ in range(30):
            devices=run([fb,'devices'],True)
            if devices.strip():break
            time.sleep(1)
    serial=only_serial(devices,'fastboot')
    product=getvar(fb,serial,'product').strip().lower()
    print('Bootloader product: '+product,flush=True)
    if product not in {'kona','tb371fc'}:raise RuntimeError('product 不匹配：'+repr(product)+'；预期 kona 或 tb371fc。未刷写，请先核对实际机型及 fastboot getvar product 输出。')
    if getvar(fb,serial,'unlocked').lower() not in {'yes','true'}:raise RuntimeError('Bootloader 未解锁或无法确认，未刷写。')
    for name,size in [('super',12884901888),('boot_a',100663296),('dtbo_a',25165824)]:
        if int(getvar(fb,serial,'partition-size:'+name),16)!=size:raise RuntimeError('分区尺寸不匹配，未刷写：'+name)
    def fast(*cmd):return run([fb,'-s',serial,*map(str,cmd)],timeout=900)
    fast('set_active','a')
    for name in FIRMWARE:
        for slot in ['a','b']:fast('flash',name+'_'+slot,ROOT/'images'/(name+'.img'))
    for name in ['vbmeta','vbmeta_system']:
        for slot in ['a','b']:fast('--disable-verity','--disable-verification','flash',name+'_'+slot,ROOT/'images'/(name+'.img'))
    fast('reboot','bootloader');time.sleep(8)
    if mode=='2':fast('erase','userdata');fast('erase','metadata')
    fast('flash','super',raw);fast('reboot')
    print('镜像刷入完成。首次部署会自动重启最多两次，请勿拔线或中断。',flush=True)
    deadline=time.monotonic()+1200
    while time.monotonic()<deadline:
        try:
            p=subprocess.run([adb,'-s',serial,'shell','getprop','sys.tb371fc.setup'],capture_output=True,text=True,timeout=8)
            state=p.stdout.strip()
            if state=='ready':print('自动部署与文件验收完成；请解锁并按随包清单测试实际功能。');return
            if state in {'failed','disabled','safe_mode'}:raise RuntimeError('自动部署停止：'+state+'，保留现场，请查看故障恢复说明。')
        except subprocess.TimeoutExpired:pass
        time.sleep(5)
    raise RuntimeError('等待自动部署超时，不代表刷入成功；请查看设备与故障恢复说明。')

if __name__=='__main__':
    try:main()
    except (RuntimeError,OSError,subprocess.SubprocessError,ValueError,KeyboardInterrupt) as e:
        print('\n停止：'+str(e),file=sys.stderr);sys.exit(1)
