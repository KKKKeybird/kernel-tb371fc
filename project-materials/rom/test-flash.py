"""Installer preflight tests; no real adb or fastboot is executed."""
import hashlib,importlib.util,json,pathlib,subprocess,sys,tempfile,unittest.mock

r=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('flash',r/'flash.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
assert m.only_serial('List of devices attached\nTEST device\n','device')=='TEST'
for text in ['','TEST unauthorized\n','A fastboot\nB fastboot\n']:
    try:m.only_serial(text,'fastboot')
    except RuntimeError:pass
    else:raise AssertionError('ambiguous or unauthorized device accepted')
with unittest.mock.patch.object(m,'run',return_value='(bootloader) partition-size:super: 0x300000000\nFinished. Total time: 0s'):
    assert m.getvar('fake','TEST','partition-size:super')=='0x300000000'
with tempfile.TemporaryDirectory() as td:
    p=pathlib.Path(td);(p/'images').mkdir();(p/'images/test.img').write_bytes(b'fixture')
    row={'path':'images/test.img','bytes':7,'sha256':hashlib.sha256(b'fixture').hexdigest()}
    (p/'flash-manifest.json').write_text(json.dumps({'files':[row]}))
    with unittest.mock.patch.object(m,'ROOT',p),unittest.mock.patch.object(sys,'argv',['flash.py']),unittest.mock.patch.object(m,'tool',side_effect=AssertionError('device tool called in check mode')):
        m.main()
        (p/'images/test.img').write_bytes(b'changed')
        try:m.verify()
        except RuntimeError:pass
        else:raise AssertionError('modified image accepted')
print('PASS: device selection, getvar parsing, read-only default, and hash rejection')
with unittest.mock.patch.object(m.subprocess,'run',return_value=type('Failed',(),{'returncode':1,'stdout':'failed for TEST_DEVICE_SERIAL'})()):
    try:m.run(['fastboot','-s','TEST_DEVICE_SERIAL','getvar','product'],True)
    except RuntimeError as error:
        assert 'TEST_DEVICE_SERIAL' not in str(error) and '<DEVICE>' in str(error)
    else:raise AssertionError('failed command accepted')
print('PASS: device serial redacted from failure arguments and output')
