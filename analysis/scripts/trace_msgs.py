# Traces object message queues of the real game: FUN_004511a0 (80:AC enqueue: object id, words) and the 90:BF / 90:BC
# records (FUN_00451f40 / FUN_00451ea0, printed when they change) while injecting keys.  Usage: trace_msgs.py total "t:vkhex,..."
import sys, time, ctypes, ctypes.wintypes as wt, frida
GAME = r"E:\Games\_ac2_trace"
total = float(sys.argv[1]); keys = [(float(a.split(':')[0]), int(a.split(':')[1], 16)) for a in sys.argv[2].split(',')]
JS = """
const exe = Process.getModuleByName('ac2_ML.exe');
const delta = exe.base.sub(ptr(0x400000));
Interceptor.attach(ptr(0x4511a0).add(delta), { onEnter(args) {
  const n = args[0].toInt32(), id = this.context.ecx.add(0x10).readU32(); const w = [];
  if (n > 0 && n <= 256) for (let i = 0; i < Math.min(n, 12); i++) w.push(args[1].add(i * 4).readU32().toString(16));
  send({k: 'enq', obj: this.context.ecx.toString(), id, n, w: w.join(' '), ms: Date.now()}); } });
function rec(va, key, len) { let last = ''; Interceptor.attach(ptr(va).add(delta), { onEnter(a) { this.p = a[0]; this.o = this.context.ecx; },
  onLeave(rv) { const h = Array.from(new Uint8Array(this.p.readByteArray(len))).map(b => b.toString(16).padStart(2, '0')).join('');
    if (h !== last) { last = h; send({k: key, obj: this.o.toString(), hex: h, ms: Date.now()}); } } }); }
Interceptor.attach(ptr(0x49a6d0).add(delta), { onEnter(args) { const t = args[0], size = t.add(0x10).readU32(), ring = t.add(0x18).readPointer(), idx = t.add(0x78).readU32();
  const v = []; for (let i = 1; i <= 5; i++) v.push(ring.add(((idx + size - i) % size) * 4).readU32().toString(16)); send({k: 'r81:30', v: v.join(' '), ms: Date.now()}); } });
rec(0x451f40, 'bf', 12); rec(0x451ea0, 'bc', 24);
"""
pid = frida.spawn([GAME + r"\ac2_ML.exe"], cwd=GAME)
s = frida.attach(pid); sc = s.create_script(JS)
t0 = [None]
def on_msg(m, d):
    if m['type'] != 'send': print(m); return
    p = m['payload']; print(p['ms'] - (t0[0] or p['ms']), {k: v for k, v in p.items() if k != 'ms'}, flush=True)
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
while time.time() - start < total:
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
