from pathlib import Path
import subprocess,sys
w=Path(__file__).resolve().parent
for index in [1,2,3]:
 b=w/'s5'/('seed'+str(index))
 subprocess.run([sys.executable,str(w.parents[2]/'nmm-protect/scripts/measure-startup.py'),str(b/'startup-plan.json'),str(b/'startup-independent')],check=True)
