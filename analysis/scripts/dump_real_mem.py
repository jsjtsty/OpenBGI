# Dumps a range of the real game's script global memory (tag 0) after a delay.  dump_real_mem.py <hex off> <len> <seconds> <out>
import sys, time, frida
GAME = r"E:\Games\_ac2_trace"  # hard-linked copy: the real game rewrites BGI.gdb when it runs, never run it in the original folder
off = int(sys.argv[1], 16); n = int(sys.argv[2], 0); secs = float(sys.argv[3]); out = sys.argv[4]
JS = """
const exe = Process.getModuleByName('ac2_ML.exe');
rpc.exports = { dump(off, n) { const b = exe.base.add(0x5485e0 - 0x400000).readPointer(); return b.add(off).readByteArray(n); } };
"""
pid = frida.spawn([GAME + r"\ac2_ML.exe"], cwd=GAME)
s = frida.attach(pid)
sc = s.create_script(JS); sc.load(); frida.resume(pid)
time.sleep(secs)
data = sc.exports_sync.dump(off, n)
open(out, "wb").write(data)
frida.kill(pid)
print("saved", len(data))
