from pathlib import Path
import sys,subprocess,json,zipfile,hashlib
w=Path(__file__).resolve().parent;root=w.parents[2];base=w/'s5'/('seed'+sys.argv[1]);jar=json.loads((w/'s5-jars.json').read_text())['current'];java=r'E:\Scoop\apps\corretto17-jdk\current\bin\java.exe'
for name,source in [('A','B'),('D','C')]:
 o=base/name
 with zipfile.ZipFile(base/source/'input/build/original-protect.apk') as z,zipfile.ZipFile(o/'input/build/original-protect.apk','w') as dst:
  for info in z.infolist():dst.writestr(info,(o/'input/build/obj/strip/arm64-v8a/libc++_en.so').read_bytes() if info.filename=='lib/arm64-v8a/libc++_en.so' else z.read(info))
for name in ['A','B','C','D']:
 o=base/name
 subprocess.run(['python',str(w/'sign-variant.py'),str(o.relative_to(w))],check=True)
 with (o/'audit.log').open('w',encoding='utf-8') as log:
  subprocess.run([java,'-cp',str(w/'s5-harness/auditor-classes')+';'+jar,'AuditMethods' if name in ['A','B'] else 'AuditDemandMethods',str(o),str(o),str(o/'protected-signed.apk')],check=True,stdout=log,stderr=subprocess.STDOUT)
 report=json.loads((o/'all-method-audit.json').read_text(encoding='utf-8'));assert report['count']==526 and report['packagedNativeMethodSetExactMatch']
 print(name+' signed and all 526 methods audited',flush=True)
startup={'serial':'192.168.6.118:5555','package':'org.savior.sync','activity':'org.savior.sync/.MainActivity','warmups':3,'rounds':20,'variants':[{'name':n,'apk':str(base/n/'protected-signed.apk'),'loader':n in ['B','D']} for n in ['A','B','C','D']]}
(base/'startup-plan.json').write_text(json.dumps(startup,indent=2))
hotspot={'serial':'192.168.6.118:5555','adb':r'E:\Scoop\apps\adb\current\platform-tools\adb.exe','runner':str(w/'s5-harness/runner-dex/classes.dex'),'warmups':5,'rounds':20,'cases':{'arithmetic':100,'branch':100,'shortCall':10000,'largeShort':10000,'jniObject':1000,'arrayPayload':1000,'exception':1000,'recursive':1000,'multiThread':10000},'variants':[{'name':n,'library':str(base/('hotspot-'+n)/'build/obj/strip/arm64-v8a/libc++_en.so')} for n in ['A','B','C','D']]}
(base/'hotspot-plan.json').write_text(json.dumps(hotspot,indent=2))
sets=[(base/n/'selected-descriptors.txt').read_bytes() for n in ['A','B','C','D']];assert all(x==sets[0] for x in sets)
print('All four variants have the exact same selected descriptor set',flush=True)
