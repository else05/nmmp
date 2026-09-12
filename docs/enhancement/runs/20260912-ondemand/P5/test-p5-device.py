from pathlib import Path
import subprocess
w=Path(__file__).resolve().parent;r=w.parents[2]
a=['adb','-s','192.168.6.118:5555'];remote='/data/local/tmp/nmmp-p5'
def run(args):
 p=subprocess.run(a+args,capture_output=True,text=True,timeout=90)
 if p.returncode: raise RuntimeError(p.stdout+p.stderr)
 return p.stdout+p.stderr
run(['shell','mkdir -p '+remote])
for name in ['envelope.golden','content.golden']:
 run(['push',str(r/'nmm-protect/mksrc/loader/tests'/name),remote+'/'+name])
run(['push',str(w/'p5-vectors/stage0-vectors.bin'),remote+'/stage0-vectors.bin'])
for variant in ['Release','Debug']:
 b=w/('p5-android-'+variant)
 names=['crypto_test','loader_test','once_test','stage0_test','module_test','bootstrap_bound','bootstrap_unbound']
 for name in names+['libtest_inner.so','module.content']:
  run(['push',str(b/name),remote+'/'+name])
 run(['shell','chmod 755 '+' '.join(remote+'/'+n for n in names)])
 commands=['crypto_test '+remote+'/envelope.golden','loader_test '+remote+'/content.golden','once_test','stage0_test '+remote+'/envelope.golden','module_test '+remote+'/libtest_inner.so '+remote+'/module.content']
 commands += ['bootstrap_'+branch+' '+str(mode) for branch in ['bound','unbound'] for mode in range(7)]
 result=''
 for command in commands: result+=run(['shell',remote+'/'+command])
 result+=run(['shell','LD_LIBRARY_PATH=/data/local/tmp/nmmp-on-demand/reader /data/local/tmp/nmmp-on-demand/reader/nmmp_native_vm_test '+remote+'/stage0-vectors.bin'])
 (w/('p5-device-'+variant+'.log')).write_text(result,encoding='utf-8')
 print(variant+': '+str(len(commands))+' loader/stage0/bootstrap scenarios and 24 program vectors PASS')
