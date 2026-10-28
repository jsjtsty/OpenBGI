"""Task 5 native execution host (Python 3 / Unicorn).
Only Win32 imports are substituted. No VM/manager/CRT return-value hooks.
VA: CThread 0044c880, scheduler 0049be00, loader 0044ce40.
Used by truth5_boot.py and truth5_verify.py. Game files are always read-only.
"""
import pathlib, struct, ctypes, io, sys, fnmatch
if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf8')
import emu, winstub
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import *

ROOT = pathlib.Path(__file__).resolve().parents[1]
GAME = pathlib.Path(emu.EXE).parent
P = lambda n: struct.pack('<I', n & 0xffffffff)

class Host:
    def __init__(self):
        self.k = emu.Kernel(); self.u = self.k.u; self.win = winstub.Win(self.k)
        # Undo emu.py's non-OS security-cookie RET patch. Keep the real check.
        executable=pathlib.Path(emu.EXE).read_bytes();pe=struct.unpack_from('<I',executable,0x3c)[0]
        section_count=struct.unpack_from('<H',executable,pe+6)[0];opt=struct.unpack_from('<H',executable,pe+20)[0]
        for i in range(section_count):
            vs,va,rs,ra=struct.unpack_from('<IIII',executable,pe+24+opt+i*40+8)
            rva=0x4cf4f7-emu.BASE
            if va<=rva<va+max(vs,rs):self.u.mem_write(0x4cf4f7,executable[ra+rva-va:ra+rva-va+16]);break
        self.now = 0; self.handles = {}; self.sizes = {}; self.next_handle = 100
        self.last_error = 0; self.tls = {}; self.messages = []
        self.virtual={};self.clipboard=0
        self.mapping_next=0x60000000;self.mappings={};self.heap_mapped_end=0x34000000
        self.recycling=False;self.free_blocks=[];self.arena_limit=emu.RET;self.spilled=False
        self.searches={}
        # CRT heap handle normally obtained from GetProcessHeap during startup.
        self.put(0x534550, 1)
        for name, count, value in [('EnterCriticalSection',1,0),('LeaveCriticalSection',1,0),
            ('InitializeCriticalSection',1,0),('DeleteCriticalSection',1,0),
            ('InitializeCriticalSectionAndSpinCount',2,1),('TryEnterCriticalSection',1,1),
            ('GetProcessHeap',0,1),('HeapCreate',3,1),('HeapFree',3,1),
            ('GetCurrentThreadId',0,1),('GetCurrentProcessId',0,1),
            ('GetACP',0,932),('GetOEMCP',0,932),('GetUserDefaultLCID',0,0x411),
            ('GetSystemDefaultLCID',0,0x411),('IsDebuggerPresent',0,0),
            ('Sleep',1,0),('CloseHandle',1,1)]:
            self.win.api(name,count)(lambda a,v=value:v)
        self.win.api('HeapAlloc',3)(lambda a:self.alloc(a[2]))
        self.win.api('HeapFree',3)(self.heap_free)
        self.win.api('HeapSize',3)(lambda a:self.sizes.get(a[2],0xffffffff))
        self.win.api('HeapReAlloc',4)(self.realloc)
        self.win.api('GetTickCount',0)(lambda a:self.now)
        self.win.api('timeGetTime',0)(lambda a:self.now)
        self.win.api('GetLastError',0)(lambda a:self.last_error)
        self.win.api('SetLastError',1)(lambda a:setattr(self,'last_error',a[0]))
        self.win.api('TlsAlloc',0)(lambda a:len(self.tls)+1)
        self.win.api('TlsGetValue',1)(lambda a:self.tls.get(a[0],0))
        self.win.api('TlsSetValue',2)(lambda a:self.tls.update({a[0]:a[1]}) or 1)
        self.win.api('CreateFileW',7)(self.open_file)
        self.win.api('ReadFile',5)(self.read_file)
        self.win.api('WriteFile',5)(self.write_file)
        self.win.api('GetFileSize',2)(self.file_size)
        self.win.api('SetFilePointer',4)(self.seek_file)
        self.win.api('CreateFileMappingW',6)(self.create_mapping)
        self.win.api('CreateFileMappingA',6)(self.create_mapping)
        self.win.api('MapViewOfFile',5)(lambda a:self.mappings[a[0]][0]+a[3])
        self.win.api('UnmapViewOfFile',1)(lambda a:1)
        self.win.api('GetFileAttributesW',1)(self.attributes)
        self.win.api('FindFirstFileW',2)(self.find_first)
        self.win.api('FindNextFileW',2)(self.find_next)
        self.win.api('FindClose',1)(lambda a:self.searches.pop(a[0],None) is not None)
        self.win.api('MultiByteToWideChar',6)(self.to_wide)
        self.win.api('WideCharToMultiByte',8)(self.to_narrow)
        self.win.api('IsDBCSLeadByteEx',2)(lambda a:int(0x81<=a[1]<=0x9f or 0xe0<=a[1]<=0xfc))
        self.win.api('GetSystemMetrics',1)(lambda a:{0:1920,1:1080,16:1920,17:1040,80:1}.get(a[0],0))
        self.win.api('MessageBoxW',4)(self.message)
        self.win.api('GlobalMemoryStatus',1)(self.memory_status)
        self.win.api('GetVersionExA',1)(self.version)
        self.win.api('GetVersionExW',1)(self.version)
        for name,n,value in [('CreateWaitableTimerA',3,0x7001),('SetWaitableTimer',6,1),
                             ('WaitForSingleObject',2,0),('CancelWaitableTimer',1,1),
                             ('CreateEventW',4,0x7002),('SetEvent',1,1),('ResetEvent',1,1)]:
            self.win.api(name,n)(lambda a,v=value:v)
        self.win.api('GetSystemMenu',2)(lambda a:0x7003)
        self.win.api('EnableMenuItem',3)(lambda a:0)
        self.win.api('GetLocalTime',1)(self.local_time)
        self.win.api('GetSystemTime',1)(lambda a:self.u.mem_write(a[0],struct.pack('<8H',2026,10,3,28,4,0,self.now//1000%60,self.now%1000)))
        self.win.api('LoadLibraryExW',3)(lambda a:0)
        self.win.api('GetProcAddress',2)(lambda a:0)
        # Native CRT initialization of encoded null WinAPI thunk cache.
        self.call(0x4e54be)
        # Original .CRT initializer: legacy wide %s specifiers, required by
        # the engine's native wide formatting wrappers (otherwise paths become EU).
        self.call(0x4d07af)
        self.win.api('CreateEventA',4)(lambda a:0x7002)
        self.win.api('CreateDirectoryW',2)(lambda a:1) # virtual directory only; never writes host filesystem
        self.win.api('GetDC',1)(lambda a:0x7100)
        self.win.api('ReleaseDC',2)(lambda a:1)
        # Real host GDI enumeration; original x86 callback executes in Unicorn.
        enum_entry=self.k.alloc(b'\xf4')
        self.put(self.win.iat['EnumFontFamiliesExW'],enum_entry)
        self.u.hook_add(UC_HOOK_CODE,self.enum_fonts,None,enum_entry,enum_entry)
        self.win.api('GetDeviceCaps',2)(lambda a:{8:1920,10:1080,12:32,14:1,88:96,90:96}.get(a[1],0))
        # Process configuration normally populated before FUN_0047b1c0.
        base=(str(GAME)+'\\').encode('utf8')+b'\0'
        self.u.mem_write(0x547db8,base);self.u.mem_write(0x5482d0,base)
        self.win.api('MonitorFromWindow',2)(lambda a:0x7200)
        self.win.api('GetMonitorInfoA',2)(self.monitor_info)
        self.win.api('GetWindowRect',2)(self.window_rect)
        self.win.api('GetClientRect',2)(self.window_rect)
        self.win.api('ScreenToClient',2)(lambda a:1)
        self.win.api('AdjustWindowRectEx',4)(lambda a:1)
        self.win.api('GetWindowLongW',2)(lambda a:0)
        self.win.api('GetWindowLongA',2)(lambda a:0)
        self.win.api('RegOpenKeyExW',5)(lambda a:2) # empty virtual registry
        self.win.api('RegQueryValueExW',6)(lambda a:2)
        self.win.api('RegCloseKey',1)(lambda a:0)
        self.win.api('RegCreateKeyExW',9)(lambda a:self.put(a[7],0x7400) or 0)
        self.win.api('RegSetValueExW',6)(lambda a:0)
        self.win.api('DialogBoxParamW',5)(lambda a:0) # cancel virtual dialog
        self.win.api('GlobalAlloc',2)(lambda a:self.alloc(a[1]))
        self.win.api('GlobalLock',1)(lambda a:a[0])
        self.win.api('GlobalUnlock',1)(lambda a:0)
        self.win.api('GlobalFree',1)(lambda a:0)
        self.win.api('OpenClipboard',1)(lambda a:1)
        self.win.api('EmptyClipboard',0)(lambda a:setattr(self,'clipboard',0) or 1)
        self.win.api('SetClipboardData',2)(lambda a:setattr(self,'clipboard',a[1]) or a[1])
        self.win.api('CloseClipboard',0)(lambda a:1)
        self.win.api('GetStockObject',1)(lambda a:ctypes.windll.gdi32.GetStockObject(a[0]))
        self.win.api('GetObjectW',3)(self.gdi_object)
        self.win.api('DeleteObject',1)(lambda a:1)
        self.win.api('LoadCursorW',2)(lambda a:0x7300)
        self.win.api('LoadCursorA',2)(lambda a:0x7300)
        self.win.api('SetCursor',1)(lambda a:0x7300)
        self.win.api('ShowCursor',1)(lambda a:0)
        for name,n,value in [('FillRect',3,1),('InvalidateRect',3,1),('UpdateWindow',1,1),
            ('SetWindowPos',7,1),('MoveWindow',6,1),('ShowWindow',2,1),('SetWindowTextW',2,1),
            ('SetWindowLongW',3,0),('SetWindowLongA',3,0),('GetMenu',1,0),
            ('DrawMenuBar',1,1),('SendMessageW',4,0),('PostMessageW',4,1),
            ('IsWindow',1,1),('IsIconic',1,0),('GetForegroundWindow',0,0),
            ('GetActiveWindow',0,0),('GetFocus',0,0),('SetFocus',1,0),
            ('GetAsyncKeyState',1,0),('GetKeyState',1,0),('SetCapture',1,0),('ReleaseCapture',0,1)]:
            self.win.api(name,n)(lambda a,v=value:v)

    def get(self,a): return struct.unpack('<I',self.k.read(a,4))[0]
    def put(self,a,n): self.u.mem_write(a,P(n))
    def alloc(self,n):
        if n>0x2000000: raise RuntimeError(f'host allocation limit {n:x}')
        if self.recycling:
            choices=[(size,index) for index,(address,size) in enumerate(self.free_blocks) if size>=max(n,1)]
            if choices:
                size,index=min(choices);address,size=self.free_blocks.pop(index);used=(max(n,1)+15)&~15
                if size-used>=64:self.free_blocks.append((address+used,size-used));size=used
                self.u.mem_write(address,bytes(size));self.sizes[address]=size;return address
        need=(self.k.heap+max(n,1)+0xffff)&~0xffff
        if need>=self.arena_limit and self.recycling and not self.spilled:
            self.k.heap=0x10000000;self.heap_mapped_end=0x10000000;self.arena_limit=emu.STACK;self.spilled=True
            need=(self.k.heap+max(n,1)+0xffff)&~0xffff
        if need>self.heap_mapped_end:
            if need>=self.arena_limit:raise RuntimeError('host heap exhausted before protected region')
            self.u.mem_map(self.heap_mapped_end,need-self.heap_mapped_end);self.heap_mapped_end=need
        a=self.k.alloc(bytes(max(n,1))); self.sizes[a]=n; return a
    def heap_free(self,a):
        if not self.recycling:return 1
        address=a[2]
        if not address:return 1
        size=self.sizes.pop(address,None)
        if size is None:return 0
        self.free_blocks.append((address,size));self.free_blocks.sort()
        merged=[]
        for start,length in self.free_blocks:
            if merged and merged[-1][0]+merged[-1][1]==start:merged[-1]=(merged[-1][0],merged[-1][1]+length)
            else:merged.append((start,length))
        self.free_blocks=merged;return 1
    def realloc(self,a):
        p=self.alloc(a[3]); self.u.mem_write(p,self.k.read(a[2],min(self.sizes.get(a[2],0),a[3])))
        if self.recycling:self.heap_free([a[0],0,a[2]])
        return p
    def string(self,a,wide=False):
        if not a:return ''
        data=bytearray(); step=2 if wide else 1
        for i in range(32768):
            b=self.k.read(a+i*step,step)
            if b==bytes(step):return data.decode('utf-16le' if wide else 'cp932','replace')
            data.extend(b)
        raise RuntimeError('unterminated string')
    def path(self,a):
        s=self.string(a,True).replace('\\','/')
        p=pathlib.Path(s); p=p if p.is_absolute() else GAME/p
        if p.suffix.lower()=='.sud': return None
        if not p.resolve().is_relative_to(GAME.resolve()): raise RuntimeError(f'path outside game: {p}')
        return p
    def open_file(self,a):
        p=self.path(a[0])
        if a[1]&0x40000000 or a[4]!=3:
            if p is None:raise RuntimeError('save write disabled')
            self.next_handle+=1;f=io.BytesIO();self.virtual[str(p)]=f;self.handles[self.next_handle]=f;return self.next_handle
        if p is not None and str(p) in self.virtual:
            self.next_handle+=1;self.handles[self.next_handle]=io.BytesIO(self.virtual[str(p)].getvalue());return self.next_handle
        if p is None or not p.is_file():self.last_error=2;return 0xffffffff
        self.next_handle+=1; self.handles[self.next_handle]=p.open('rb');return self.next_handle
    def read_file(self,a):
        b=self.handles[a[0]].read(a[2]);self.u.mem_write(a[1],b)
        if a[3]:self.put(a[3],len(b))
        return 1
    def write_file(self,a):
        f=self.handles[a[0]]
        if not isinstance(f,io.BytesIO):raise RuntimeError('write to real game handle denied')
        n=f.write(self.k.read(a[1],a[2]));self.put(a[3],n);return 1
    def file_size(self,a):
        f=self.handles[a[0]]; n=len(f.getvalue()) if isinstance(f,io.BytesIO) else pathlib.Path(f.name).stat().st_size
        if a[1]:self.put(a[1],n>>32)
        return n&0xffffffff
    def seek_file(self,a):
        off=a[1]-(0x100000000 if a[1]&0x80000000 else 0)
        return self.handles[a[0]].seek(off,a[3])
    def create_mapping(self,a):
        size=a[4]|(a[3]<<32)
        if size>0x4000000:raise RuntimeError('mapping size exceeds experiment limit')
        size=(max(size,1)+0xffff)&~0xffff;address=self.mapping_next;self.mapping_next+=size
        self.u.mem_map(address,size);self.next_handle+=1;self.mappings[self.next_handle]=(address,size)
        if a[0]!=0xffffffff:
            f=self.handles[a[0]];pos=f.tell();f.seek(0);data=f.read(size);f.seek(pos);self.u.mem_write(address,data)
        self.last_error=0;return self.next_handle
    def attributes(self,a):
        p=self.path(a[0]);return (16 if p.is_dir() else 32) if p and p.exists() else 0xffffffff
    def find_data(self,address,path):
        st=path.stat();ft=lambda n:n//100+116444736000000000
        raw=struct.pack('<IQQQIIII',16 if path.is_dir() else 32,
            ft(getattr(st,'st_birthtime_ns',st.st_ctime_ns)),ft(st.st_atime_ns),ft(st.st_mtime_ns),
            st.st_size>>32,st.st_size&0xffffffff,0,0)
        raw+=(path.name.encode('utf-16le')+b'\0\0').ljust(520,b'\0')[:520]+bytes(28)
        self.u.mem_write(address,raw)
    def find_first(self,a):
        path=self.path(a[0]);entries=[]
        if path and path.parent.is_dir():
            entries=sorted((p for p in path.parent.iterdir() if p.suffix.lower()!='.sud' and fnmatch.fnmatchcase(p.name.casefold(),path.name.casefold())),key=lambda p:p.name.casefold())
        if not entries:self.last_error=2;return 0xffffffff
        self.next_handle+=1;self.searches[self.next_handle]=iter(entries[1:]);self.find_data(a[1],entries[0]);return self.next_handle
    def find_next(self,a):
        path=next(self.searches[a[0]],None)
        if path is None:self.last_error=18;return 0
        self.find_data(a[1],path);return 1
    def to_wide(self,a):
        if a[3]==0xffffffff:
            b=bytearray();p=a[2]
            while True:
                c=self.k.read(p,1);b.extend(c);p+=1
                if c==b'\0':break
            b=bytes(b)
        else:b=self.k.read(a[2],a[3])
        out=b.decode('utf8' if a[0]==65001 else 'cp932','replace').encode('utf-16le')
        if a[4] and a[5]:self.u.mem_write(a[4],out[:a[5]*2])
        return len(out)//2
    def to_narrow(self,a):
        s=self.string(a[2],True)+'\0' if a[3]==0xffffffff else self.k.read(a[2],a[3]*2).decode('utf-16le')
        out=s.encode('utf8' if a[0]==65001 else 'cp932','replace')
        if a[4] and a[5]:self.u.mem_write(a[4],out[:a[5]])
        return len(out)
    def message(self,a):
        self.messages.append((self.string(a[1],True),self.string(a[2],True),a[3]));return 1
    def memory_status(self,a):
        self.u.mem_write(a[0],struct.pack('<8I',32,50,0x80000000,0x40000000,0,0,0,0));return 0
    def version(self,a):
        self.u.mem_write(a[0]+4,struct.pack('<4I',6,1,7601,2));return 1
    def local_time(self,a):
        self.u.mem_write(a[0],struct.pack('<8H',2026,10,3,28,12,0,self.now//1000%60,self.now%1000))
    def monitor_info(self,a):
        self.u.mem_write(a[1],struct.pack('<10I',40,0,0,1920,1080,0,0,1920,1040,1));return 1
    def window_rect(self,a):
        self.u.mem_write(a[1],struct.pack('<4I',0,0,1280,720));return 1
    def gdi_object(self,a):
        buf=ctypes.create_string_buffer(a[1]);n=ctypes.windll.gdi32.GetObjectW(a[0],a[1],buf)
        if n:self.u.mem_write(a[2],buf.raw[:n])
        return n
    def os_callback(self,name,n,fn):
        address=0x4100f000+len([x for x in self.win.names if x>=0x4100f000])*16
        self.win.names[address]=name;self.win.api(name,n)(fn);self.u.mem_write(address,b'\xf4');return address
    def display_mode(self,a):
        self.u.mem_write(a[2],struct.pack('<4I',1920,1080,60,22));return 0
    def enum_fonts(self,u,address,size,_):
        sp=u.reg_read(UC_X86_REG_ESP);args=[self.get(sp+4+i*4) for i in range(5)]
        gdi=ctypes.WinDLL('gdi32');user=ctypes.WinDLL('user32')
        user.GetDC.restype=ctypes.c_void_p;user.GetDC.argtypes=[ctypes.c_void_p]
        user.ReleaseDC.argtypes=[ctypes.c_void_p,ctypes.c_void_p]
        cbtype=ctypes.WINFUNCTYPE(ctypes.c_int,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint,ctypes.c_ssize_t)
        records=[]
        @cbtype
        def collect(lf,tm,kind,param):
            records.append((ctypes.string_at(lf,348),ctypes.string_at(tm,100),kind));return 1
        query=ctypes.create_string_buffer(self.k.read(args[1],92))
        gdi.EnumFontFamiliesExW.argtypes=[ctypes.c_void_p,ctypes.c_void_p,cbtype,ctypes.c_ssize_t,ctypes.c_uint]
        dc=user.GetDC(None);gdi.EnumFontFamiliesExW(dc,query,collect,0,0);user.ReleaseDC(None,dc)
        self.win.log.append(('EnumFontFamiliesExW:host-records',[len(records)]))
        code=bytearray(b'\x55\x8b\xec\xb8\x01\0\0\0');jumps=[]
        for lf,tm,kind in records:
            for value in (args[3],kind,self.k.alloc(tm),self.k.alloc(lf)):code+=b'\x68'+P(value)
            code+=b'\xb8'+P(args[2])+b'\xff\xd0\x85\xc0\x0f\x84';jumps.append(len(code));code+=bytes(4)
        end=len(code);code+=b'\x8b\xe5\x5d\xc2\x14\0'
        for j in jumps:code[j:j+4]=P(end-(j+4))
        trampoline=self.k.alloc(code);u.reg_write(UC_X86_REG_EIP,trampoline)
    def call(self,va,ecx=0,edx=0,*args,count=3000000):
        sp=emu.STACK+0x80000-4*(len(args)+1)
        self.u.mem_write(sp,P(emu.RET)+b''.join(P(x) for x in args))
        for r,v in [(UC_X86_REG_ESP,sp),(UC_X86_REG_EBP,0),(UC_X86_REG_ECX,ecx),(UC_X86_REG_EDX,edx)]:self.u.reg_write(r,v)
        self.u.emu_start(va,emu.RET,count=count,timeout=30000000)
        for _ in range(9):
            if self.u.reg_read(UC_X86_REG_EIP)==emu.RET:break
            print(f'native continuation {self.u.reg_read(UC_X86_REG_EIP):08X}',flush=True)
            self.u.emu_start(self.u.reg_read(UC_X86_REG_EIP),emu.RET,count=count,timeout=30000000)
        if self.u.reg_read(UC_X86_REG_EIP)!=emu.RET:raise RuntimeError(f'execution limit at {self.u.reg_read(UC_X86_REG_EIP):08x}')
        return self.u.reg_read(UC_X86_REG_EAX)
    def thread(self,root=False):
        obj=self.alloc(0x8c)
        self.call(0x44c880,obj,0,0,0 if root else 4096,0 if root else 0x800000,0 if root else 0x400000,0 if root else 1,0)
        return obj
    def display(self):
        table=self.alloc(0x44);com=self.alloc(4);self.put(com,table)
        self.win.api('Direct3DCreate9',1)(lambda a:com)
        self.put(table+8,self.os_callback('IDirect3D9::Release',1,lambda a:0))
        self.put(table+0x10,self.os_callback('IDirect3D9::GetAdapterCount',1,lambda a:1))
        self.put(table+0x20,self.os_callback('IDirect3D9::GetAdapterDisplayMode',3,self.display_mode))
        self.put(table+0x3c,self.os_callback('IDirect3D9::GetAdapterMonitor',2,lambda a:0x7200))
        self.put(table+0x40,self.os_callback('IDirect3D9::CreateDevice',7,lambda a:0x8876086a)) # NOTAVAILABLE
        self.put(0x546194,com);self.call(0x46b2f0)
        workers=self.alloc(0x44);self.call(0x44ee30,workers,0,1);self.put(0x548634,workers)
        manager=self.alloc(0xa40);self.call(0x44a880,manager,0,0,0);self.put(0x5485ec,manager)
        for va in (0x437940,0x420160,0x451150,0x4581e0):self.call(va)
        slots=self.alloc(0xdc);self.call(0x4086c0,slots);self.put(0x548630,slots);self.call(0x44ac30)
        for va in (0x420170,0x437950,0x451160):self.call(va)
        return manager
