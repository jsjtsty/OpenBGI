# Multi-thread research VM on top of vm_proto.VM.  Scheduler rules from FUN_0049be00:
#   per frame, walk the thread list in creation order (new threads are appended and run in the same frame);
#   a thread with a wait object runs only when the object reports ready; rc 1/5 next thread, 2 same thread again,
#   3 switch to thread DAT_0054adb0, 4 terminate, 6 next thread and skip every remaining thread this frame.
# usage: vm_mt.py [--frames N] [--steps N] [--out FILE]
import argparse, sys
sys.path.insert(0, __file__.rsplit('\\', 1)[0].rsplit('/', 1)[0] if False else '.')
import vm_proto
from vm_proto import VM, ROOT

FIELDS = ('program', 'modules', 'locals', 'expr', 'ep', 'sp', 'ip', 'calls')

class MT(VM):
    def __init__(self):
        super().__init__()
        self.auto = True
        self.brlog = False
        self.threads = []          # dicts with FIELDS + id/dead/wait/queue
        self.next_id = 1           # Native startup first constructs an omitted sentinel thread (ID 0).
        self.frame = 0
        self.now = 0               # virtual clock, ms
        self.cur = None
        self.skip_frame = False
        self.switch_to = None
        self.events = []
        self.wait_traces = {}
        self.dialog_result = None  # A simulated user choice must be explicit.
        root = {f: getattr(self, f) for f in FIELDS}
        self._add(root)
        self.cur = root

    def _add(self, st):
        st.update(id=self.next_id, dead=False, wait=None, queue=[], deadline=0)
        self.next_id += 1
        self.threads.append(st)

    def save(self):
        for f in FIELDS:
            self.cur[f] = getattr(self, f)

    def load_thread(self, t):
        for f in FIELDS:
            setattr(self, f, t[f])
        self.cur = t

    def log(self, s):
        self.trace.append(s)

    # ---- sub-opcodes that need thread awareness ----
    def subop(self, key):
        t = self.cur
        if key == '80:44':
            stack = self.pop(); prog = self.pop(); slots = self.pop()
            name = self.string(self.pop()); arc = self.string(self.pop())
            self.save()
            old = {f: getattr(self, f) for f in FIELDS}
            self.program = bytearray(); self.modules = []; self.locals = bytearray(max(stack, 0x100)); self.expr = [0] * max(slots, 1)
            self.ep = 0; self.sp = 0; self.ip = 0; self.calls = []
            self.load(arc, name)
            st = {f: getattr(self, f) for f in FIELDS}
            for f in FIELDS:
                setattr(self, f, old[f])
            self._add(st)
            self.push(st['id'])
            self.log(f'  80:44 create thread id={st["id"]} {arc}/{name} stack={stack:#x} prog={prog:#x} slots={slots:#x}')
            return 0
        if key == '80:46':
            self.push(t['id']); self.log(f'  {key} thread id={t["id"]}'); return 0
        if key == '80:47':
            i = self.pop(); self.push(any(x['id'] == i and not x['dead'] for x in self.threads)); self.log(f'  {key} exists {i}'); return 0
        if key == '80:48':
            msg = self.pop(); i = self.pop(); tg = [x for x in self.threads if x['id'] == i]
            if not tg: raise ValueError(f'80:48 no thread {i}')
            tg[0]['queue'].append(msg); self.log(f'  {key} send {msg:#x} -> thread {i}'); return 0
        if key == '80:49':
            addr = self.pop()
            if t['queue']:
                self.write(addr, t['queue'].pop(0), 4); self.push(1)
            else:
                self.push(0)
            self.log(f'  {key} recv -> {self.mem.get(addr, 0) if False else ""}'); return 0
        if key == '80:4A':
            i = self.pop(); n = self.pop(); arr = self.pop()
            tg = [x for x in self.threads if x['id'] == i]
            if not tg: raise ValueError(f'80:4A no thread {i}')
            for k in range(n): tg[0]['queue'].append(self.read(arr + 4 * k, 4))
            self.log(f'  {key} send {n} values -> thread {i}'); return 0
        if key == '80:4B':
            lim = self.pop(); addr = self.pop(); k = 0
            while t['queue'] and k < lim:
                self.write(addr + 4 * k, t['queue'].pop(0), 4); k += 1
            self.push(k); self.log(f'  {key} recv {k}'); return 0
        if key == '80:5E':
            i = self.pop(); self.switch_to = i; self.log(f'  {key} switch to {i}'); return 3
        if key == '80:58':
            v = self.pop(); t['deadline'] = self.now + v; self.log(f'  {key} deadline={t["deadline"]} (now {self.now})'); return 0
        if key == '80:5A':
            rem = t['deadline'] - self.now
            self.push(1 if rem > 0 else 0)
            if rem > 0:
                t['wait'] = ('time', t['deadline'])
            self.log(f'  {key} remaining={rem}'); return 2
        if key == '80:6A':
            self.log('  80:6A yield+skip rest of frame'); return 6
        if key == 'B0:82':
            if self.dialog_result is None:
                raise NotImplementedError('B0:82 needs an explicit simulated dialog result')
            flag=self.pop();mode=self.pop();message=self.pop()
            self.push(self.dialog_result)
            self.log(f'  B0:82 SIMULATED user result={self.dialog_result} mode={mode} flag={flag} message_address={message:#x}; pops=3 pushes=1')
            return 0
        if key == '80:5C':
            priority, enabled, duration = [self.pop() for _ in range(3)]
            duration = vm_proto.s32(duration)
            t['wait'] = ('time_ex', self.now + max(0, duration))
            self.log(f'  80:5C simulated timer duration={duration} input_enabled={enabled} priority={priority}; no-input assumption'); return 2
        return super().subop(key)

    def step(self):
        if self.brlog:
            pc = self.ip; op = self.program[pc]
            if op in (0x15, 0x3b):
                a = self.expr[(self.ep - 1) % len(self.expr)]; b = self.expr[(self.ep - 2) % len(self.expr)]
                self.log(f'  BR {self.modname(pc)} op={op:02X} mode={self.program[pc+1]:02X} top={a:#x} next={b:#x}')
        return super().step()

    def ready(self, t):
        w = t['wait']
        if w is None: return -1
        kind, v = w
        if kind in ('time', 'time_ex'): ok = self.now >= v
        else: ok = self.frame >= v
        return 1 if ok else 0

    def run_frame(self, steps):
        self.skip_frame = False
        i = 0
        executed = 0
        spins = 0
        while i < len(self.threads):
            t = self.threads[i]
            if t['dead']:
                i += 1; continue
            if t['wait'] is not None:
                wait_kind = t['wait'][0]
                r = self.ready(t)
                if r == 0:
                    i += 1; continue
                t['wait'] = None
                if wait_kind == 'time_ex':
                    self.load_thread(t); self.push(0); self.save()
                    self.log(f'  80:5C timer completed thread={t["id"]}, push 0 (simulated no input)')
            elif False:
                pass
            if self.skip_frame:
                i += 1; continue
            self.load_thread(t)
            rc = 0
            n = 0
            while rc == 0 and n < steps:
                pc = self.ip
                try:
                    rc = self.step()
                except Exception as e:
                    self.log(f'STOP thread {t["id"]} {self.modname(pc)} {type(e).__name__}: {e}')
                    self.save(); return 'error'
                n += 1
            executed += n
            self.save()
            if rc == 0:
                self.log(f'STOP thread {t["id"]} step limit'); return 'limit'
            if rc == 4:
                t['dead'] = True; self.log(f'THREAD {t["id"]} exit'); i += 1
            elif rc == 2:
                spins += 1
                if spins > 100000:
                    self.log(f'STOP thread {t["id"]} spinning on rc=2 without a wait object'); return 'spin'
            elif rc == 3:
                tgt = [k for k, x in enumerate(self.threads) if x['id'] == self.switch_to]
                if not tgt: raise ValueError('switch to missing thread')
                i = tgt[0]
            elif rc == 6:
                self.skip_frame = True; i += 1
            else:
                i += 1
        return 'ok'

    def run(self, frames, steps=2000000):
        for f in range(frames):
            self.frame = f
            self.log(f'=== frame {f} t={self.now}ms')
            r = self.run_frame(steps)
            if r != 'ok': return r
            if all(t['dead'] for t in self.threads): self.log('ALL THREADS DEAD'); return 'exit'
            self.now += 16
        return 'frames'

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('--frames', type=int, default=60); ap.add_argument('--steps', type=int, default=2000000); ap.add_argument('--out', default='out/vm_mt_trace.txt'); ap.add_argument('--br', action='store_true'); ap.add_argument('--dialog-result',type=int,choices=(0,1),default=None,help='explicit synthetic dialog outcome; default stops')
    a = ap.parse_args()
    vm = MT(); vm.brlog = a.br; vm.dialog_result=a.dialog_result; r = vm.run(a.frames, a.steps)
    out = ROOT / a.out; out.write_text('\n'.join(vm.trace) + '\n', encoding='utf8')
    print(r, 'frames', vm.frame, 'threads', [(t['id'], t['dead']) for t in vm.threads], 'trace', out, 'lines', len(vm.trace)); print(vm.trace[-1])
