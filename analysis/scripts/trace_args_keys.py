# Like trace_args.py (full argument lists of chosen handlers in the real game) but names handlers as "90:56,90:33" and injects keys.
# Usage: trace_args_keys.py seconds N ops "t:vkhex,..." [filter_hex_first_arg]
#   e.g. trace_args_keys.py 50 12 90:56,90:33,90:5C "30:28,33:0D" 80000002
# Prints "ms op args..." (top of the expression stack first) for every call, optionally only those whose first-popped
# (deepest argument position given by N) ... the filter matches any value in the list.
import sys, time, csv, ctypes, ctypes.wintypes as wt, frida, pathlib
ROOT = pathlib.Path(__file__).resolve().parents[1]
GAME = r"E:\Games\_ac2_trace"
secs, N = float(sys.argv[1]), int(sys.argv[2])
ops = sys.argv[3].split(',')
keys = [(float(a.split(':')[0]), int(a.split(':')[1], 16)) for a in sys.argv[4].split(',')] if len(sys.argv) > 4 and sys.argv[4] else []
flt = sys.argv[5].lower() if len(sys.argv) > 5 else None
H = {}
with open(ROOT / "子opcode表.tsv", encoding="utf-8-sig", newline="") as f:
    for r in csv.DictReader(f, delimiter="\t"):
        k = r["主opcode"].upper() + ":" + r["子opcode"].upper()
        if k in [o.upper() for o in ops]: H[int(r["处理器VA"], 16)] = k
JS = """
const exe = Process.getModuleByName('ac2_ML.exe');
const delta = exe.base.sub(ptr(0x400000));
const H = %s;
for (const va in H) Interceptor.attach(ptr(parseInt(va)).add(delta), { onEnter(args) {
  const t = args[0], size = t.add(0x10).readU32(), ring = t.add(0x18).readPointer(), idx = t.add(0x78).readU32();
  const v = [];
  for (let i = 1; i <= %d; i++) v.push(ring.add(((idx + size - i) %% size) * 4).readU32().toString(16));
  send({op: H[va], v: v.join(' '), ms: Date.now()});
} });
""" % (H, N)
pid = frida.spawn([GAME + r"\ac2_ML.exe"], cwd=GAME)
s = frida.attach(pid); sc = s.create_script(JS)
t0 = [None]
def on_msg(m, d):
    if m['type'] != 'send': print(m); return
    p = m['payload']
    if flt and flt not in p['v'].split(): return
    print(p['ms'] - (t0[0] or p['ms']), p['op'], p['v'], flush=True)
sc.on('message', on_msg); sc.load(); frida.resume(pid)
t0[0] = int(time.time() * 1000)
u = ctypes.windll.user32
def win():
    r = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, l):
        p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h): r.append(h)
        return True
    u.EnumWindows(cb, 0); return r[0] if r else None
start = time.time(); done = set()
while time.time() - start < secs:
    now = time.time() - start
    for i, (t, vk) in enumerate(keys):
        if i in done or now < t: continue
        done.add(i); h = win()
        if h:
            u.SetForegroundWindow(h); time.sleep(0.2)
            print(int(time.time() * 1000) - t0[0], 'KEY', hex(vk), flush=True)
            u.keybd_event(vk, 0, 0, 0); time.sleep(0.08); u.keybd_event(vk, 0, 2, 0)
    time.sleep(0.05)
frida.kill(pid)
