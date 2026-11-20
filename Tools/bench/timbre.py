# log-spectral distance between the original and a rendering: python timbre.py original.wav render.wav
import numpy as np, wave, sys
def load(p):
    w=wave.open(p); n=w.getnframes(); ch=w.getnchannels(); sr=w.getframerate()
    x=np.frombuffer(w.readframes(n),dtype='<i2').astype(float).reshape(-1,ch).mean(1)/32768; return x,sr
a,sr=load(sys.argv[1]); b,_=load(sys.argv[2]); n=min(len(a),len(b)); a,b=a[:n],b[:n]
N=4096; hop=1024; win=np.hanning(N)
edges=np.geomspace(40,16000,41); f=np.fft.rfftfreq(N,1/sr)
def bands(x):
    out=[]
    for i in range(0,len(x)-N,hop):
        S=np.abs(np.fft.rfft(x[i:i+N]*win))**2
        out.append([S[(f>=edges[k])&(f<edges[k+1])].sum()+1e-12 for k in range(40)])
    return 10*np.log10(np.array(out))
A=bands(a); B=bands(b)
act=A.max(1)>A.max()-50
A=A[act]-A[act].max(1,keepdims=True); B=B[act]-B[act].max(1,keepdims=True)
print(f"{sys.argv[2].split('/')[-1]:14s} spectral distance {np.sqrt(((np.clip(A,-60,0)-np.clip(B,-60,0))**2).mean()):.2f} dB  (lower = closer to the original)")
