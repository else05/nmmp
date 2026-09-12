from pathlib import Path
import subprocess,json
w=Path(__file__).resolve().parent;h=w/'s5-harness';out=h/'probe-results';out.mkdir(exist_ok=False);adb=[r'E:\Scoop\apps\adb\current\platform-tools\adb.exe','-s','192.168.6.118:5555'];remote='/data/local/tmp/nmmp-on-demand/bench'
def run(args):return subprocess.check_output(adb+args,text=True,encoding='utf-8',errors='replace',timeout=120)
run(['push',str(h/'probe-runner-dex/classes.dex'),remote+'/probe-runner.dex']);results=[]
for variant in ['A','C']:
 run(['push',str(h/('probe-'+variant+'/build/obj/strip/arm64-v8a/libc++_en.so')),remote+'/probe-'+variant+'.so'])
 for case in ['arithmetic','branch','shortCall','largeShort','jniObject','arrayPayload','exception','recursive','multiThread']:
  text=run(['shell','CLASSPATH='+remote+'/probe-runner.dex','app_process64','/system/bin','bench.ProbeMain',remote+'/probe-'+variant+'.so',case,'100'])
  (out/(variant+'-'+case+'.log')).write_text(text,encoding='utf-8');rows=[json.loads(line) for line in text.splitlines() if line.startswith('{')];assert len(rows)==1;row=rows[0];row['variant']=variant;results.append(row);print(json.dumps(row),flush=True)
(out/'results.json').write_text(json.dumps(results,indent=2))
for case in ['arithmetic','branch','shortCall','largeShort','jniObject','arrayPayload','exception','recursive','multiThread']:
 rows=[r for r in results if r['case']==case];assert rows[0]['checksum']==rows[1]['checksum']
large=[r for r in results if r['variant']=='C' and r['case']=='largeShort'][0];assert max(large['counters'][14:17])<64 and large['counters'][2]<7896
assert all(r['counters'][0]>0 for r in results if r['variant']=='A')
assert all(r['counters'][0]>=0 for r in results)
print('PASS: native requests and reader domains observed; large-short path reads only initial instruction region')
