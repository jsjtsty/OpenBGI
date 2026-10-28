# Traces the real ac2_ML.exe: every sub-opcode handler's entry and exit with the expression-stack tops (Frida).
# Usage: trace_real.py [seconds] [out.tsv]   (the game dir is only read; the process runs from it)
import csv, sys, time, pathlib, frida

ROOT = pathlib.Path(__file__).resolve().parents[1]
GAME = r"E:\Games\_ac2_trace"  # hard-linked copy: the real game rewrites BGI.gdb when it runs, never run it in the original folder
seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 40
out_path = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "out" / "real_trace.tsv"

handlers = {}
with open(ROOT / "子opcode表.tsv", encoding="utf-8-sig", newline="") as f:
    for r in csv.DictReader(f, delimiter="\t"):
        handlers.setdefault(int(r["处理器VA"], 16), []).append(f'{r["主opcode"]}:{r["子opcode"]}')

JS = """
const handlers = %s;
const exe = Process.getModuleByName('ac2_ML.exe');
const delta = exe.base.sub(ptr(0x400000));
const N = 12;
function snap(t) {
  const size = t.add(0x10).readU32(), ring = t.add(0x18).readPointer(), idx = t.add(0x78).readU32();
  const v = [];
  for (let i = 1; i <= N; i++) { const j = (idx + size - i) %% size; v.push(ring.add(j * 4).readU32()); }
  return [idx, v];
}
let seq = 0;
for (const va in handlers) {
  const names = handlers[va].join('/');
  Interceptor.attach(ptr(parseInt(va)).add(delta), {
    onEnter(args) {
      this.t = args[0];
      try { this.before = snap(this.t); } catch (e) { this.before = null; }
      this.n = seq++;
    },
    onLeave(rv) {
      if (this.n === %DUMP%) { const b = exe.base.add(0x5485e0 - 0x400000).readPointer(); send({dump: this.n}, b.readByteArray(0x2000)); }
      let after = null;
      try { after = snap(this.t); } catch (e) {}
      send({n: this.n, id: names, t: this.t.toString(), rv: rv.toInt32(), b: this.before, a: after, ms: Date.now()});
    }
  });
}
const WATCH = %WATCH%;
let lastv = null;
if (WATCH >= 0) setInterval(() => {
  try { const b = exe.base.add(0x5485e0 - 0x400000).readPointer(); if (b.isNull()) return;
    const v = b.add(WATCH).readU16(); if (v !== lastv) { send({watch: WATCH, v, afterSeq: seq}); lastv = v; } } catch (e) {}
}, 1);
const MON = %MON%;
if (MON >= 0) {
  const arm = () => { try { const b = exe.base.add(0x5485e0 - 0x400000).readPointer(); if (b.isNull()) return false;
      MemoryAccessMonitor.enable({base: b.add(MON & ~0xfff), size: 0x1000}, { onAccess(d) { if (d.operation === 'write') send({mon: d.address.sub(delta).toString(), seq, off: d.rangeIndex}); } });
      return true; } catch (e) { send({err: e.toString()}); return true; } };
  const iv = setInterval(() => { if (arm()) clearInterval(iv); }, 1);
}
send({hello: exe.base.toString(), count: Object.keys(handlers).length});
"""

rows = []
def on_message(msg, data):
    if msg["type"] != "send":
        print(msg); return
    p = msg["payload"]
    if "dump" in p:
        open(ROOT / "out" / "real_mem_at.bin", "wb").write(data); print("dumped at seq", p["dump"]); return
    if "mon" in p or "err" in p:
        print("mon", p); return
    if "watch" in p:
        print("watch", p); return
    if "hello" in p:
        print("attached", p); return
    rows.append(p)

pid = frida.spawn([GAME + r"\ac2_ML.exe"], cwd=GAME)
session = frida.attach(pid)
import os
WATCH = int(os.environ.get("WATCH_OFF", "-1"), 16) if os.environ.get("WATCH_OFF") else -1
JS = JS.replace("%WATCH%", str(WATCH)).replace("%DUMP%", os.environ.get("DUMP_SEQ","-1")).replace("%MON%", str(int(os.environ["MON_OFF"], 16)) if os.environ.get("MON_OFF") else "-1")
script = session.create_script(JS % ("{" + ",".join(f'{va}:{sorted(set(v))!r}'.replace("'", '"') for va, v in handlers.items()) + "}"))
script.on("message", on_message)
script.load()
frida.resume(pid)
t0 = time.time()
while time.time() - t0 < seconds:
    time.sleep(0.5)
try:
    frida.kill(pid)
except Exception:
    pass
with open(out_path, "w", encoding="utf-8", newline="") as f:
    f.write("seq\tthread\tsubop\trv\tidx_before\ttops_before\tidx_after\ttops_after\n")
    for p in sorted(rows, key=lambda r: r["n"]):
        b, a = p["b"], p["a"]
        f.write("\t".join([str(p["n"]), p["t"], p["id"], str(p["rv"]), str(b[0] if b else ""), " ".join(f"{x:X}" for x in b[1]) if b else "", str(a[0] if a else ""), " ".join(f"{x:X}" for x in a[1]) if a else ""]) + "\n")
print("events", len(rows), "->", out_path)
