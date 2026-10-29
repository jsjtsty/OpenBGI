# Traces object message queues of the real game: FUN_004511a0 (80:AC enqueue: object id, words) and the 90:BF / 90:BC
# records (FUN_00451f40 / FUN_00451ea0, printed when they change) while injecting keys.  Usage: trace_msgs.py total "t:vkhex,..."
import sys, time, ctypes, ctypes.wintypes as wt, frida
GAME = r"E:\Games\_ac2_trace"
total = float(sys.argv[1]); keys = [(float(a.split(':')[0]), int(a.split(':')[1], 16)) for a in sys.argv[2].split(',')]
import csv
H = {}
for r in csv.DictReader(open(r"D:/Projects/C++/OpenBGI/analysis/子opcode表.tsv", encoding="utf-8-sig"), delimiter="	"):
    if r["主opcode"]+":"+r["子opcode"] in ("91:9F","91:38","90:58","92:90","90:90","90:3A","90:53","80:1A","80:11"): H[int(r["处理器VA"],16)] = r["主opcode"]+":"+r["子opcode"]
JS = """
const exe = Process.getModuleByName('ac2_ML.exe');
const delta = exe.base.sub(ptr(0x400000));
const H = %s;
for (const va in H) Interceptor.attach(ptr(parseInt(va)).add(delta), { onEnter(args) { this.t = args[0]; this.i = args[0].add(0x78).readU32(); },
  onLeave(rv) { const t = this.t, size = t.add(0x10).readU32(), j = t.add(0x78).readU32(); let d = (j - this.i) %% size; if (d > size / 2) d -= size; send({k: H[va], net: d, ms: Date.now()}); } });

""" % (H,)
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
