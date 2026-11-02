#!/usr/bin/env python3
"""Check this text-only public export without printing matching secret values."""
from pathlib import Path
import re,sys
root=Path(__file__).resolve().parents[1]
denied={'.apk','.so','.jar','.class','.zip','.img','.bin','.bundle','.pyc','.p12','.pfx','.key'}
patterns={
 'personal_mac_path':re.compile(r'/(?:Users|Volumes)/[A-Za-z0-9_.-]+/'),
 'private_key':re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
 'github_token':re.compile(r'\bgh[pousr]_[A-Za-z0-9]{20,}\b'),
 'bearer_token':re.compile(r'(?i)bearer\s+[A-Za-z0-9._-]{20,}'),
 'device_serial':re.compile(r'\bHA[0-9A-Z]{6,}\b'),
}
issues=[];count=0
for p in root.rglob('*'):
 if not p.is_file():continue
 if '__pycache__' in p.parts:continue
 if p.suffix.lower() in denied:issues.append((str(p.relative_to(root)),'binary_or_private_file'));continue
 try:s=p.read_text(encoding='utf-8')
 except UnicodeError:issues.append((str(p.relative_to(root)),'non_text'));continue
 if '\x00' in s:issues.append((str(p.relative_to(root)),'nul_byte'))
 for name,pattern in patterns.items():
  if pattern.search(s):issues.append((str(p.relative_to(root)),name))
 count+=1
for path,kind in issues:print(path+': '+kind+' (value omitted)')
print(f'Checked {count} text files; findings: {len(issues)}')
sys.exit(bool(issues))
