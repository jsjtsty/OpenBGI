# Prints the records the real game writes for 90:BF (FUN_00451f40, 12 bytes) and 90:BC (FUN_00451ea0, 24 bytes).
# Usage: trace_icon_state.py [seconds]
import sys, time, frida
GAME = r"E:\Games\_ac2_trace"  # hard-linked copy, never the original folder
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 50
JS = """
const exe = Process.getModuleByName('ac2_ML.exe');
const delta = exe.base.sub(ptr(0x400000));
let n = {bf: 0, bc: 0};
function hook(va, key, len) {
  Interceptor.attach(ptr(va).add(delta), {
    onEnter(args) { this.p = args[0]; },
    onLeave(rv) {
      const c = ++n[key];
      if (c <= 4 || c % 1000 === 0) send({key, c, hex: Array.from(new Uint8Array(this.p.readByteArray(len))).map(b => b.toString(16).padStart(2, '0')).join('')});
    }
  });
}
hook(0x451f40, 'bf', 12);
hook(0x451ea0, 'bc', 24);
"""
pid = frida.spawn([GAME + r"\ac2_ML.exe"], cwd=GAME)
s = frida.attach(pid)
sc = s.create_script(JS)
sc.on('message', lambda m, d: print(m.get('payload', m)))
sc.load(); frida.resume(pid)
time.sleep(secs)
frida.kill(pid)
