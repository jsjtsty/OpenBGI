# Compares the root script thread's sub-opcode sequence in the real trace with obgi_run's OBGI_LIVE log, so scheduling
# differences between the threads do not matter.  compare_thread.py [real tsv] [max diffs]
import sys, difflib, pathlib
ROOT = pathlib.Path(__file__).resolve().parents[1]
real_path = ROOT / (sys.argv[1] if len(sys.argv) > 1 else 'out/real_trace.tsv')
limit = int(sys.argv[2]) if len(sys.argv) > 2 else 10
rows = [l.split('\t') for l in real_path.read_text(encoding='utf8').split('\n')[1:] if l]
root = rows[2][1]
real = [(i, r[2], r[7]) for i, r in enumerate(rows) if r[1] == root]
ours = []
for n, l in enumerate(l for l in (ROOT / 'out/ours_trace.txt').read_text(encoding='utf8').splitlines() if '->' in l):
    p = l.split()
    if p[0] == 't1': ours.append((n, p[2], [int(x, 16) for x in p[p.index('->') + 1:]], p[1]))
real, ours = real[:4000], ours[:4000]
sm = difflib.SequenceMatcher(None, [r[1] for r in real], [o[1] for o in ours], autojunk=False)
shown = matched = 0
for tag, i1, i2, j1, j2 in sm.get_opcodes():
    if tag == 'equal':
        matched += i2 - i1
        continue
    if shown < limit:
        shown += 1
        print(tag, f'real#{real[i1][0] if i1 < len(real) else "end"} ours#{ours[j1][0] if j1 < len(ours) else "end"}',
              [r[1] for r in real[i1:i2]][:10], [o[1] for o in ours[j1:j2]][:10], ours[j1][3] if j1 < len(ours) else '')
print('matched', matched, 'of real', len(real), 'ours', len(ours))
