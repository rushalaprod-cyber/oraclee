# Synthetic multi-instrument loops with ground truth: python gen.py outdir
import numpy as np, wave, sys, json
out=sys.argv[1]
def render(name, bpm, bars, seed, detune=0.0, jitter=0.0, inharm=0.0, sr=44100):
    rng=np.random.default_rng(seed); beat=60/bpm; L=int(sr*beat*4*bars)+int(sr*0.3)
    x=np.zeros(L); truth=[]
    def add(sig,t):
        i=int(t*sr); n=min(len(sig),L-i)
        if n>0: x[i:i+n]+=sig[:n]
    def J(t): return max(0,t+rng.normal(0,jitter))
    def tone(m,d,v,decay,nh,shape):
        t=np.arange(int(sr*d))/sr; f=440*2**((m-69+detune)/12)
        s=sum(shape**(h-1)*np.sin(2*np.pi*f*h*np.sqrt(1+inharm*h*h)*t+rng.uniform(0,6)) for h in range(1,nh+1) if f*h<9000)
        return s*np.minimum(1,t/0.004)*np.exp(-t*decay)*np.minimum(1,(d-t)/0.02)*v
    def kick(v):
        t=np.arange(int(sr*0.35))/sr; f=50+110*np.exp(-t*35); return np.sin(2*np.pi*np.cumsum(f)/sr)*np.exp(-t*9)*v*1.2
    def snare(v):
        t=np.arange(int(sr*0.2))/sr; return (rng.standard_normal(len(t))*np.exp(-t*22)*0.7+np.sin(2*np.pi*185*t)*np.exp(-t*30)*0.5)*v
    def hat(v):
        t=np.arange(int(sr*0.05))/sr; n=np.diff(rng.standard_normal(len(t)),prepend=0); return n*np.exp(-t*90)*v*0.35
    def note(layer,m,t,d,v,sig):
        add(sig,t); truth.append(dict(layer=layer,pitch=int(m),start=t,end=t+d,vel=v))
    nb=4*bars
    for b in range(nb):
        t=J(b*beat)
        if b%2==0: v=1.0 if b%4==0 else 0.7; note('drums',36,t,0.1,v,kick(v))
        else: v=0.9 if b%4==3 else 0.6; note('drums',38,t,0.1,v,snare(v))
    for e in range(2*nb):
        t=J(e*beat/2); v=0.9 if e%2==0 else 0.45; note('drums',42,t,0.05,v,hat(v))
    roots=[36,43,41,38,36,45,41,43]
    for b in range(nb):
        m=roots[(b//2)%len(roots)]; v=rng.uniform(0.45,0.65); t=J(b*beat)
        note('bass',m,t,beat*0.9,v/0.65,tone(m,beat*0.9,v,2.0,6,0.5))
    prog=[[60,64,67],[57,60,64],[65,69,72],[67,71,74]]
    for i in range(nb//2):
        c=prog[i%4]; t=J(i*2*beat)
        for m in c: note('chords',m,t,beat*1.9,0.6,tone(m,beat*1.9,0.22,1.0,6,0.5))
    scale=[72,74,76,77,79,81,83,84]
    for e in range(0,2*nb,1):
        if rng.random()<0.45: continue
        m=scale[rng.integers(len(scale))]+ (7 if rng.random()<0.2 else 0); v=rng.uniform(0.4,1.0); t=J(e*beat/2)
        note('melody',int(m),t,beat*0.45,v,tone(m,beat*0.45,0.3*v,3,5,0.4))
    x=x/np.max(np.abs(x))*0.9
    w=wave.open(f'{out}/{name}.wav','wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes((x*32767).astype('<i2').tobytes()); w.close()
    json.dump(dict(bpm=bpm,truth=truth),open(f'{out}/{name}.json','w'))
render('clean96',96,2,1)
render('humanized87',87,4,2,jitter=0.008)
render('detuned110',110,4,3,detune=0.35,jitter=0.005)
render('piano124',124,4,4,inharm=0.0004,jitter=0.006)
