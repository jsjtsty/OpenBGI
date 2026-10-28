# For each DAT_xxxxxxxx given, list the functions in out/ac2_ML.c that mention it, with line counts.  usage: dat_readers.py 0052ec54 ...
import re, sys
L = open(r'D:\Projects\C++\OpenBGI\analysis\out\ac2_ML.c', encoding='utf-8', errors='replace').read().replace('\r', '').split('\n')
want = {a.lower(): {} for a in sys.argv[1:]}
cur = None
for l in L:
    m = re.match(r'// ==== ([0-9a-f]{8}) (\S+) ', l)
    if m:
        cur = m.group(2)
        continue
    for a, d in want.items():
        if 'DAT_' + a in l or '_' + a in l.lower() and False:
            d[cur] = d.get(cur, 0) + 1
for a, d in want.items():
    print('DAT_' + a, ' '.join('%s:%d' % (k, v) for k, v in d.items()))
