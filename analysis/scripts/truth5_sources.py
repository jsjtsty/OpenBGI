"""Usage: analysis/.venv/Scripts/python.exe analysis/scripts/truth5_sources.py
Collect exact existing decompilations for task 5; static evidence only.
"""
import re,csv
from truth5_runtime import ROOT
from truth5_targets import GROUPS
source=(ROOT/'out/ac2_ML.c').read_text(encoding='utf8')
functions={a:b for a,b in re.findall(r'// ==== ([0-9a-f]{8}) [^\n]*\n(.*?)(?=// ==== |\Z)',source,re.S)}
table={}
with (ROOT/'子opcode表.tsv').open(encoding='utf-8-sig') as f:
    for r in csv.DictReader(f,delimiter='\t'):table[r['主opcode']+':'+r['子opcode']]=r['处理器VA'].lower()
with (ROOT/'out/task5_native_sources.txt').open('w',encoding='utf8') as f:
    for group,ops in GROUPS.items():
        for op in ops:
            a=table.get(op)
            f.write(f'\n=== {group} {op} {a} ===\n')
            if not a:continue
            body=functions[a];f.write(body)
            for callee in dict.fromkeys(re.findall(r'FUN_([0-9a-f]{8})\(',body)):
                if callee not in ('0044d2a0','0044d2c0','0049d1f0','00402010','00471e50'):f.write(f'\n--- {callee} ---\n'+functions.get(callee,'NO DECOMPILATION\n'))
