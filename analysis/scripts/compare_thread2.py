# Greedy in-order comparison of the root thread (real vs obgi_run), resynchronising on a mismatch.  Prints the first
# mismatches with the surrounding ops.   compare_thread2.py [skip real] [skip ours] [count]
import sys, pathlib
ROOT = pathlib.Path(__file__).resolve().parents[1]
rows = [l.split('\t') for l in (ROOT / 'out/real_trace.tsv').read_text(encoding='utf8').split('\n')[1:] if l]
root = rows[2][1]
real = [(i, r[2]) for i, r in enumerate(rows) if r[1] == root]
ours = []
for n, l in enumerate(l for l in (ROOT / 'out/ours_trace.txt').read_text(encoding='utf8').splitlines() if '->' in l):
    p = l.split()
    if p[0] == 't1': ours.append((n, p[2], p[1]))
a = int(sys.argv[1]) if len(sys.argv) > 1 else 0
b = int(sys.argv[2]) if len(sys.argv) > 2 else 0
count = int(sys.argv[3]) if len(sys.argv) > 3 else 3
print('root thread ops: real', len(real), 'ours', len(ours))
shown = 0
while a < len(real) and b < len(ours) and shown < count:
    if real[a][1] == ours[b][1]:
        a += 1; b += 1; continue
    shown += 1
    print(f'-- mismatch real[{a}] (row {real[a][0]}) ours[{b}] ({ours[b][2]})')
    print('   real:', [r[1] for r in real[a - 3:a + 12]])
    print('   ours:', [o[1] for o in ours[b - 3:b + 12]])
    # resync: find the next place where 6 consecutive ops agree
    found = False
    for da in range(0, 400):
        for db in range(0, 400):
            if a + da + 6 <= len(real) and b + db + 6 <= len(ours) and all(real[a + da + k][1] == ours[b + db + k][1] for k in range(6)):
                a += da; b += db; found = True; break
        if found: break
    if not found: break
print('stopped at real', a, 'ours', b)
