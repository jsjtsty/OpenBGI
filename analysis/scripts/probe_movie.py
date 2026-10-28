# Usage: py -3 analysis/scripts/probe_movie.py <archive.arc>
# Reads only the first 16 MiB of each .mpg ARC member and sends it to ffprobe stdin.
import pathlib,struct,subprocess,sys
p=pathlib.Path(sys.argv[1]);u32=lambda b,o=0:struct.unpack_from('<I',b,o)[0]
with p.open('rb') as f:
 h=f.read(16);assert h[:12]==b'BURIKO ARC20';n=u32(h,12);table=f.read(n*128);base=16+128*n
 for i in range(n):
  rec=table[i*128:(i+1)*128];name=rec[:96].split(b'\0')[0].decode('cp932');off,size=struct.unpack_from('<II',rec,96)
  if not name.lower().endswith('.mpg'):continue
  f.seek(base+off);sample=f.read(min(size,16*1024*1024))
  cmd=['ffprobe','-v','error','-show_entries','format=format_name:stream=codec_name,codec_type,width,height,r_frame_rate,avg_frame_rate,field_order','-of','json','-i','pipe:0']
  result=subprocess.run(cmd,input=sample,capture_output=True,check=True)
  print(name, 'bytes examined',len(sample),flush=True);print(result.stdout.decode('utf8'))
