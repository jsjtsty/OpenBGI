# Prints the full argument lists (top N expression-stack values, top first) of chosen handlers in the real game.
# Usage: trace_args.py seconds N va[,va...]   e.g. trace_args.py 50 17 0x48bf80,0x48abe0
import sys, time, frida
GAME = r"E:\Games\_ac2_trace"  # hard-linked copy, never the original folder
secs, N = float(sys.argv[1]), int(sys.argv[2])
vas = [int(x, 16) for x in sys.argv[3].split(',')]
JS = """
const exe = Process.getModuleByName('ac2_ML.exe');
const delta = exe.base.sub(ptr(0x400000));
for (const va of %s) Interceptor.attach(ptr(va).add(delta), { onEnter(args) {
  const t = args[0], size = t.add(0x10).readU32(), ring = t.add(0x18).readPointer(), idx = t.add(0x78).readU32();
  const v = [];
  for (let i = 1; i <= %d; i++) v.push(ring.add(((idx + size - i) %% size) * 4).readU32().toString(16));
  send({va: va.toString(16), v: v.join(' ')});
} });
""" % (vas, N)
pid = frida.spawn([GAME + r"\ac2_ML.exe"], cwd=GAME)
s = frida.attach(pid)
sc = s.create_script(JS)
sc.on('message', lambda m, d: print(m.get('payload', m), flush=True))
sc.load(); frida.resume(pid)
time.sleep(secs)
frida.kill(pid)
