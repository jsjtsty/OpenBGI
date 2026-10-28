# Python 3.10: py -3 analysis/scripts/merge_bp_candidates.py <table.tsv> <store.tsv> <out.tsv>
# Merge structural candidate seeds; these do not prove reachability.
import csv,sys
rows=[]
for path in sys.argv[1:3]:
 with open(path,encoding='utf8',newline='') as f:rows.extend(csv.DictReader(f,delimiter='\t'))
with open(sys.argv[3],'w',encoding='utf8',newline='') as f:
 w=csv.DictWriter(f,fieldnames=['program','table_start','entries','candidate_targets'],delimiter='\t');w.writeheader();w.writerows(rows)
print('candidate initializer records',len(rows))
