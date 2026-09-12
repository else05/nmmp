from pathlib import Path
import subprocess,sys
w=Path(__file__).resolve().parent
for index in [2,3]:
 b=w/'s5'/('seed'+str(index))
 subprocess.run([sys.executable,str(w.parents[2]/'nmm-protect/scripts/measure-hotspots.py'),str(b/'hotspot-ordinary-plan.json'),str(b/'hotspots')],check=True)
