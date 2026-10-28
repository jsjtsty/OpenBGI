# usage: py extract_shader.py <exe> <out> -- extract RT "SHADER" id 0x81 resource, decode DSC/SDC if wrapped (FUN_0046d150)
import sys,os,subprocess
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import pefile
from sdc_decode import sdc_decode
pe=pefile.PE(sys.argv[1])
for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
    tn=t.name.decode() if t.name else str(t.struct.Id)
    for n in t.directory.entries:
        for l in n.directory.entries:
            d=pe.get_data(l.data.struct.OffsetToData,l.data.struct.Size)
            print(tn,n.struct.Id,len(d),d[:16])
            if tn=='SHADER': open(sys.argv[2]+'.raw','wb').write(d)
