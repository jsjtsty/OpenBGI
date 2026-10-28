// Node.js 24: node analysis/scripts/audit_dsc.js "E:\Games\Amairo Chocolate 2" analysis/out/dsc_manifest.tsv
// ARC20 entries; DSC FUN_00470f00, FUN_00471170, FUN_00472c40. Independent canonical decoder cross-check.
const fs = require('fs'), path = require('path'), crypto = require('crypto');
const {decode} = require('./extract_dsc');
const [root, output] = process.argv.slice(2); if (!root || !output) throw Error('game directory and manifest path required');
function canonical(d) {
 if(d.length<0x220) throw Error('short header'); const size=d.readUInt32LE(20), tokens=d.readUInt32LE(24); if(size>0x4000000) throw Error('size limit');
 let state=d.readUInt32LE(16), lens=[];
 for(let symbol=0; symbol<512; symbol++){const product=BigInt(state)*0x015a4e35n;const random=Number((product>>16n)&0x7fffn);state=Number((product+1n)&0xffffffffn);const n=(d[32+symbol]-random)&255;if(n) lens.push([n,symbol]);}
 lens.sort((a,b)=>a[0]-b[0]||a[1]-b[1]); const maps=new Map();let code=0n,last=0;
 for(const [n,sym] of lens){if(n>32) throw Error('code length > 32'); code <<= BigInt(n-last);if(code >= (1n<<BigInt(n))) throw Error('oversubscribed Huffman tree');if(!maps.has(n))maps.set(n,new Map());maps.get(n).set(code,sym);code++;last=n;}
 let pos=0, bit=0, out=Buffer.alloc(size); function read(){const p=0x220+(bit>>>3);if(p>=d.length)throw Error('truncated bitstream');const v=(d[p]>>>(7-(bit&7)))&1;bit++;return v;}
 for(let k=0;k<tokens;k++){let c=0n,s;for(let n=1;n<=last;n++){c=(c<<1n)|BigInt(read());s=maps.get(n)?.get(c);if(s!==undefined)break;}if(s===undefined)throw Error('invalid code'); if(s<256){if(pos>=size)throw Error('overflow');out[pos++]=s;}else{let distance=0;for(let i=0;i<12;i++)distance=distance*2+read();distance+=2;let count=(s&255)+2;if(distance>pos||pos+count>size)throw Error('invalid backreference');for(let i=0;i<count;i++){out[pos]=out[pos-distance];pos++;}}}
 if(pos!==size)throw Error(`size mismatch ${pos}/${size}`);return out;
}
function read(fd, size, pos){let b=Buffer.alloc(size);if(fs.readSync(fd,b,0,size,pos)!==size)throw Error('short read');return b;}
const rows=['arc\t成员名\t存储大小\t解出大小\t前16字节hex\t状态\tsha256\t备注'];let archives=0, checked=0, failed=0, all=0;
for(const file of fs.readdirSync(root).filter(x=>x.toLowerCase().endsWith('.arc')).sort()){
 const fd=fs.openSync(path.join(root,file),'r');try{let size=fs.fstatSync(fd).size, h=read(fd,16,0);if(h.subarray(0,12).toString('ascii')!=='BURIKO ARC20'){console.error('unsupported',file);failed++;continue;}let count=h.readUInt32LE(12),base=16+count*128;if(base>size)throw Error('bad directory length');let dir=read(fd,count*128,16);archives++;
 for(let i=0;i<count;i++){let p=i*128,name=dir.subarray(p,p+96).toString('utf8').split('\0')[0].replace(/[\t\r\n]/g,' '),off=base+dir.readUInt32LE(p+96),len=dir.readUInt32LE(p+100);all++;if(off+len>size){failed++;rows.push(`${file}\t${name}\t${len}\t\t\tFAIL\t\tarchive range`);continue;}if(len<16||read(fd,16,off).toString('ascii')!=='DSC FORMAT 1.00\0')continue;
 let row=[file,name,len,'','','PASS','',''];try{let data=read(fd,len,off), a=decode(data),b=canonical(data);if(!a.equals(b))throw Error('decoder disagreement');row[3]=a.length;row[4]=a.subarray(0,16).toString('hex');row[6]=crypto.createHash('sha256').update(a).digest('hex');if(name.endsWith('._bs')&&!a.subarray(0,27).toString('ascii').startsWith('BurikoCompiledScriptVer'))row[7]='_bs header differs';checked++;}catch(e){failed++;row[5]='FAIL';row[7]=e.message;}rows.push(row.join('\t'));
 }
 }catch(e){failed++;console.error(file,e.message);}finally{fs.closeSync(fd);}
 console.log(`${file}: inspected; DSC pass=${checked} failures=${failed}`);
}
fs.writeFileSync(output,rows.join('\n')+'\n');console.log(`archives=${archives} members=${all} DSC_pass=${checked} failures=${failed}`);if(failed)process.exitCode=1;
