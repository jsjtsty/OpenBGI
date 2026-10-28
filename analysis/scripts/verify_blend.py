# usage: .venv/Scripts/python.exe scripts/verify_blend.py [name-substring] [-v]
# Runs the REAL blend kernels of ac2_ML.exe under Unicorn on random bitmaps and compares with the reference formulas of 混合公式.md.
import sys, os, random, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emu

def sat(x): return 0 if x < 0 else 255 if x > 255 else x

def px(b, i, f):
    n = emu.BPP[f]
    return list(b[i*n:i*n+n]) if f in (1, 2) else int.from_bytes(b[i*n:i*n+n], 'little')

def rnd_pix(f, rng):
    if f == 0: return rng.choice([0, rng.randrange(1, 0x8000), rng.randrange(0x8000)])
    if f == 3: return rng.randrange(256)
    a = rng.choice([0, 1, 2, rng.randrange(256), 255, 255, rng.randrange(256)])
    p = [rng.randrange(256) for _ in range(3)]
    if rng.random() < 0.1: p = [0, 0, 0]
    return p + [a]

def pack(f, p):
    return struct.pack('<H', p) if f == 0 else bytes([p]) if f == 3 else bytes(p)

M = {}
def test(name, entry, sf, df, Ps, ref, args=lambda P: [P]):
    M[name] = (entry, sf, df, Ps, ref, args)

def r1_21(s, d, P):
    sa = s[3]
    if not sa & 0xfe: return d
    w = ((sa >> 1) * (256 - P)) >> 8
    return [sat(d[c] + (((s[c]-d[c]) * w) >> 7)) for c in range(3)] + [d[3]]
def r1_11(s, d, P):
    w = P >> 1
    return [sat(s[c] + (((d[c]-s[c]) * w) >> 7)) for c in range(4)]
def over_int(s, d, Q):
    # integer model of the scalar tail of cda0 (Q = 256-P; mode 0 uses Q = 256): sa'*256 = sa*Q
    sa = s[3]; da = d[3]
    e = sa*Q; ecx = (da*(0x10000 - e)) >> 8; edi = ecx + e
    if edi == 0: return d
    ws = (e << 8)//edi; wd = (ecx << 8)//edi
    # real kernel ORs a_o with the alpha-lane of the colour blend: (s_a*ws + d_a*wd)>>8 (saturated)
    return [((d[c]*wd + s[c]*ws) >> 8) & 255 for c in range(3)] + [(edi >> 8) | min(255, (sa*ws + da*wd) >> 8)]
def r0_22(s, d, P):
    if s[3] == 0: return d
    if s[3] == 255: return s
    return over_int(s, d, 256)
def r1_22(s, d, P):
    sa = s[3]
    if sa == 0: return d
    return over_int(s, d, 256-P)
def r2_22(s, d, P):
    sa = s[3]
    if not sa & 0xfe: return d
    f = ((sa >> 1) * P) >> 8
    return [min(255, d[c] + ((s[c]*f) >> 7)) for c in range(3)] + [min(255, d[3] + ((s[3]*(P >> 1)) >> 7))]
def r2_21(s, d, P):
    sa = s[3]
    if not sa & 0xfe: return d
    w = ((sa >> 1) * P) >> 8
    return [min(255, d[c] + ((s[c]*w) >> 7)) for c in range(3)] + [d[3]]
def r2_11(s, d, P):
    if s[0]+s[1]+s[2] == 0: return d
    return [min(255, d[c] + ((s[c]*P) >> 8)) for c in range(3)] + [d[3]]
def r2_12(s, d, P):
    if s[0]+s[1]+s[2] == 0: return d
    return [min(255, d[c] + ((s[c]*P) >> 8)) for c in range(3)] + [min(255, d[3]+P)]
def r3_21(s, d, P):
    sa = s[3]
    if not sa & 0xfe: return d
    f = ((sa >> 1) * P) >> 8
    return [max(0, d[c] - ((s[c]*f) >> 7)) for c in range(3)] + [d[3]]
