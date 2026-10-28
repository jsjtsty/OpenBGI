# Python 3.10: py -3 analysis/scripts/verify_dispatch_returns.py <exe> <out.tsv>
# x86 normal paths of 7F/80/81/90/91/92/A0/B0/C0/D0/E0 dispatchers.
# Pattern: push esi; call eax; mov ecx,[ebp-4]; add esp,4; xor ecx,ebp;
# pop esi; call cookie checker; mov esp,ebp; pop ebp; ret.
# FUN_004cf4f7 successful cookie path uses only ECX/flags, not EAX.
import pathlib,struct,sys,csv
p,out=map(pathlib.Path,sys.argv[1:]);b=p.read_bytes()
wrappers={0x7f:0x484af0,0x80:0x499d80,0x81:0x49b840,0x90:0x48f5d0,0x91:0x493fd0,0x92:0x495c20,0xa0:0x4968a0,0xb0:0x4888a0,0xc0:0x486330,0xd0:0x487600,0xe0:0x469250}
def get(va,n):return b[va-0x400c00:va-0x400c00+n]
pre=bytes.fromhex('56 ff d0 8b 4d fc 83 c4 04 33 cd 5e e8');post=bytes.fromhex('8b e5 5d c3');cookie=0x4cf4f7
rows=[]
for op,va in wrappers.items():
 data=get(va,112);n=data.find(pre);valid=n>=0 and data[n+17:n+21]==post
 if valid:target=va+n+17+struct.unpack_from('<i',data,n+13)[0];valid=target==cookie
 rows.append((f'{op:02X}',f'{va:08X}',f'{va+n:08X}' if n>=0 else '',f'{target:08X}' if n>=0 and valid else '', 'PASS' if valid else 'FAIL'))
chk=get(cookie,12);cookie_ok=chk==bytes.fromhex('3b 0d f8 e2 52 00 f2 75 02 f2 c3 f2')
async_22=get(0x489bd0,259).find(bytes.fromhex('8b 4d fc b8 02 00 00 00 5f 5e 33 cd 5b e8'))>=0
async_10=get(0x488d20,0x140).find(bytes.fromhex('b8 02 00 00 00 eb 02 33 c0'))>=0
with out.open('w',encoding='utf8',newline='') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(('main','wrapper_va','indirect_call_sequence_va','cookie_va','pattern'));w.writerows(rows)
print('wrappers',len(rows),'pattern pass',sum(r[-1]=='PASS' for r in rows),'successful cookie preserves EAX',cookie_ok,'90:22 returns 2 on success',async_22,'90:10 asynchronous 2 / synchronous 0',async_10)
if not cookie_ok or not async_22 or not async_10 or any(r[-1]!='PASS' for r in rows):sys.exit(1)
