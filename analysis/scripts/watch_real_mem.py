# Polls a script-memory (tag 0 global) address in the real game and prints each change.  watch_real_mem.py <hex offset> [seconds] [width]
import sys, time, frida
GAME = r"E:\Games\_ac2_trace"  # hard-linked copy: the real game rewrites BGI.gdb when it runs, never run it in the original folder
off = int(sys.argv[1], 16); secs = float(sys.argv[2]) if len(sys.argv) > 2 else 20; width = int(sys.argv[3]) if len(sys.argv) > 3 else 2
JS = """
const exe = Process.getModuleByName('ac2_ML.exe');
const slot = exe.base.add(0x5485e0 - 0x400000);
let last = null; const t0 = Date.now();
setInterval(() => {
  try {
    const base = slot.readPointer(); if (base.isNull()) return;
    const v = %d === 4 ? base.add(%d).readU32() : base.add(%d).readU16();
    if (v !== last) { send({t: Date.now() - t0, v}); last = v; }
  } catch (e) {}
}, 5);
"""
pid = frida.spawn([GAME + r"\ac2_ML.exe"], cwd=GAME)
s = frida.attach(pid)
sc = s.create_script(JS % (width, off, off))
sc.on("message", lambda m, d: print(m["payload"] if m["type"] == "send" else m))
sc.load(); frida.resume(pid)
time.sleep(secs)
frida.kill(pid)
