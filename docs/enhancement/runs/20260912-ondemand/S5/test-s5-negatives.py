from pathlib import Path
import subprocess,time,json,re,hashlib
w=Path(__file__).resolve().parent;adb=[r'E:\Scoop\apps\adb\current\platform-tools\adb.exe','-s','192.168.6.118:5555'];results=[]
def run(args):return subprocess.check_output(adb+args,text=True,encoding='utf-8',errors='replace',timeout=120)
try:
 for kind in ['package','certificate','seed']:
  o=w/('s5-negative-'+kind);apk=o/'protected-signed.apk'
  assert 'Success' in run(['install','-r',str(apk)])
  run(['shell','am','force-stop','org.savior.sync'])
  remote=run(['shell','pm','path','org.savior.sync']).strip().removeprefix('package:');sha=hashlib.sha256(apk.read_bytes()).hexdigest();assert run(['shell','sha256sum',remote]).split()[0]==sha
  with (o/'device-negative.log').open('wb') as log:
   proc=subprocess.Popen(adb+['logcat','-T','1','-v','threadtime'],stdout=log,stderr=subprocess.STDOUT)
   try:
    time.sleep(.3);start=run(['shell','am','start','-n','org.savior.sync/.MainActivity']);(o/'start.log').write_text(start,encoding='utf-8')
    for _ in range(60):
     time.sleep(.2);logs=(o/'device-negative.log').read_text(encoding='utf-8',errors='replace')
     if 'Process: org.savior.sync, PID:' in logs and 'java.lang.InternalError:' in logs:break
   finally:proc.terminate();proc.wait(timeout=5)
  logs=(o/'device-negative.log').read_text(encoding='utf-8',errors='replace');match=re.search(r'Process: org.savior.sync, PID: (\d+)',logs);assert match,'target crash PID not recorded'
  pid=match[1];target='\n'.join(line for line in logs.splitlines() if re.search(r'\s'+pid+r'\s+\d+\s',line))
  result={'mutation':kind,'apkSha256':sha,'pid':pid,'internalError':'java.lang.InternalError:' in target,'errors':re.findall(r'java.lang.InternalError: ([^\n]+)',target)}
  assert result['internalError'];(o/'target-process.log').write_text(target,encoding='utf-8');(o/'device-result.json').write_text(json.dumps(result,indent=2));results.append(result);print(json.dumps(result),flush=True)
finally:
 subprocess.run(['python',str(w/'check-device.py'),'final-acceptance'],check=True)
 (w/'s5-negative-results.json').write_text(json.dumps({'results':results,'unseededCandidateRestoredAndVerified':True},indent=2))