def r4_11(s, d, P):
    return [d[c] + ((((s[c]-256) * (P//2)) * (2*d[c])) >> 16) for c in range(3)] + [d[3]]
def r4_12(s, d, P):
    return [d[c] + (((((s[c]*d[c]) >> 8) - d[c]) * (P >> 1)) >> 7) for c in range(3)] + [d[3]]
def r4_21(s, d, P):
    sa = s[3]
    if not sa & 0xfe: return d
    w = ((sa >> 1) * P) >> 8
    return [d[c] + (((((s[c]*d[c]) >> 8) - d[c]) * w) >> 7) for c in range(3)] + [d[3]]
def r4_33(s, d, P):
    m = ((s*d + 1) * 0x101) >> 16
    return (d + (((m-d) * P) >> 8)) & 255
def r5_11(s, d, P): return [(s[c]*(256-P)) >> 8 for c in range(4)]
def r5_21(s, d, P):
    sa = s[3]
    if not sa & 0xfe: return d
    sp = [(s[c]*(256-P)) >> 8 for c in range(3)]
    return [d[c] + (((sp[c]-d[c]) * (sa >> 1)) >> 7) for c in range(3)] + [d[3]]
def r6_11(s, d, P):
    out = []
    for c in range(3):
        sp = (s[c]*P) >> 8
        out.append(sat(d[c] + sp - ((sp*d[c]) >> 8)))
    return out + [d[3]]
def r6_21(s, d, P):
    sa = s[3]
    if sa == 0: return d
    out = []
    for c in range(3):
        sp = (s[c] * ((sa*P) >> 8)) >> 8
        out.append(sat(d[c] + sp - ((sp*d[c]) >> 8)))
    return out + [d[3]]
def r7_21(s, d, P):
    sa = s[3]
    if sa == 0: return d
    t = 256 - ((sa*P) >> 8)
    return [(d[c]*t) >> 8 for c in range(3)] + [d[3]]
def r7_22(s, d, P):
    sa = s[3]
    if sa == 0: return d
    return d[:3] + [(d[3]*(0x10000 - sa*P)) >> 16] if P < 256 else d[:3] + [(d[3]*(256-sa)) >> 8]
def r7_23(s, d, P):
    sa = s[3]
    if sa == 0: return d
    return (d*(0xff00 - sa*P)) >> 16 if P < 256 else (d*(255-sa)) >> 8
def mix89(hard):
    def f(s, d, P):
        out = []
        for c in range(3):
            m = (s[c] > 127) if hard else (d[c] > 127)
            r = 2*(s[c]+d[c]) - 255 - ((s[c]*d[c]) >> 7) if m else (s[c]*d[c]) >> 7
            out.append(sat(d[c] + (((r-d[c]) * P) >> 8)))
        return out + [d[3]]
    return f
def r40_22(s, d, P): return [0, 0, 0, 0] if ((s[3] != 0) if P else (s[3] == 255)) else d
def r40_11(s, d, P): return [0, 0, 0, 0] if s[0]+s[1]+s[2] else d
def r40_00(s, d, P): return 0 if s else d
def rC1_11(s, d, P): return [(s[c]*(256-P) + 255*P) >> 8 for c in range(3)] + [s[3]]
def rFF(s, d, P):
    return [s[0], 0, 0, 255] if P == 0 else [0, s[1], 0, 255] if P == 1 else [0, 0, s[2], 255] if P == 2 else [s[3], s[3], s[3], 255]
def r80_12(s, d, P): return s[:3] + [255]
def r80_21(s, d, P): return [(s[c]*(s[3] >> 1)) >> 7 for c in range(4)]
def r80_31(s, d, P): return [0, 0, 0, s]

def ch16(v): return ((v >> 10) & 31, (v >> 5) & 31, v & 31)
def p16(r, g, b): return (r << 10) | (g << 5) | b
def r0_00(s, d, P): return d if s == 0 else s
def lerp16(s, d, ws, wd):
    return p16(*[min(31, (a*ws + b*wd) >> 8) for a, b in zip(ch16(s), ch16(d))])
def r1_00(s, d, P): return d if s == 0 else lerp16(s, d, 256-P, P)
def r2_00(s, d, P):
    if s == 0: return d
    return p16(*[min(31, ((a*P) >> 8) + b) for a, b in zip(ch16(s), ch16(d))])
def r5_00(s, d, P): return p16(*[(a*(256-P)) >> 8 for a in ch16(s)])
def rF0_00(s, d, P): return lerp16(s, d, 256-P, P)
def r0_11(s, d, P): return s
def r0_12(s, d, P): return s[:3] + [255]
def r0_21(s, d, P):  return None
def r1_12(s, d, P):
    e = 255*(256-P); ecx = (d[3]*(0x10000 - e)) >> 8; edi = ecx + e   # no alpha-OR quirk in c940
    if edi == 0: return d
    ws = (e << 16)//edi; wd = (ecx << 16)//edi
    return [((s[c]*ws + d[c]*wd) >> 16) & 255 for c in range(3)] + [edi >> 8]
def r5_22(s, d, P):
    sa = s[3]
    if sa == 0: return d
    Q = 256-P; den = 256*sa + (256-sa)*d[3]
    ws = ((sa*Q) << 16)//den; wd = (((256-sa)*d[3]) << 16)//den
    return [(s[c]*ws + d[c]*wd) >> 16 for c in range(3)] + [den >> 8]

test('mode1 2->1 (ca80)', 0x40c6c0, 2, 1, [1, 2, 64, 127, 128, 200, 255], r1_21)
test('mode1 1->1 (c850)', 0x40c6c0, 1, 1, [1, 64, 128, 200, 255], r1_11)
test('mode1 2->2 (cda0)', 0x40c6c0, 2, 2, [1, 64, 128, 200, 255], r1_22)
test('mode0 2->2 (c480)', 0x40c240, 2, 2, [0], r0_22, lambda P: [])
test('mode2 2->2 (e280)', 0x40dce0, 2, 2, [1, 64, 128, 255, 256], r2_22)
test('mode2 2->1 (e0b0)', 0x40dce0, 2, 1, [1, 64, 128, 255, 256], r2_21)
test('mode2 1->1 (de90)', 0x40dce0, 1, 1, [1, 64, 128, 255, 256], r2_11)
test('mode2 1->2 (df90)', 0x40dce0, 1, 2, [1, 64, 128, 255, 256], r2_12)
test('mode3 2->1 (e590)', 0x40e560, 2, 1, [1, 64, 128, 255, 256], r3_21)
test('mode4 1->1 (e7d0)', 0x40e760, 1, 1, [2, 64, 128, 255, 256], r4_11)
test('mode4 1->2 (e8f0)', 0x40e760, 1, 2, [2, 64, 128, 255, 256], r4_12)
test('mode4 2->1 (ea00)', 0x40e760, 2, 1, [2, 64, 128, 255, 256], r4_21)
test('mode4 3->3 (ebe0)', 0x40e760, 3, 3, [2, 64, 128, 255, 256], r4_33)
test('mode5 1->1 (ede0)', 0x40ed50, 1, 1, [1, 64, 128, 255], r5_11)
test('mode5 2->1 (eec0)', 0x40ed50, 2, 1, [1, 64, 128, 255], r5_21)
test('mode6 1->1 (6e80)', 0x416e40, 1, 1, [1, 64, 128, 255, 256], r6_11)
test('mode6 2->1 (6fd0)', 0x416e40, 2, 1, [1, 64, 128, 255, 256], r6_21)
test('mode7 2->1 (82d0)', 0x418260, 2, 1, [1, 64, 128, 255, 256], r7_21)
test('mode7 2->2 (84b0/8620)', 0x418260, 2, 2, [1, 64, 128, 255, 256], r7_22)
test('mode7 2->3 (8770/88b0)', 0x418260, 2, 3, [1, 64, 128, 255, 256], r7_23)
test('mode8 1->1 (73c0)', 0x417200, 1, 1, [1, 64, 128, 255], mix89(False))
test('mode9 1->1 (79d0)', 0x417810, 1, 1, [1, 64, 128, 255], mix89(True))
test('mode0x40 2->2 P=0', 0x40f170, 2, 2, [0], r40_22)
test('mode0x40 2->2 P!=0', 0x40f170, 2, 2, [5], r40_22)
test('mode0x40 1->1', 0x40f170, 1, 1, [1], r40_11)
test('mode0x40 0->0', 0x40f170, 0, 0, [1], r40_00)
test('mode0xC1 1->1', 0x411e40, 1, 1, [1, 64, 128, 255], rC1_11, lambda P: [0xffffff, P])
test('mode0xF0 1->1', 0x40d000, 1, 1, [1, 64, 128, 255], r1_11)
test('mode0xFF 2->1', 0x412560, 2, 1, [0, 1, 2, 3], rFF)
test('mode0x80 1->2', 0x40c040, 1, 2, [0], r80_12)
test('mode0x80 2->1 (c140)', 0x40c040, 2, 1, [0], r80_21)
test('mode0x80 3->2 (2100)', 0x40c040, 3, 2, [0], r80_31)
test('mode0 0->0 (c290)', 0x40c240, 0, 0, [0], r0_00, lambda P: [])
test('mode1 0->0 (c750)', 0x40c6c0, 0, 0, [1, 64, 128, 255], r1_00)
test('mode2 0->0 (dd60)', 0x40dce0, 0, 0, [1, 64, 128, 255, 256], r2_00)
test('mode5 0->0 (1e90)', 0x40ed50, 0, 0, [1, 64, 128, 255], r5_00)
test('mode0xF0 0->0 (d050)', 0x40d000, 0, 0, [1, 64, 128, 255], rF0_00)
test('mode0 1->1 (be00)', 0x40c240, 1, 1, [0], r0_11, lambda P: [])
test('mode0 1->2 (c090)', 0x40c240, 1, 2, [0], r0_12, lambda P: [])
test('mode1 1->2 (c940)', 0x40c6c0, 1, 2, [1, 64, 128, 255], r1_12)
test('mode5 2->2 (f040)', 0x40ed50, 2, 2, [1, 64, 128, 255], r5_22)
test('mode0 2->1 (c300)', 0x40c240, 2, 1, [0], lambda s, d, P: (s[:3] + [d[3]]) if s[3] == 255 else r1_21(s, d, 0), lambda P: [])

def run(filt, trials=30):
    k = emu.Kernel()
    for name, (entry, sf, df, Ps, ref, args) in M.items():
        if filt and filt not in name: continue
        bad = tot = mx = 0; ex = None; mxs = mxv = 0
        for P in Ps:
            for t in range(trials):
                rng = random.Random(P*1000+t); w = rng.choice([1, 2, 3, 8, 9]); sc = (w == 1); h = rng.choice([1, 3])
                sp = [rnd_pix(sf, rng) for _ in range(w*h)]; dp = [rnd_pix(df, rng) for _ in range(w*h)]
                S, _ = k.bitmap(b''.join(pack(sf, p) for p in sp), w, h, sf)
                D, da = k.bitmap(b''.join(pack(df, p) for p in dp), w, h, df)
                k.call(entry, D, S, *args(P))
                out = k.read(da, w*h*emu.BPP[df])
                for i in range(w*h):
                    got = px(out, i, df); exp = ref(sp[i], dp[i], P); tot += 1
                    if isinstance(exp, list):
                        n = 3 if df == 1 else 4  # alpha byte of a no-alpha (fmt 1) bitmap is don't-care
                        diff = max(abs(a-b) for a, b in zip(got[:n], exp[:n])) if isinstance(got, list) else 99
                    else: diff = abs(got-exp)
                    if df == 2 and name[:5] in ('mode0', 'mode1') and isinstance(exp, list) and exp[3] == 0 and got[3] == 0: diff = 0  # alpha-0 pixel: colour is invisible
                    mx = max(mx, diff)
                    if sc: mxs = max(mxs, diff)
                    elif w % 2 == 0: mxv = max(mxv, diff)
                    if diff:
                        bad += 1
                        if ex is None or diff > ex[0]: ex = (diff, P, sp[i], dp[i], got, exp)
        print('%-26s %s px=%d mismatch=%d maxdiff=%d (scalar-only %d, simd-only-even-width %d)' % (name, 'OK  ' if bad == 0 else 'DIFF', tot, bad, mx, mxs, mxv))
        if ex and (mx > 1 or '-v' in sys.argv): print('    worst: diff=%d P=%d s=%s d=%s got=%s exp=%s' % ex)

if __name__ == '__main__':
    a = [x for x in sys.argv[1:] if not x.startswith('-')]
    run(a[0] if a else None)
