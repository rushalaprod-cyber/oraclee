# Scores pitched notes regardless of layer: python score_real.py dir cli [mode]
#  onset F1  : right pitch, start within 50 ms
#  full F1   : + end within max(80 ms, 30 % of the note)
#  minis     : detected notes shorter than 120 ms that match nothing
#  frags     : extra pieces when one long true note comes out as several notes
import json, subprocess, sys, glob, os, re
d, cli = sys.argv[1], sys.argv[2]
mode = sys.argv[3] if len(sys.argv) > 3 else 'multi'
NN = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
tot = {}
for js in sorted(glob.glob(d + '/*.json')):
    g = json.load(open(js))
    out = subprocess.run([cli, js[:-5] + '.wav', '/dev/null', mode, '120'], capture_output=True, text=True).stdout.splitlines()
    det = []
    for l in out[1:]:
        f = l.split('\t')
        if len(f) < 5 or 'Batterie' in f[2]: continue
        m = re.match(r'([A-G]#?)(-?\d+)$', f[3].strip())
        if not m: continue
        det.append((float(f[0]), float(f[1]), (int(m.group(2)) + 1) * 12 + NN.index(m.group(1))))
    T = [t for t in g['truth'] if t['layer'] != 'drums']
    used = set(); on = 0; full = 0; frags = 0
    for t in T:
        best = None
        for i, x in enumerate(det):
            if i not in used and x[2] == t['pitch'] and abs(x[0] - t['start']) < 0.05:
                best = i; break
        if best is None: continue
        used.add(best); on += 1
        dur = t['end'] - t['start']
        if abs(det[best][1] - t['end']) < max(0.08, 0.3 * dur): full += 1
        if dur > 0.5:  # pieces of the same pitch inside the true note
            for i, x in enumerate(det):
                if i != best and i not in used and x[2] == t['pitch'] and x[0] > t['start'] + 0.05 and x[0] < t['end'] - 0.05:
                    frags += 1; used.add(i)
    minis = sum(1 for i, x in enumerate(det) if i not in used and x[1] - x[0] < 0.12)
    nd, nt = len(det), len(T)
    f1 = lambda m: 2 * m / (nd + nt) if nd + nt else 0
    name = os.path.basename(js)[:-5]
    print(f"{name:16s} notes {nt:3d} -> {nd:3d} | onset F1 {f1(on):.2f} | full F1 {f1(full):.2f} | minis {minis:3d} | frags {frags:3d}")
    for k, v in (('onset', f1(on)), ('full', f1(full)), ('minis', minis), ('frags', frags)):
        tot.setdefault(k, []).append(v)
print('MEAN ' + ' '.join(f"{k} {sum(v)/len(v):.3f}" if k in ('onset', 'full') else f"{k} {sum(v)}" for k, v in tot.items()))
