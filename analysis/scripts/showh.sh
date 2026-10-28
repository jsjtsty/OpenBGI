#!/bin/sh
# usage: showh.sh addr... -- compact handler bodies with POP/PUSH counts
for a in "$@"; do .venv/Scripts/python.exe scripts/showfn.py $a | grep -v "^// ====\|security_check\|^{\|^}\|local_.*DAT_0052e2f8\|^  [a-zA-Z0-9_ *]* [a-zA-Z_0-9]*;$\|WARNING\|^  return 0;\|FUN_00402010\|ExceptionList\|puStack\|local_8 = 0xffffffff" | sed 's/FUN_0044d2a0/POP/g;s/FUN_0049d1f0/POPPTR/g;s/FUN_0044d2c0/PUSH/g' | cut -c1-140; echo "--- $a"; done
