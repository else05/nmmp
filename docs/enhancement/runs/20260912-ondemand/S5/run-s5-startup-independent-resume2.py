from pathlib import Path
import subprocess,sys
w=Path(__file__).resolve().parent
collector=w.parents[2]/'nmm-protect/scripts/measure-startup.py'
for index in [2,3]:
    b=w/'s5'/('seed'+str(index))
    plan=b/('startup-independent-resume-plan.json' if index==2 else 'startup-independent-sdk-plan.json')
    output=b/('startup-independent-resumed' if index==2 else 'startup-independent')
    subprocess.run([sys.executable,'-B',str(collector),str(plan),str(output)],check=True)
