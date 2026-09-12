from pathlib import Path
import subprocess,time,json,hashlib
w=Path(__file__).resolve().parent; out=w/'p5-bad-program'; adb=[r'E:\Scoop\apps\adb\current\platform-tools\adb.exe','-s','192.168.6.118:5555']
def run(args): return subprocess.check_output(adb+args,text=True,encoding='utf-8',errors='replace')
report={}
try:
 assert 'Success' in run(['install','-r',str(out/'protected-signed.apk')])
 run(['shell','am','force-stop','org.savior.sync'])
 remote=run(['shell','pm','path','org.savior.sync']).strip().removeprefix('package:')
 report['apkSha256']=hashlib.sha256((out/'protected-signed.apk').read_bytes()).hexdigest()
 assert run(['shell','sha256sum',remote]).split()[0]==report['apkSha256']
 with (out/'negative-process.log').open('w',encoding='utf-8') as log:
  proc=subprocess.Popen(adb+['logcat','-T','1','-v','threadtime'],stdout=log,stderr=subprocess.STDOUT)
  try:
   time.sleep(.4)
   start=run(['shell','am','start','-n','org.savior.sync/.MainActivity']); (out/'negative-start.log').write_text(start,encoding='utf-8')
   for _ in range(30):
    time.sleep(.2)
    if 'initialization failed (-6)' in (out/'negative-process.log').read_text(encoding='utf-8',errors='replace'): break
  finally:
   proc.terminate(); proc.wait(timeout=5)
 logs=(out/'negative-process.log').read_text(encoding='utf-8',errors='replace')
 report['loaderRejectedBeforePayload']='initialization failed (-6)' in logs
 report['loaderReadySeen']='NMMP-Loader: ready id=' in logs
 report['jniLoadFailed']='UnsatisfiedLinkError' in logs
 assert report['loaderRejectedBeforePayload'] and not report['loaderReadySeen'] and report['jniLoadFailed'],report
finally:
 subprocess.run(['python',str(w/'check-device.py'),'p5-combined'],check=True)
 report['goodApkRestoredAndVerified']=True
 (out/'negative-result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 print(json.dumps(report))
