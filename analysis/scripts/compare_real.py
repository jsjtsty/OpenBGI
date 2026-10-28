# Compares the real game's sub-opcode trace (trace_real.py) with obgi_run's OBGI_LIVE log: matched ops, differing pushed values.
import sys, difflib, pathlib
ROOT = pathlib.Path(__file__).resolve().parents[1]
rows = [l.split('\t') for l in (ROOT / 'out/real_trace.tsv').read_text(encoding='utf8').split('\n')[1:] if l][1:6000]
ours = []
for l in (ROOT / 'out/ours_trace.txt').read_text(encoding='utf8').splitlines():
    p = l.split()
    arrow = p.index('->')
    ours.append((p[2], [int(x, 16) for x in p[arrow + 1:]], p[1]))

def pushed(r, n):
    """The n values the real handler left on top of the expression stack, in pushing order."""
    a = [int(x, 16) for x in r[7].split()] if r[7] else []
    if len(a) < n: return None
    return a[:n][::-1]

rs = [r[2] for r in rows]
ours = ours[:6000]
os_ = [o[0] for o in ours]
sm = difflib.SequenceMatcher(None, rs, os_, autojunk=False)
diffs = 0
for tag, i1, i2, j1, j2 in sm.get_opcodes():
    if tag != 'equal': continue
    for k in range(i2 - i1):
        r, o = rows[i1 + k], ours[j1 + k]
        rp = pushed(r, len(o[1]))
        if rp is None: continue
        pops = '?'
        if rp != o[1]:
            diffs += 1
            if diffs <= int(sys.argv[1] if len(sys.argv) > 1 else 40):
                print(f"real#{i1+k} ours#{j1+k} {o[0]} pops {pops}: real pushed {[hex(x) for x in rp]} ours {[hex(x) for x in o[1]]} (ours {o[2]})")
print('ops matched', sum(i2 - i1 for t, i1, i2, j1, j2 in sm.get_opcodes() if t == 'equal'), 'differing', diffs)
