from pathlib import Path
import json
import re
import subprocess
import time

w = Path(__file__).resolve().parent
out = w / 'observation'
out.mkdir(exist_ok=False)
row = json.loads((w / 'startup/attempts.jsonl').read_text(encoding='utf-8').splitlines()[-1])
assert row['valid']
adb = [r'D:\Android\SDK\platform-tools\adb.exe', '-s', '192.168.6.118:5555']
def run(args):
    return subprocess.check_output(adb + args, text=True, encoding='utf-8', errors='replace', timeout=45)
started = time.monotonic()
samples = []
for index in range(4):
    if index:
        time.sleep(max(0, index * 20 - (time.monotonic() - started)))
    pid = run(['shell', 'pidof', 'org.savior.sync']).strip()
    assert pid == row['pid'], pid
    sample = dict(elapsedSeconds=time.monotonic() - started, pid=pid)
    samples.append(sample)
    print(json.dumps(sample), flush=True)
memory = run(['shell', 'dumpsys', 'meminfo', 'org.savior.sync'])
(out / 'memory.log').write_text(memory, encoding='utf-8')
raw = run(['logcat', '-d', '--pid=' + row['pid'], '-v', 'epoch'])
log = '\n'.join(line for line in raw.splitlines() if re.match(r'^\s*\d+\.\d+\s', line)
                and float(line.split()[0]) >= row['deviceStartEpoch'])
(out / 'process.log').write_text(log, encoding='utf-8')
markers = [m for m in ['FATAL EXCEPTION', 'InternalError', 'UnsatisfiedLinkError', 'SIGSEGV', 'JNI DETECTED ERROR'] if m in log]
assert not markers, markers
window = run(['shell', 'dumpsys', 'window', 'windows'])
(out / 'window.log').write_text(window, encoding='utf-8')
assert any('org.savior.sync/' in block and 'MainActivity' in block and 'HAS_DRAWN' in block
           and 'isOnScreen=true' in block and (' ' + row['pid'] + ':') in block
           for block in re.split(r'(?=  Window #[0-9]+)', window))
result = dict(apkSha256=row['apkSha256'], samples=samples, mainWindowVisible=True, crashMarkers=markers,
    totalPssKb=int(re.search(r'(?:TOTAL PSS|TOTAL):\s*(\d+)', memory)[1]),
    scope='One minute of idle unactivated application observation, not full business or performance acceptance')
(out / 'summary.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
