# python score.py dir cli  -> F1 per layer (onset +-50ms, exact pitch) and tempo
import json, subprocess, sys, glob, os
d, cli = sys.argv[1], sys.argv[2]
names={'Mélodie':'melody','Accords':'chords','Basse':'bass','Batterie':'drums'}
drum={'Kick':36,'Snare':38,'Hi-hat':42}
tot={}
for js in sorted(glob.glob(d+'/*.json')):
    g=json.load(open(js)); wav=js[:-5]+'.wav'
    out=subprocess.run([cli,wav,'/dev/null','multi','120'],capture_output=True,text=True).stdout.splitlines()
    bpm=float(out[0].split()[2]); det=[]
    for l in out[1:]:
        f=l.split('\t')
        if len(f)<5: continue
        layer=names.get(f[2].strip(),f[2].strip()); p=drum.get(f[3],None)
        if p is None:
            import re
            m=re.match(r'([A-G]#?)(-?\d+)',f[3]); n=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'].index(m.group(1)); p=(int(m.group(2))+1)*12+n
        det.append(dict(layer=layer,pitch=p,start=float(f[0])))
    line=f"{os.path.basename(js)[:-5]:12s} bpm {bpm:6.2f}/{g['bpm']}"
    allm=0; allt=0; alld=0
    for L in ['drums','bass','chords','melody']:
        T=[t for t in g['truth'] if t['layer']==L]; D=[x for x in det if x['layer']==L]
        used=set(); m=0
        for t in T:
            for i,x in enumerate(D):
                if i not in used and x['pitch']==t['pitch'] and abs(x['start']-t['start'])<0.05:
                    used.add(i); m+=1; break
        # pitch-only-any-layer matches (separation errors aside)
        p=m/len(D) if D else 0; r=m/len(T) if T else 0; f1=2*p*r/(p+r) if p+r else 0
        line+=f" | {L} F1 {f1:.2f} ({m}/{len(T)} +{len(D)-m})"
        tot.setdefault(L,[]).append(f1)
    # layer-agnostic pitched F1
    T=[t for t in g['truth'] if t['layer']!='drums']; D=[x for x in det if x['layer']!='drums']; used=set(); m=0
    for t in T:
        for i,x in enumerate(D):
            if i not in used and x['pitch']==t['pitch'] and abs(x['start']-t['start'])<0.05: used.add(i); m+=1; break
    p=m/len(D) if D else 0; r=m/len(T) if T else 0; f=2*p*r/(p+r) if p+r else 0
    line+=f" | notes(any layer) F1 {f:.2f}"; tot.setdefault('any',[]).append(f)
    print(line)
print('MEAN', ' '.join(f"{k} {sum(v)/len(v):.3f}" for k,v in tot.items()))
