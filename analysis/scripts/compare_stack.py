# Aligns the root thread of the real trace with obgi_run's (OBGI_LIVE + OBGI_STACK_AT) log op by op and reports the first
# ops whose top-of-stack values differ (addresses >= 0x1000000 are ignored: they are host-specific).
import sys, pathlib
ROOT = pathlib.Path(__file__).resolve().parents[1]
rows = [l.split('\t') for l in (ROOT / 'out/real_trace.tsv').read_text(encoding='utf8').split('\n')[1:] if l]
root = rows[2][1]
real = [r for r in rows if r[1] == root]
ours = []
lines = (ROOT / 'out/ours_stack.txt').read_text(encoding='utf8').splitlines()
for i in range(0, len(lines) - 1, 2):
    st, ev = lines[i], lines[i + 1]
    if not ev.startswith('t1 '): continue
    vals = st.split(':', 1)[1].split()[1:] if False else st.split(': ', 1)[1].split()
    ours.append((ev.split()[2], vals, ev.split()[1]))
skip = int(sys.argv[1]) if len(sys.argv) > 1 else 0
limit = int(sys.argv[2]) if len(sys.argv) > 2 else 10
N = int(sys.argv[3]) if len(sys.argv) > 3 else 3
k, j = 0, 1
j = 0
shown = 0
resyncs = 0
while k < len(ours) and j < len(real) and shown < limit:
    r, o = real[j], ours[k]
    if r[2] != o[0]:
        found = False
        for d in range(0, 600):
            for e in range(0, 600):
                if j + d + 6 <= len(real) and k + e + 6 <= len(ours) and all(real[j + d + q][2] == ours[k + e + q][0] for q in range(6)):
                    found = True; break
            if found: break
        if not found: print('lost sync at ours', k, 'real', j); break
        resyncs += 1
        j += d; k += e
        continue
    a = [x.upper() for x in r[7].split()[:N]]
    b = [x.upper() for x in o[1][:N]]
    if k >= skip and o[0] not in ('80:40', '80:04', '80:02') and any(a[i] != b[i] and int(a[i], 16) < 0x1000000 and int(b[i], 16) < 0x1000000 for i in range(min(len(a), len(b)))):
        shown += 1
        print(f'ours#{k} real#{j} row {r[0]} {o[0]} {o[2]}', 'real', a, 'ours', b)
    j += 1; k += 1
print('reached ours', k, 'real', j, 'resyncs', resyncs)
