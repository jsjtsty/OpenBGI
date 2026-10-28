"""Task 5 requested opcode inventory, shared by the independent native tests."""
T1='80:17 81:0F 80:1C 80:12 80:14 80:18 80:19 80:0F 91:96 91:B8 91:BA 90:B9 90:05 90:20'.split()
T2='80:81 B0:C5 B0:C6 80:0D 81:30 81:0E 90:16 91:8D 91:8E 80:89 80:8A 80:8B 6C 6E 73 77 7C 7D 7E 78 90:09 90:DD 80:98'.split()
T3=['90:'+x for x in '31 35 36 37 39 3A 3C 3D 3F 40 41 42 43 44 45 46 48 49 53 55'.split()]+[f'91:{x:02X}' for x in range(0x31,0x4b)]+['91:55']
T4=[f'90:{x:02X}' for x in range(0x80,0x8a)]+[f'91:{x:02X}' for x in range(0x88,0x8f)]+[f'92:{x:02X}' for x in range(0x88,0x8f)]
GROUPS={'T1':T1,'T2':T2,'T3':T3,'T4':T4}
