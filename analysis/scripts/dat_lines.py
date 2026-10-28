# Print the source lines mentioning DAT_<addr> inside the given function list.  usage: dat_lines.py <addr> FUN_a FUN_b ...
import re, sys
L = open(r'D:\Projects\C++\OpenBGI\analysis\out\ac2_ML.c', encoding='utf-8', errors='replace').read().replace('\r', '').split('\n')
a = 'DAT_' + sys.argv[1].lower(); fs = set(sys.argv[2:]); cur = None
for l in L:
    m = re.match(r'// ==== ([0-9a-f]{8}) (\S+) ', l)
    if m:
        cur = m.group(2); continue
    if cur in fs and a in l:
        print('  %s: %s' % (cur, l.strip()[:150]))
