#!/usr/bin/env python3
"""Drive actual preferences; never forge battery readings or prediction history."""
from pathlib import Path
import os,subprocess as sp,time,json,re,xml.etree.ElementTree as E
ROOT=Path(__file__).resolve().parent
OUT=ROOT/'four-mode-acceptance';OUT.mkdir(exist_ok=True)
A=['adb','-s','HA1T90V1']
def adb(*args):
 r=sp.run(A+list(args),text=True,capture_output=True,timeout=25)
 if r.returncode:raise RuntimeError(r.stdout+r.stderr)
 return r.stdout
def root(s):return adb('shell','su','-c',"'"+s+"'")
def ensure_interactive():
 for attempt in range(10):
  power=adb('shell','dumpsys','power')
  if 'mWakefulness=Awake' not in power:
   adb('shell','input','keyevent','KEYCODE_POWER')
   time.sleep(2)
   continue
  adb('shell','wm','dismiss-keyguard')
  time.sleep(2)
  if 'interactive=true' in state()[0]:return
  pin=os.environ.get('TB371FC_TEST_PIN','')
  if pin:
   adb('shell','input','swipe','900','2500','900','700','300')
   adb('shell','input','text',pin)
   adb('shell','input','keyevent','KEYCODE_ENTER')
   time.sleep(3)
   if 'interactive=true' in state()[0]:return
 raise RuntimeError('Could not restore an interactive display')
def start_charger_page():
 for attempt in range(10):
  r=sp.run(A+['shell','su','-c',"'am start -W -n com.miui.securitycenter/com.miui.powercenter.nightcharge.ChargerProtectActivity'"],text=True,capture_output=True,timeout=25)
  if r.returncode==0 and 'Status: ok' in r.stdout:return
  time.sleep(3)
 raise RuntimeError('Charger protection page did not start:\n'+r.stdout+r.stderr)
def snapshot(name):
 adb('shell','uiautomator','dump','/data/local/tmp/charge-four-test.xml')
 p=OUT/(name+'.xml');adb('pull','/data/local/tmp/charge-four-test.xml',str(p))
 return list(E.parse(p).iter('node'))
def click(title,name):
 for attempt in range(4):
  nodes=snapshot(name+'-find')
  for n in nodes:
   if n.get('text')==title:
    x1,y1,x2,y2=map(int,re.findall(r'\d+',n.get('bounds')))
    adb('shell','input','tap',str((x1+x2)//2),str((y1+y2)//2));return
  # Scroll to the four choices without changing orientation or density.
  bounds=next((n.get('bounds') for n in nodes if n.get('scrollable')=='true'), '[0,0][2942,1840]')
  x1,y1,x2,y2=map(int,re.findall(r'\d+',bounds));x=(x1+x2)//2
  adb('shell','input','swipe',str(x),str(y2-150),str(x),str(y1+250),'400')
 raise AssertionError('Preference unavailable: '+title)
def state():
 raw=root('cat /dev/tb371fc_stock_charge/status; settings --user 0 get secure tb371fc_smart_common_limit; cat /sys/class/power_supply/battery/status')
 values={}
 for part in raw.split():
  if '=' in part:
   k,v=part.split('=',1);values[k]=v
 return raw,values
rows=[]
ensure_interactive()
root('settings --user 0 put system screen_off_timeout 600000')
start_charger_page()
time.sleep(2)
for title,mode in [
    ('电池健康保护',2),
    ('智能充电保护',1),
    ('正常充满',0),
    ('旁路供电',3),
    ('正常充满',0),
    ('旁路供电',3),
]:
 click(title,str(mode));time.sleep(3)
 raw,s=state();assert s.get('mode')==str(mode),raw
 assert s.get('native_ready')=='true',raw
 assert s.get('input_suspend')=='0',raw
 nodes=snapshot(str(mode)+'-selected')
 assert any(n.get('text')==title and n.get('checked')=='true' for n in nodes),title
 if mode==2:
  assert s.get('smart_limit')=='80',raw
  if int(s['soc'])>=80:assert s.get('hold')=='true' and s.get('charging_enabled')=='0',raw
 if mode==3 and s.get('interactive')=='true' and int(s['soc'])>=25:
  assert s.get('hold')=='true',raw
 rows.append(dict(title=title,state=raw));print('PASS',title,raw,flush=True)
 (OUT/'results.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2))
root('input keyevent 223');time.sleep(3)
raw,s=state();assert s.get('interactive')=='false',raw
if s.get('mode')=='3':
    assert s.get('hold')=='false',raw
    assert s.get('charging_enabled')=='1',raw
rows.append(dict(test='screen-off',state=raw));print('SCREEN_OFF',raw,flush=True)
(OUT/'results.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2))
ensure_interactive()
raw,s=state();assert s.get('interactive')=='true',raw
if s.get('mode')=='3' and int(s['soc'])>=25:
    assert s.get('hold')=='true',raw
    assert s.get('charging_enabled')=='0',raw
rows.append(dict(test='screen-on',state=raw));print('SCREEN_ON',raw,flush=True)
(OUT/'results.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2))
