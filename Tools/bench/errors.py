import json, subprocess, sys, re
js, cli, L = sys.argv[1], sys.argv[2], sys.argv[3]
g=json.load(open(js)); out=subprocess.run([cli,js[:-5]+'.wav','/dev/null','multi','120'],capture_output=True,text=True).stdout.splitlines()
names={'Mélodie':'melody','Accords':'chords','Basse':'bass','Batterie':'drums'}
NN=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
def nm(p): return NN[p%12]+str(p//12-1)
det=[]
for l in out[1:]:
    f=l.split('\t')
    if len(f)<5 or names.get(f[2].strip())!=L: continue
    m=re.match(r'([A-G]#?)(-?\d+)',f[3]); 
    if not m: continue
    det.append((float(f[0]),float(f[1]),(int(m.group(2))+1)*12+NN.index(m.group(1))))
T=[t for t in g['truth'] if t['layer']==L]
used=set(); miss=[]
for t in T:
    ok=False
    for i,x in enumerate(det):
        if i not in used and x[2]==t['pitch'] and abs(x[0]-t['start'])<0.05: used.add(i); ok=True; break
    if not ok: miss.append(t)
print('MISSED:', ' '.join(f"{nm(t['pitch'])}@{t['start']:.2f}" for t in miss))
print('FALSE :', ' '.join(f"{nm(x[2])}@{x[0]:.2f}-{x[1]:.2f}" for i,x in enumerate(det) if i not in used))
allp=sorted([(t['start'],nm(t['pitch']),t['layer'][0]) for t in g['truth'] if t['layer']!='drums'])
print('TRUTH :', ' '.join(f"{n}{l}@{s:.2f}" for s,n,l in allp))
