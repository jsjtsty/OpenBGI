// Node.js 24: node analysis/scripts/extract_dsc.js <archive> <member> <output>
// DSC and archive research, derived from FUN_00470f00/00471170/00472c40.
const fs = require('fs');
function decode(d) {
 if (d.subarray(0,16).toString('ascii') !== 'DSC FORMAT 1.00\0') return d;
 if (d.length < 0x220) throw Error('truncated DSC header');
 const seed=d.readUInt32LE(16), outputSize=d.readUInt32LE(20), tokens=d.readUInt32LE(24);
 if(outputSize>0x4000000) throw Error('invalid output size');
 let state=seed, lengths=[];
 for(let n=0;n<512;n++){
  const product=(state & 0xffff)*0x4e35;
  const high=(Math.imul(state >>> 16,0x4e35)+Math.imul(state,0x15a)+(product >>> 16)) & 0xffff;
  state=((product & 0xffff)+1+high*0x10000)>>>0;
  let len=(d[32+n]-(high & 0x7fff))&255; if(len) lengths.push([len,n]);
 }
 lengths.sort((a,b)=>a[0]-b[0] || a[1]-b[1]);
 const nodes=[{}]; let active=[0],idx=0,depth=0;
 while(idx<lengths.length){let next=[]; for(const node of active){if(idx<lengths.length && lengths[idx][0]===depth) nodes[node].symbol=lengths[idx++][1]; else {let left=nodes.length; nodes.push({},{}); nodes[node].left=left; nodes[node].right=left+1; next.push(left,left+1);}} if(!next.length && idx<lengths.length) throw Error('invalid tree'); active=next; if(++depth>255 || nodes.length>4096) throw Error('invalid code lengths');}
 let bit=0, out=Buffer.alloc(outputSize), pos=0;
 function read(n){let v=0; while(n--){let byte=0x220+(bit>>>3); if(byte>=d.length) throw Error('truncated input'); v=(v<<1)|((d[byte]>>(7-(bit&7)))&1); bit++;} return v;}
 for(let k=0;k<tokens;k++){let n=0; while(nodes[n].symbol===undefined){n=read(1)?nodes[n].right:nodes[n].left; if(n===undefined) throw Error('invalid bitstream');} let s=nodes[n].symbol; if(s<256){if(pos>=out.length) throw Error('output overflow'); out[pos++]=s;} else {let dist=read(12)+2, count=(s&255)+2; if(dist>pos || pos+count>out.length) throw Error(`invalid match at ${pos}, distance ${dist}`); while(count--){out[pos]=out[pos-dist]; pos++;}} }
 if(pos!==outputSize) throw Error(`decoded ${pos}, expected ${outputSize}`); return out;
}
module.exports = { decode };
if (require.main === module) {
const [archive, name, dest]=process.argv.slice(2); if(!archive||!name||!dest) throw Error('usage: node extract_dsc.js archive member output');
const arc=fs.readFileSync(archive); if(arc.subarray(0,12).toString('ascii')!=='BURIKO ARC20') throw Error('unsupported archive'); const count=arc.readUInt32LE(12), base=16+count*128;
let found=false; for(let i=0;i<count;i++){let p=16+i*128, member=arc.subarray(p,p+96).toString('ascii').split('\0')[0], off=arc.readUInt32LE(p+96), size=arc.readUInt32LE(p+100); if(member.toLowerCase()===name.toLowerCase()){if(base+off+size>arc.length) throw Error('bad member range'); let raw=arc.subarray(base+off,base+off+size); let data=decode(raw); fs.writeFileSync(dest,data); console.log(`${member}: archive offset 0x${(base+off).toString(16)}, stored ${size}, decoded ${data.length}`); found=true; break;}}
if(!found) throw Error('member not found');
}
