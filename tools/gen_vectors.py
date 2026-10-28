# Regenerates tests/vectors/{cbg,bm,sdc}_manifest.tsv from the Python reference decoders in analysis/scripts.
# usage: analysis\.venv\Scripts\python.exe -I tools\gen_vectors.py <cbg|bm|sdc> [game_dir]
import glob, hashlib, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'analysis', 'scripts'))
OUT = os.path.join(ROOT, 'tests', 'vectors')
GAME = sys.argv[2] if len(sys.argv) > 2 else r'E:\Games\Amairo Chocolate 2'
sha = lambda b: hashlib.sha256(bytes(b)).hexdigest()


def members(arc):
    with open(arc, 'rb') as f:
        h = f.read(16)
        n = struct.unpack_from('<I', h, 12)[0]
        d = f.read(n * 128)
        base = 16 + n * 128
        for i in range(n):
            e = d[i * 128:(i + 1) * 128]
            off, size = struct.unpack_from('<II', e, 96)
            yield e[:96].split(b'\0')[0].decode('cp932', 'replace'), base + off, size


def read(arc, off, size):
    with open(arc, 'rb') as f:
        f.seek(off)
        return f.read(size)


def gen_cbg():
    import decode_cbg_v1 as cbg
    cands = []
    for arc in sorted(glob.glob(GAME + r'\*.arc')):
        with open(arc, 'rb') as f:
            for name, off, size in members(arc):
                f.seek(off)
                h = f.read(48)
                if h[:16] == b'CompressedBG___\0' and struct.unpack_from('<H', h, 46)[0] == 1:
                    w, hh, bpp = struct.unpack_from('<HHH', h, 16)
                    cands.append((os.path.basename(arc), name, off, size, w, hh, bpp))
    print(len(cands), 'CBG v1 members')
    rows = ['arc\tname\twidth\theight\tbpp\tsha256_rgba']
    for k, (arc, name, off, size, w, h, bpp) in enumerate(cands):
        if k % 40 or w * h * (bpp // 8) > 5_000_000:
            continue
        w2, h2, bpp2, rgba = cbg.decode(read(os.path.join(GAME, arc), off, size))
        rows.append(f'{arc}\t{name}\t{w2}\t{h2}\t{bpp2}\t{sha(rgba)}')
        print(rows[-1], flush=True)
    open(os.path.join(OUT, 'cbg_manifest.tsv'), 'w', newline='\n', encoding='utf-8').write('\n'.join(rows) + '\n')


def gen_bm():
    import bm_dec
    want = {'ef_shuchu_wh_a.bm': 4, 'shuchu.bm': 6, 'ef_love.bm': 4}
    rows = ['arc\tname\tframe\tsha256_bgra']
    for arc in sorted(glob.glob(GAME + r'\*.arc')):
        for name, off, size in members(arc):
            if name.lower() in want:
                bm = bm_dec.BM(read(arc, off, size))
                for i in range(min(want[name.lower()], bm.n)):
                    rows.append(f'{os.path.basename(arc)}\t{name}\t{i}\t{sha(bm.frame(i).tobytes())}')
                    print(rows[-1], flush=True)
    open(os.path.join(OUT, 'bm_manifest.tsv'), 'w', newline='\n', encoding='utf-8').write('\n'.join(rows) + '\n')


def gen_sdc():
    import sdc_decode
    rows = ['path\tconsumed\tsize\tsha256']
    for rel in ['BGI.gdb', r'全CG存档\BGI.gdb']:
        p = os.path.join(GAME, rel)
        if os.path.exists(p):
            out, osz, ok, end = sdc_decode.sdc_decode(open(p, 'rb').read())
            assert ok and len(out) == osz
            rows.append(f'{rel.replace(chr(92), "/")}\t{end}\t{len(out)}\t{sha(out)}')
            print(rows[-1])
    open(os.path.join(OUT, 'sdc_manifest.tsv'), 'w', newline='\n', encoding='utf-8').write('\n'.join(rows) + '\n')


{'cbg': gen_cbg, 'bm': gen_bm, 'sdc': gen_sdc}[sys.argv[1]]()
