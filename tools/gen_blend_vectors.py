# Regenerates tests/vectors/blend_cases.tsv from the reference formulas in analysis/scripts/verify_blend.py.
# Those formulas were checked bit-for-bit against the scalar path of the original exe's kernels (Unicorn).
# usage: analysis\.venv\Scripts\python.exe -I tools\gen_blend_vectors.py
import os, random, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'analysis', 'scripts'))
import verify_blend as vb

# entry point of the original kernel family -> script-level blend mode
MODE = {0x40c240: 0, 0x40c6c0: 1, 0x40dce0: 2, 0x40e560: 3, 0x40e760: 4, 0x40ed50: 5, 0x416e40: 6,
        0x418260: 7, 0x417200: 8, 0x417810: 9, 0x40f170: 0x40, 0x411e40: 0xC1, 0x40d000: 0xF0,
        0x412560: 0xFF, 0x40c040: 0x80}
# aliases that the dispatcher resolves (FUN_0040b9d0): 0x20=1, 0xC0=5, 0x21..0x27 = 2,3,4,6,7,8,9 with P' = 256-P
ALIAS = {1: 0x20, 5: 0xC0}
REVERSED = {2: 0x21, 3: 0x22, 4: 0x23, 6: 0x24, 7: 0x25, 8: 0x26, 9: 0x27}
MASK = {0: 0xFFFF, 1: 0xFFFFFF, 2: 0xFFFFFFFF, 3: 0xFF}


def pack(v):
    return v if isinstance(v, int) else v[0] | v[1] << 8 | v[2] << 16 | v[3] << 24


rows = ['mode\tsf\tdf\tP\tsrc\tdst\texpected']
for name, (entry, sf, df, Ps, ref, args) in vb.M.items():
    mode = MODE[entry]
    for P in Ps:
        for t in range(10):
            rng = random.Random(P * 1000 + t)
            w = rng.choice([1, 2, 3, 8, 9])
            h = rng.choice([1, 3])
            sp = [vb.rnd_pix(sf, rng) for _ in range(w * h)]
            dp = [vb.rnd_pix(df, rng) for _ in range(w * h)]
            for s, d in zip(sp, dp):
                e = ref(s, d, P)
                if e is None:
                    continue
                exp = pack(e if isinstance(e, int) else [x & 255 for x in e]) & MASK[df]
                emit = lambda m, p: rows.append('%x\t%d\t%d\t%d\t%x\t%x\t%x' % (m, sf, df, p, pack(s), pack(d), exp))
                emit(mode, P)
                if mode in ALIAS:
                    emit(ALIAS[mode], P)
                if mode in REVERSED and P <= 256:
                    emit(REVERSED[mode], 256 - P)
rows = list(dict.fromkeys(rows))  # drop exact duplicates
open(os.path.join(ROOT, 'tests', 'vectors', 'blend_cases.tsv'), 'w', newline='\n').write('\n'.join(rows) + '\n')
print(len(rows) - 1, 'cases')
