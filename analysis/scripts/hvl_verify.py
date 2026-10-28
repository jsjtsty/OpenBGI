# Usage: py -3 analysis/scripts/hvl_verify.py <BGI.hvl> <game dir> [name-filter]
# FUN_00401c90 parses BHV / BHV_V2; FUN_00401b70 computes two-word rolling digest.
import pathlib,struct,sys
h=pathlib.Path(sys.argv[1]).read_bytes();directory=pathlib.Path(sys.argv[2]);wanted=sys.argv[3].lower() if len(sys.argv)>3 else ''
if len(h)<16 or h[:4]!=b'BHV_':raise ValueError('unsupported BHV signature')
magic=h[:8];count=struct.unpack_from('<I',h,12)[0]
if magic==b'BHV_____':stride,hashoff=0x40,0x38
elif magic==b'BHV_V2__':stride,hashoff=0x100,0xf8
else:raise ValueError('unknown BHV variant')
if 16+count*stride!=len(h):raise ValueError('unexpected file length')
def digest(path):
 a=s=x=t=y=0
 with path.open('rb') as f:
  while chunk:=f.read(1<<20):
   for c in chunk:
    a=(a*233+c)&0xffffffff;low=a&255;s=(s+low)&255;x^=low;t=(t+c)&255;y^=c
 return a,s|x<<8|t<<16|y<<24
passed=failed=missing=0
for i in range(count):
 rec=h[16+i*stride:16+(i+1)*stride];name=rec[:hashoff].split(b'\0')[0].decode('cp932',errors='replace')
 if wanted and wanted not in name.lower():continue
 path=directory/name;expect=struct.unpack_from('<II',rec,hashoff)
 if not path.is_file():missing+=1;print(name,'MISSING');continue
 actual=digest(path);ok=actual==expect;passed+=ok;failed+=not ok
 print(name,'PASS' if ok else 'MISMATCH','expected',*(f'{v:08X}' for v in expect),'actual',*(f'{v:08X}' for v in actual),flush=True)
print('total',passed,'pass',failed,'mismatch',missing,'missing')
if failed:sys.exit(1)
