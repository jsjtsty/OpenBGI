# Runs the real game (in _ac2_trace) with every sub-opcode handler hooked, injects input into its window at given times
# and writes a compact log (ms since start, thread, op, return value, top 4 stack values after) to out/input_trace.tsv.
# Usage: trace_input.py total_seconds "t:kind[:arg],..."   kinds: key:<vkhex>  click  move:x:y   (window client coords)
import csv, sys, time, pathlib, ctypes, ctypes.wintypes as wt, frida
ROOT = pathlib.Path(__file__).resolve().parents[1]
GAME = r"E:\Games\_ac2_trace"
total = float(sys.argv[1])
events = []
for e in sys.argv[2].split(','):
    p = e.split(':'); events.append((float(p[0]), p[1], p[2:]))
handlers = {}
with open(ROOT / "子opcode表.tsv", encoding="utf-8-sig", newline="") as f:
    for r in csv.DictReader(f, delimiter="\t"):
        handlers.setdefault(int(r["处理器VA"], 16), []).append(f'{r["主opcode"]}:{r["子opcode"]}')
JS = """
const handlers = %s;
const exe = Process.getModuleByName('ac2_ML.exe');
const delta = exe.base.sub(ptr(0x400000));
for (const va in handlers) {
  const names = handlers[va].join('/');
  Interceptor.attach(ptr(parseInt(va)).add(delta), {
    onEnter(args) { this.t = args[0]; },
    onLeave(rv) {
      const t = this.t; let v = [];
      try { const size = t.add(0x10).readU32(), ring = t.add(0x18).readPointer(), idx = t.add(0x78).readU32();
        for (let i = 1; i <= 4; i++) v.push(ring.add(((idx + size - i) %% size) * 4).readU32().toString(16)); } catch (e) {}
      send({id: names, t: t.toString(), rv: rv.toInt32(), v: v.join(' '), ms: Date.now()});
    }
  });
}
""" % (handlers)
rows = []
t0 = None
def on_msg(m, d):
    if m['type'] != 'send': print(m); return
    p = m['payload']; rows.append((p['ms'], p['t'], p['id'], p['rv'], p['v']))
pid = frida.spawn([GAME + r"\ac2_ML.exe"], cwd=GAME)
s = frida.attach(pid); sc = s.create_script(JS); sc.on('message', on_msg); sc.load(); frida.resume(pid)
u = ctypes.windll.user32
def find_window():
    res = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, l):
        p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h): res.append(h)
        return True
    u.EnumWindows(cb, 0); return res[0] if res else None
start = time.time(); marks = []; done = set()
while time.time() - start < total:
    now = time.time() - start
    for i, (t, kind, a) in enumerate(events):
        if i in done or now < t: continue
        done.add(i); h = find_window()
        if not h: print('no window'); continue
        u.SetForegroundWindow(h); time.sleep(0.2)
        rc = wt.RECT(); u.GetClientRect(h, ctypes.byref(rc)); pt = wt.POINT(0, 0); u.ClientToScreen(h, ctypes.byref(pt))
        if kind == 'key':
            vk = int(a[0], 16); u.keybd_event(vk, 0, 0, 0); time.sleep(0.08); u.keybd_event(vk, 0, 2, 0)
        elif kind in ('click', 'move'):
            x, y = (int(a[0]), int(a[1])) if a else (rc.right // 2, rc.bottom // 2)
            u.SetCursorPos(pt.x + x, pt.y + y); time.sleep(0.15)
            if kind == 'click': u.mouse_event(2, 0, 0, 0, 0); time.sleep(0.08); u.mouse_event(4, 0, 0, 0, 0)
        marks.append((int(time.time() * 1000), kind, a)); print('injected', kind, a, 'client', rc.right, rc.bottom, flush=True)
    time.sleep(0.05)
frida.kill(pid)
with open(ROOT / 'out/input_trace.tsv', 'w', encoding='utf8') as f:
    for m in marks: f.write(f'MARK\t{m[0]}\t{m[1]}\t{m[2]}\n')
    for r in rows: f.write('\t'.join(map(str, r)) + '\n')
print('rows', len(rows), 'marks', marks)
