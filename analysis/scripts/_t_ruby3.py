import verify_ruby as R
import verify_layout as V
from verify_ruby import *
for t in sys.argv[1:]:
    try:
        nl,nodes,before,rt=ruby_nodes(t)
        print(repr(t),[(s32(n+4),s32(n+0x10),s32(n+0x14),s32(n+0x20),r32(n+0x5c)) for n in nodes])
    except UcError as e:
        esp=u.reg_read(UC_X86_REG_ESP);print(repr(t),'FAULT',e,hex(u.reg_read(UC_X86_REG_EIP)),[hex(u.reg_read(r)) for r in (UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESI,UC_X86_REG_EDI)],[hex(r32(esp+4*i)) for i in range(0,200) if 0x401000<r32(esp+4*i)<0x4f0000][:8])
print('shapelog tail',V.SHAPELOG[-10:] if hasattr(V,'SHAPELOG') else None)
