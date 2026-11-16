// ==== 00401010 FUN_00401010 ====

void FUN_00401010(void)

{
  FUN_00402d90((undefined4 *)&DAT_00409738);
  return;
}


// ==== 00401020 FUN_00401020 ====

void FUN_00401020(void)

{
  FUN_004064fc((_onexit_t)&LAB_00401030);
  return;
}


// ==== 00401040 FUN_00401040 ====

/* WARNING: Removing unreachable block (ram,0x00401172) */
/* WARNING: Removing unreachable block (ram,0x004012b2) */
/* WARNING: Removing unreachable block (ram,0x004013f2) */
/* WARNING: Removing unreachable block (ram,0x00401352) */
/* WARNING: Removing unreachable block (ram,0x00401212) */
/* WARNING: Removing unreachable block (ram,0x004010e2) */
/* WARNING: Removing unreachable block (ram,0x00401483) */

bool FUN_00401040(void)

{
  BOOL BVar1;
  void *lpBuffer;
  void *in_stack_0001527c;
  HANDLE in_stack_0001528c;
  undefined1 uVar2;
  undefined1 uStack_9;
  undefined1 *local_8;
  undefined4 local_4;
  
  local_4 = 0xffffffff;
  local_8 = &LAB_00406752;
  uVar2 = SUB41(ExceptionList,0);
  ExceptionList = (void *)((int)register0x00000010 + -0xc);
  FUN_00406520(uVar2);
  local_8 = (undefined1 *)0x0;
  BVar1 = ReadFile(in_stack_0001528c,&local_8,4,(LPDWORD)((int)register0x00000010 + -0xc),
                   (LPOVERLAPPED)0x0);
  if (BVar1 != 0) {
    SetFilePointer(in_stack_0001528c,-4,(PLONG)0x0,1);
    if (local_8 == (undefined1 *)0x576453) {
      lpBuffer = malloc(0x1000);
      ReadFile(in_stack_0001528c,lpBuffer,0x1000,(LPDWORD)((int)register0x00000010 + -0xc),
               (LPOVERLAPPED)0x0);
    }
    else if (local_8 == (undefined1 *)0x32576453) {
      lpBuffer = malloc(0x2000);
      ReadFile(in_stack_0001528c,lpBuffer,0x2000,(LPDWORD)((int)register0x00000010 + -0xc),
               (LPOVERLAPPED)0x0);
    }
    else if (local_8 == (undefined1 *)0x33576453) {
      lpBuffer = malloc(0x2000);
      ReadFile(in_stack_0001528c,lpBuffer,0x2000,(LPDWORD)((int)register0x00000010 + -0xc),
               (LPOVERLAPPED)0x0);
    }
    else if (local_8 == (undefined1 *)0x34576453) {
      lpBuffer = malloc(0x10000);
      ReadFile(in_stack_0001528c,lpBuffer,0x10000,(LPDWORD)((int)register0x00000010 + -0xc),
               (LPOVERLAPPED)0x0);
    }
    else if (local_8 == (undefined1 *)0x35576453) {
      lpBuffer = malloc(0x10000);
      ReadFile(in_stack_0001528c,lpBuffer,0x10000,(LPDWORD)((int)register0x00000010 + -0xc),
               (LPOVERLAPPED)0x0);
    }
    else if (local_8 == (undefined1 *)0x36576453) {
      lpBuffer = malloc(0x10000);
      ReadFile(in_stack_0001528c,lpBuffer,0x10000,(LPDWORD)((int)register0x00000010 + -0xc),
               (LPOVERLAPPED)0x0);
    }
    else {
      if (local_8 != (undefined1 *)0x37576453) {
        ExceptionList = in_stack_0001527c;
        return false;
      }
      lpBuffer = malloc(0x10000);
      ReadFile(in_stack_0001528c,lpBuffer,0x10000,(LPDWORD)((int)register0x00000010 + -0xc),
               (LPOVERLAPPED)0x0);
    }
    free(lpBuffer);
  }
  ExceptionList = in_stack_0001527c;
  return false;
}


// ==== 004014c0 FUN_004014c0 ====

undefined * FUN_004014c0(void)

{
  return &DAT_00409738;
}


// ==== 004014d0 FUN_004014d0 ====

void FUN_004014d0(void)

{
  DAT_00413904 = 0;
  EnumWindows((WNDENUMPROC)&LAB_004014f0,1);
  return;
}


// ==== 00401650 FUN_00401650 ====

void FUN_00401650(void)

{
  int iVar1;
  undefined4 *puVar2;
  CHAR local_100 [256];
  
  wsprintfA(local_100,s_c_nCount__d_00409030,DAT_00413904);
  FUN_004046c0(local_100);
  iVar1 = 0;
  if (0 < DAT_00413904) {
    puVar2 = &DAT_00409538;
    do {
      wsprintfA(local_100,s___d_____X___X_00409020,iVar1,*puVar2,puVar2[1]);
      FUN_004046c0(local_100);
      iVar1 = iVar1 + 1;
      puVar2 = puVar2 + 2;
    } while (iVar1 < DAT_00413904);
  }
  EnumWindows((WNDENUMPROC)&LAB_004014f0,0);
  return;
}


// ==== 004016e0 FUN_004016e0 ====

void FUN_004016e0(void)

{
  EnumWindows((WNDENUMPROC)&LAB_004014f0,2);
  return;
}


// ==== 004016f0 FUN_004016f0 ====

bool __cdecl FUN_004016f0(uint param_1)

{
  bool bVar1;
  undefined3 extraout_var;
  int iVar2;
  
  FUN_00405050(&DAT_00409748);
  bVar1 = FUN_00404e70(DAT_00409748);
  if (CONCAT31(extraout_var,bVar1) == 0) {
    return false;
  }
  iVar2 = FUN_00404b40((BYTE *)0x0,param_1,0,0x4010000);
  if ((iVar2 == 1) || (iVar2 == 10)) {
    FUN_00401770();
  }
  return iVar2 == 0;
}


// ==== 00401770 FUN_00401770 ====

undefined4 FUN_00401770(void)

{
  uint uVar1;
  HINSTANCE pHVar2;
  
  uVar1 = MessageBoxA((HWND)0x0,&DAT_00412c88,s_SdWrap_00409044,4);
  if (uVar1 == 6) {
    pHVar2 = ShellExecuteA((HWND)0x0,&DAT_0040903c,&DAT_00413088,(LPCSTR)0x0,(LPCSTR)0x0,1);
    return CONCAT31((int3)((uint)pHVar2 >> 8),1);
  }
  return uVar1 & 0xffffff00;
}


// ==== 004017b0 FUN_004017b0 ====

undefined4 __cdecl FUN_004017b0(char param_1)

{
  undefined4 uVar1;
  
  uVar1 = FUN_00401840(param_1);
  if ((char)uVar1 == '\0') {
    uVar1 = FUN_004018c0(param_1);
    if ((char)uVar1 == '\0') {
      return 0;
    }
  }
  return 1;
}


// ==== 004017e0 FUN_004017e0 ====

bool __cdecl FUN_004017e0(undefined4 param_1)

{
  int iVar1;
  
  iVar1 = FUN_00404ef0(&DAT_00409748,DAT_00409758,0,param_1,0);
  if (iVar1 == 0) {
    return true;
  }
  if (iVar1 == 1) {
    FUN_004045f0();
    return false;
  }
  if (DAT_0040975c == 0) {
    return false;
  }
  iVar1 = FUN_00404ef0(&DAT_0040975c,DAT_0040976c,0,param_1,0);
  return iVar1 == 0;
}


// ==== 00401840 FUN_00401840 ====

uint __cdecl FUN_00401840(char param_1)

{
  uint uVar1;
  int local_c;
  uint local_8;
  undefined1 local_4 [4];
  
  local_c = 0;
  uVar1 = FUN_00404e90(&DAT_00409748,local_4,&local_c,&local_8);
  if (uVar1 != 0) {
    return uVar1 & 0xffffff00;
  }
  if (param_1 == '\0') {
    uVar1 = FUN_00404eb0(&DAT_00409748,0);
    if (0 < (int)uVar1) {
      local_c = local_c + uVar1;
    }
  }
  else if ((local_8 == 0xffffffff) || (local_8 == 0)) goto LAB_004018a7;
  local_8 = 0;
  if (local_c != 0) {
    return CONCAT31((int3)((uint)local_c >> 8),1);
  }
LAB_004018a7:
  return local_8 & 0xffffff00;
}


// ==== 004018c0 FUN_004018c0 ====

undefined4 __cdecl FUN_004018c0(char param_1)

{
  uint uVar1;
  int local_c;
  uint local_8;
  undefined1 local_4 [4];
  
  if (DAT_0040975c == 0) {
    return 0;
  }
  local_c = 0;
  uVar1 = FUN_00404e90(&DAT_0040975c,local_4,&local_c,&local_8);
  if (uVar1 != 0) {
    return uVar1 & 0xffffff00;
  }
  if (param_1 == '\0') {
    uVar1 = FUN_00404eb0(&DAT_0040975c,0);
    if (0 < (int)uVar1) {
      local_c = local_c + uVar1;
    }
  }
  else if ((local_8 == 0xffffffff) || (local_8 == 0)) goto LAB_00401936;
  local_8 = 0;
  if (local_c != 0) {
    return CONCAT31((int3)((uint)local_c >> 8),1);
  }
LAB_00401936:
  return local_8 & 0xffffff00;
}


// ==== 00401950 FUN_00401950 ====

uint __cdecl FUN_00401950(undefined4 param_1)

{
  uint uVar1;
  int iVar2;
  
  if (DAT_004138ec == (HMODULE)0x0) {
    DAT_004138ec = LoadLibraryA(s_ucopgdlg_dll_00409060);
    if (DAT_004138ec == (HMODULE)0x0) {
      return 0;
    }
  }
  if (DAT_004138f0 == (FARPROC)0x0) {
    DAT_004138f0 = GetProcAddress(DAT_004138ec,s_UC_OpenningDialog2_0040904c);
    if (DAT_004138f0 == (FARPROC)0x0) {
      uVar1 = FreeLibrary(DAT_004138ec);
      DAT_004138ec = (HMODULE)0x0;
      return uVar1 & 0xffffff00;
    }
  }
  iVar2 = (*DAT_004138f0)(param_1);
  return (uint)(iVar2 != 0);
}


// ==== 004019c0 FUN_004019c0 ====

undefined4 FUN_004019c0(void)

{
  uint uVar1;
  int iVar2;
  
  if (DAT_004138ec == (HMODULE)0x0) {
    DAT_004138ec = LoadLibraryA(s_ucopgdlg_dll_00409060);
    if (DAT_004138ec == (HMODULE)0x0) {
      return 0;
    }
  }
  if (DAT_004138f4 == (FARPROC)0x0) {
    DAT_004138f4 = GetProcAddress(DAT_004138ec,s_UC_Mode3Charge_00409070);
    if (DAT_004138f4 == (FARPROC)0x0) {
      uVar1 = FreeLibrary(DAT_004138ec);
      DAT_004138ec = (HMODULE)0x0;
      return uVar1 & 0xffffff00;
    }
  }
  iVar2 = (*DAT_004138f4)(0,0);
  return CONCAT31((int3)((uint)iVar2 >> 8),1);
}


// ==== 00401a30 FUN_00401a30 ====

uint __cdecl FUN_00401a30(undefined4 param_1,undefined4 param_2)

{
  uint uVar1;
  int iVar2;
  
  if (DAT_004138fc == (HMODULE)0x0) {
    DAT_004138fc = LoadLibraryA(s_ucopgdlg_dll_00409060);
    if (DAT_004138fc == (HMODULE)0x0) {
      return 0;
    }
    DAT_00413900 = GetProcAddress(DAT_004138fc,s_UC_ScidCharge_00409080);
    if (DAT_004138fc == (HMODULE)0x0) {
      uVar1 = FreeLibrary((HMODULE)0x0);
      DAT_004138fc = (HMODULE)0x0;
      return uVar1 & 0xffffff00;
    }
  }
  iVar2 = (*DAT_00413900)(param_1,param_2,
                          "https://charge1.soft-denchi.jp/DenchiSystem/satxControler",
                          "https://charge1.soft-denchi.jp/DenchiSystem/satxControler");
  return (uint)(iVar2 == 0);
}


// ==== 00401aa0 FUN_00401aa0 ====

bool FUN_00401aa0(void)

{
  if (DAT_004138ec == (HMODULE)0x0) {
    DAT_004138ec = LoadLibraryA(s_ucopgdlg_dll_00409060);
    if (DAT_004138ec == (HMODULE)0x0) {
      return false;
    }
  }
  if (DAT_004138f8 == (FARPROC)0x0) {
    DAT_004138f8 = GetProcAddress(DAT_004138ec,s_UC_DmmDialog_00409090);
  }
  return DAT_004138f8 != (FARPROC)0x0;
}


// ==== 00401af0 FUN_00401af0 ====

uint __cdecl FUN_00401af0(undefined4 param_1)

{
  uint uVar1;
  int iVar2;
  
  FUN_00401aa0();
  if (DAT_004138f8 == (code *)0x0) {
    uVar1 = FUN_004019c0();
    return uVar1;
  }
  iVar2 = (*DAT_004138f8)(param_1);
  return (uint)(iVar2 != 0);
}


// ==== 00401b20 FUN_00401b20 ====

undefined1 FUN_00401b20(void)

{
  undefined1 uVar1;
  undefined4 uVar2;
  HWND hWnd;
  undefined *puVar3;
  uint uVar4;
  BOOL BVar5;
  int iVar6;
  BYTE *pBVar7;
  undefined *puVar8;
  DEVMODEA local_1a4;
  CHAR local_108 [264];
  
  if (((DAT_00409784 & 0xa0) != 0) &&
     ((uVar2 = FUN_00401840('\0'), (char)uVar2 != '\0' ||
      (uVar2 = FUN_004018c0('\0'), (char)uVar2 != '\0')))) {
    return 1;
  }
  if (DAT_00413908 != '\0') {
    return 1;
  }
  FUN_004046c0(s_DoOpgDlg2_004090e4);
  local_1a4.dmDeviceName[0] = '\0';
  pBVar7 = local_1a4.dmDeviceName + 1;
  for (iVar6 = 0x26; iVar6 != 0; iVar6 = iVar6 + -1) {
    pBVar7[0] = '\0';
    pBVar7[1] = '\0';
    pBVar7[2] = '\0';
    pBVar7[3] = '\0';
    pBVar7 = pBVar7 + 4;
  }
  pBVar7[0] = '\0';
  pBVar7[1] = '\0';
  pBVar7[2] = '\0';
  if ((DAT_00409784 & 2) != 0) {
    FUN_00401ca0(&local_1a4);
    FUN_00401ce0((DEVMODEA *)0x0);
  }
  FUN_004014d0();
  GetCurrentDirectoryA(0x104,local_108);
  hWnd = GetForegroundWindow();
  DAT_00413908 = 1;
  if ((DAT_00409784 & 0x20) == 0) {
    if ((DAT_00409784 & 0x80) == 0) {
      if ((DAT_00409784 & 0x400) == 0) {
        uVar4 = FUN_00401950(&DAT_00409748);
        uVar1 = (undefined1)uVar4;
      }
      else {
        uVar4 = FUN_00401af0(&DAT_00409738);
        uVar1 = (undefined1)uVar4;
      }
    }
    else {
      uVar2 = FUN_004019c0();
      uVar1 = (undefined1)uVar2;
    }
  }
  else {
    puVar8 = &DAT_004138c8;
    puVar3 = FUN_00404860(0x409738,&DAT_00413888);
    uVar4 = FUN_00401a30(puVar3,puVar8);
    uVar1 = (undefined1)uVar4;
  }
  DAT_00413908 = 0;
  SetCurrentDirectoryA(local_108);
  FUN_00401650();
  if ((DAT_00409784 & 2) != 0) {
    FUN_00401ce0(&local_1a4);
  }
  if (hWnd != (HWND)0x0) {
    BVar5 = SetForegroundWindow(hWnd);
    if (BVar5 != 0) {
      FUN_004046c0(s_SetForegroundWindow___succeeded__004090c0);
      return uVar1;
    }
    FUN_004046c0(s_SetForegroundWindow___failed__004090a0);
  }
  return uVar1;
}


// ==== 00401ca0 FUN_00401ca0 ====

uint __cdecl FUN_00401ca0(DEVMODEA *param_1)

{
  uint in_EAX;
  BOOL BVar1;
  int iVar2;
  DEVMODEA *pDVar3;
  
  if (param_1 == (DEVMODEA *)0x0) {
    return in_EAX & 0xffffff00;
  }
  pDVar3 = param_1;
  for (iVar2 = 0x27; iVar2 != 0; iVar2 = iVar2 + -1) {
    pDVar3->dmDeviceName[0] = '\0';
    pDVar3->dmDeviceName[1] = '\0';
    pDVar3->dmDeviceName[2] = '\0';
    pDVar3->dmDeviceName[3] = '\0';
    pDVar3 = (DEVMODEA *)(pDVar3->dmDeviceName + 4);
  }
  BVar1 = EnumDisplaySettingsA((LPCSTR)0x0,0xffffffff,param_1);
  if (BVar1 != 0) {
    return CONCAT31((int3)((uint)BVar1 >> 8),1);
  }
  for (iVar2 = 0x27; iVar2 != 0; iVar2 = iVar2 + -1) {
    param_1->dmDeviceName[0] = '\0';
    param_1->dmDeviceName[1] = '\0';
    param_1->dmDeviceName[2] = '\0';
    param_1->dmDeviceName[3] = '\0';
    param_1 = (DEVMODEA *)(param_1->dmDeviceName + 4);
  }
  return 0;
}


// ==== 00401ce0 FUN_00401ce0 ====

bool __cdecl FUN_00401ce0(DEVMODEA *param_1)

{
  LONG LVar1;
  
  LVar1 = ChangeDisplaySettingsA(param_1,0);
  return (bool)('\x01' - (LVar1 != 0));
}


// ==== 00401d20 FUN_00401d20 ====

undefined4 FUN_00401d20(HINSTANCE param_1,undefined4 param_2,LPCSTR param_3)

{
  bool bVar1;
  int iVar2;
  undefined1 *puVar3;
  HANDLE hFile;
  uint uVar4;
  DWORD lDistanceToMove;
  DWORD DVar5;
  undefined *puVar6;
  HWND pHVar7;
  HACCEL hAccTable;
  tagMSG local_1c;
  
  LoadStringA(param_1,0x6d,&DAT_00413910,100);
  FUN_00401f90(param_1);
  FUN_004046c0(param_3);
  iVar2 = strncmp(param_3,&DAT_004090f8,3);
  if ((iVar2 == 0) && (iVar2 = FUN_00401ef0((int)param_3), iVar2 == 0)) {
    return 0;
  }
  PTR_DAT_004090f0 = param_3;
  puVar3 = FUN_00403800();
  if (puVar3 == (undefined1 *)0x0) {
    return 0;
  }
  hFile = FUN_004040b0();
  if (hFile == (HANDLE)0xffffffff) {
    return 0;
  }
  uVar4 = FUN_00404900(hFile);
  if (uVar4 == 0) {
    CloseHandle(hFile);
    return 0;
  }
  lDistanceToMove = FUN_004049f0();
  DVar5 = SetFilePointer(hFile,lDistanceToMove,(PLONG)0x0,0);
  if (DVar5 != lDistanceToMove) {
    CloseHandle(hFile);
    return 0;
  }
  bVar1 = FUN_00401040();
  if (!bVar1) {
    CloseHandle(hFile);
    return 0;
  }
  CloseHandle(hFile);
  puVar6 = FUN_004014c0();
  if ((puVar6[0x4c] & 0x10) == 0) {
    puVar6 = FUN_004014c0();
    wsprintfA(&DAT_00413974,&DAT_004090f4,*(undefined4 *)(puVar6 + 0x10));
    pHVar7 = FindWindowA(&DAT_00413910,&DAT_00413974);
    if (pHVar7 != (HWND)0x0) {
      return 0;
    }
  }
  iVar2 = FUN_00402010(param_1);
  if (iVar2 == 0) {
    return 0;
  }
  hAccTable = LoadAcceleratorsA(param_1,(LPCSTR)0x6d);
  iVar2 = GetMessageA(&local_1c,(HWND)0x0,0,0);
  while (iVar2 != 0) {
    iVar2 = TranslateAcceleratorA(local_1c.hwnd,hAccTable,&local_1c);
    if (iVar2 == 0) {
      TranslateMessage(&local_1c);
      DispatchMessageA(&local_1c);
    }
    iVar2 = GetMessageA(&local_1c,(HWND)0x0,0,0);
  }
  return local_1c.wParam;
}


// ==== 00401ef0 FUN_00401ef0 ====

undefined4 __cdecl FUN_00401ef0(int param_1)

{
  ulong dwProcessId;
  ulong dwProcessId_00;
  HANDLE pvVar1;
  char *local_4;
  
  FUN_004046c0(&DAT_004090f8);
  local_4 = (char *)0x0;
  dwProcessId = strtoul((char *)(param_1 + 3),&local_4,0x10);
  if (dwProcessId != 0) {
    dwProcessId_00 = strtoul(local_4,&local_4,0x10);
    if (dwProcessId_00 != 0) {
      pvVar1 = OpenProcess(0x1f0fff,0,dwProcessId_00);
      WaitForSingleObject(pvVar1,0xffffffff);
      CloseHandle(pvVar1);
      pvVar1 = OpenProcess(0x1f0fff,0,dwProcessId);
      if (pvVar1 != (HANDLE)0x0) {
        TerminateProcess(pvVar1,0);
        CloseHandle(pvVar1);
      }
      return 0;
    }
  }
  return 1;
}


// ==== 00401f90 FUN_00401f90 ====

void __cdecl FUN_00401f90(HINSTANCE param_1)

{
  WNDCLASSEXA local_30;
  
  local_30.cbSize = 0x30;
  local_30.style = 3;
  local_30.lpfnWndProc = (WNDPROC)&LAB_00402260;
  local_30.cbClsExtra = 0;
  local_30.cbWndExtra = 0;
  local_30.hInstance = param_1;
  local_30.hIcon = LoadIconA(param_1,(LPCSTR)0x6b);
  local_30.hCursor = LoadCursorA((HINSTANCE)0x0,(LPCSTR)0x7f00);
  local_30.hbrBackground = (HBRUSH)0x6;
  local_30.lpszMenuName = (LPCSTR)0x6d;
  local_30.lpszClassName = &DAT_00413910;
  local_30.hIconSm = (HICON)0x0;
  RegisterClassExA(&local_30);
  return;
}


// ==== 00402010 FUN_00402010 ====

undefined4 __cdecl FUN_00402010(HINSTANCE param_1)

{
  HWND hWnd;
  
  DAT_004139d8 = param_1;
  hWnd = CreateWindowExA(0x80,&DAT_00413910,&DAT_00413974,0xcf0000,-0x80000000,0,-0x80000000,0,
                         (HWND)0x0,(HMENU)0x0,param_1,(LPVOID)0x0);
  if (hWnd == (HWND)0x0) {
    return 0;
  }
  ShowWindow(hWnd,0);
  UpdateWindow(hWnd);
  PostMessageA(hWnd,0x414,0,0);
  return 1;
}


// ==== 00402080 FUN_00402080 ====

uint __cdecl FUN_00402080(HWND param_1)

{
  bool bVar1;
  char cVar2;
  undefined *puVar3;
  undefined3 extraout_var;
  undefined4 uVar4;
  uint uVar5;
  undefined3 extraout_var_00;
  undefined3 extraout_var_01;
  undefined3 extraout_var_02;
  LPCSTR pCVar6;
  
  puVar3 = FUN_004014c0();
  bVar1 = FUN_004016f0((uint)param_1);
  uVar5 = CONCAT31(extraout_var,bVar1);
  if (!bVar1) goto LAB_004020f5;
  if ((*(uint *)(puVar3 + 0x4c) & 4) == 0) {
    if ((*(uint *)(puVar3 + 0x4c) & 0x400) == 0) {
      uVar4 = FUN_00401840('\0');
      if (((char)uVar4 != '\0') || (uVar4 = FUN_004018c0('\0'), (char)uVar4 == '\0')) {
        cVar2 = FUN_00401b20();
        uVar5 = CONCAT31(extraout_var_01,cVar2);
        if (cVar2 == '\0') goto LAB_004020f5;
      }
    }
    else {
      bVar1 = FUN_00401aa0();
      if (!bVar1) {
        uVar5 = FUN_00401770();
        return uVar5 & 0xffffff00;
      }
      uVar4 = FUN_00401840('\x01');
      if (((char)uVar4 == '\0') && (uVar4 = FUN_004018c0('\x01'), (char)uVar4 == '\0')) {
        cVar2 = FUN_00401b20();
        if (cVar2 == '\0') {
          return CONCAT31(extraout_var_00,cVar2);
        }
      }
    }
  }
  else {
    uVar4 = FUN_00401840('\0');
    if (((char)uVar4 == '\0') && (uVar4 = FUN_004018c0('\0'), (char)uVar4 == '\0')) {
      pCVar6 = puVar3 + 0x9d50;
      if (puVar3[0x9d50] == '\0') {
        pCVar6 = &DAT_004072c8;
      }
      pCVar6 = FUN_00404860((int)puVar3,pCVar6);
      uVar5 = MessageBoxA(param_1,pCVar6,&DAT_004139e4,0);
      goto LAB_004020f5;
    }
  }
  if ((*(uint *)(puVar3 + 0x4c) & 4) == 0) {
    if ((*(uint *)(puVar3 + 0x4c) & 0x400) == 0) {
      uVar5 = FUN_004017b0('\0');
      if ((char)uVar5 != '\0') {
        bVar1 = FUN_004017e0(*(undefined4 *)(puVar3 + 0x38));
        uVar5 = CONCAT31(extraout_var_02,bVar1);
        if (bVar1) goto LAB_004021b0;
      }
LAB_004020f5:
      return uVar5 & 0xffffff00;
    }
    uVar4 = FUN_004017b0('\x01');
    if (((char)uVar4 == '\0') && (uVar4 = FUN_004018c0('\x01'), (char)uVar4 == '\0')) {
      return uVar4;
    }
  }
LAB_004021b0:
  uVar4 = FUN_004021d0(param_1);
  return CONCAT31((int3)((uint)uVar4 >> 8),1);
}


// ==== 004021d0 FUN_004021d0 ====

undefined4 __cdecl FUN_004021d0(HWND param_1)

{
  undefined *puVar1;
  undefined4 uVar2;
  UINT_PTR UVar3;
  BOOL BVar4;
  
  puVar1 = FUN_004014c0();
  uVar2 = FUN_00403830(param_1,PTR_DAT_004090f0,*(uint *)(puVar1 + 0x4c),puVar1 + 0x9450,
                       puVar1 + 0x50,0x100,*(DWORD *)(puVar1 + 0xa1b0));
  if ((char)uVar2 == '\0') {
    return uVar2;
  }
  uVar2 = FUN_00404500();
  if ((char)uVar2 == '\0') {
    return uVar2;
  }
  if (*(UINT *)(puVar1 + 0x40) != 0) {
    UVar3 = SetTimer(param_1,0x400,*(UINT *)(puVar1 + 0x40),(TIMERPROC)0x0);
    if (UVar3 == 0) {
      return 0;
    }
  }
  DAT_004139e0 = 1;
  BVar4 = PostMessageA(param_1,DAT_0041390c,0,0);
  return CONCAT31((int3)((uint)BVar4 >> 8),1);
}


// ==== 004027e0 FUN_004027e0 ====

undefined4 * __fastcall FUN_004027e0(undefined4 *param_1)

{
  FUN_004027f0(param_1);
  return param_1;
}


// ==== 004027f0 FUN_004027f0 ====

void __fastcall FUN_004027f0(undefined4 *param_1)

{
  int iVar1;
  undefined4 *puVar2;
  time_t tVar3;
  
  *param_1 = 0x576453;
  param_1[1] = 0;
  tVar3 = time((time_t *)0x0);
  param_1[2] = (int)tVar3;
  param_1[3] = 0;
  puVar2 = param_1 + 4;
  for (iVar1 = 0x10; iVar1 != 0; iVar1 = iVar1 + -1) {
    *puVar2 = 0;
    puVar2 = puVar2 + 1;
  }
  puVar2 = param_1 + 0x14;
  for (iVar1 = 0x40; iVar1 != 0; iVar1 = iVar1 + -1) {
    *puVar2 = 0;
    puVar2 = puVar2 + 1;
  }
  return;
}


// ==== 00402830 FUN_00402830 ====

bool __thiscall FUN_00402830(void *this,int *param_1)

{
  uint uVar1;
  int iVar2;
  int *piVar3;
  
  if (*param_1 != 0x576453) {
    return false;
  }
  piVar3 = this;
  for (iVar2 = 0x54; iVar2 != 0; iVar2 = iVar2 + -1) {
    *piVar3 = *param_1;
    param_1 = param_1 + 1;
    piVar3 = piVar3 + 1;
  }
  FUN_00405230(0x140,(int)this + 0x10,*(uint *)((int)this + 8));
  uVar1 = FUN_00405060(0x140,(byte *)((int)this + 0x10));
  return uVar1 == *(uint *)((int)this + 4);
}


// ==== 00402880 FUN_00402880 ====

undefined4 * __fastcall FUN_00402880(undefined4 *param_1)

{
  FUN_00402890(param_1);
  return param_1;
}


// ==== 00402890 FUN_00402890 ====

void __fastcall FUN_00402890(undefined4 *param_1)

{
  int iVar1;
  undefined4 *puVar2;
  time_t tVar3;
  
  *param_1 = 0x32576453;
  param_1[1] = 0;
  tVar3 = time((time_t *)0x0);
  param_1[2] = (int)tVar3;
  param_1[3] = 0;
  puVar2 = param_1 + 4;
  for (iVar1 = 0x10; iVar1 != 0; iVar1 = iVar1 + -1) {
    *puVar2 = 0;
    puVar2 = puVar2 + 1;
  }
  puVar2 = param_1 + 0x14;
  for (iVar1 = 0x450; iVar1 != 0; iVar1 = iVar1 + -1) {
    *puVar2 = 0;
    puVar2 = puVar2 + 1;
  }
  return;
}


// ==== 004028d0 FUN_004028d0 ====

bool __thiscall FUN_004028d0(void *this,int *param_1)

{
  uint uVar1;
  int iVar2;
  int *piVar3;
  
  if (*param_1 != 0x32576453) {
    return false;
  }
  piVar3 = this;
  for (iVar2 = 0x464; iVar2 != 0; iVar2 = iVar2 + -1) {
    *piVar3 = *param_1;
    param_1 = param_1 + 1;
    piVar3 = piVar3 + 1;
  }
  FUN_00405230(0x1180,(int)this + 0x10,*(uint *)((int)this + 8));
  uVar1 = FUN_00405060(0x1180,(byte *)((int)this + 0x10));
  return uVar1 == *(uint *)((int)this + 4);
}


// ==== 00402920 FUN_00402920 ====

undefined4 * __fastcall FUN_00402920(undefined4 *param_1)

{
  FUN_00402930(param_1);
  return param_1;
}


// ==== 00402930 FUN_00402930 ====

void __fastcall FUN_00402930(undefined4 *param_1)

{
  int iVar1;
  undefined4 *puVar2;
  time_t tVar3;
  
  *param_1 = 0x33576453;
  param_1[1] = 0;
  tVar3 = time((time_t *)0x0);
  param_1[2] = (int)tVar3;
  param_1[3] = 0;
  puVar2 = param_1 + 4;
  for (iVar1 = 0x10; iVar1 != 0; iVar1 = iVar1 + -1) {
    *puVar2 = 0;
    puVar2 = puVar2 + 1;
  }
  puVar2 = param_1 + 0x14;
  for (iVar1 = 0x450; iVar1 != 0; iVar1 = iVar1 + -1) {
    *puVar2 = 0;
    puVar2 = puVar2 + 1;
  }
  puVar2 = param_1 + 0x464;
  for (iVar1 = 0x40; iVar1 != 0; iVar1 = iVar1 + -1) {
    *puVar2 = 0;
    puVar2 = puVar2 + 1;
  }
  return;
}


// ==== 00402980 FUN_00402980 ====

bool __thiscall FUN_00402980(void *this,int *param_1)

{
  uint uVar1;
  int iVar2;
  int *piVar3;
  
  if (*param_1 != 0x33576453) {
    return false;
  }
  piVar3 = this;
  for (iVar2 = 0x4a4; iVar2 != 0; iVar2 = iVar2 + -1) {
    *piVar3 = *param_1;
    param_1 = param_1 + 1;
    piVar3 = piVar3 + 1;
  }
  FUN_00405230(0x1280,(int)this + 0x10,*(uint *)((int)this + 8));
  uVar1 = FUN_00405060(0x1280,(byte *)((int)this + 0x10));
  return uVar1 == *(uint *)((int)this + 4);
}


// ==== 004029d0 FUN_004029d0 ====

undefined4 __thiscall FUN_004029d0(void *this,undefined4 *param_1)

{
  undefined4 uVar1;
  int iVar2;
  undefined4 *puVar3;
  
  puVar3 = this;
  for (iVar2 = 0x20; iVar2 != 0; iVar2 = iVar2 + -1) {
    *puVar3 = 0;
    puVar3 = puVar3 + 1;
  }
  *(undefined4 *)((int)this + 0x80) = *param_1;
  *(undefined4 *)((int)this + 0x84) = param_1[1];
  *(undefined4 *)((int)this + 0x88) = 0;
  uVar1 = param_1[2];
  *(undefined4 *)((int)this + 0x8c) = uVar1;
  *(undefined4 *)((int)this + 0x90) = 0;
  return CONCAT31((int3)((uint)uVar1 >> 8),1);
}


// ==== 00402a10 FUN_00402a10 ====

undefined4 __thiscall FUN_00402a10(void *this,LPCSTR param_1)

{
  undefined4 uVar1;
  uint uVar2;
  
  uVar2 = lstrlenA(param_1);
  if (uVar2 >= 0x80) {
    AfxMessageBox(&DAT_00409104,0,0);
  }
  lstrcpynA(this,param_1,0x7f);
  *(undefined1 *)((int)this + 0x7f) = 0;
  *(undefined4 *)((int)this + 0x80) = *(undefined4 *)(param_1 + 0x100);
  *(undefined4 *)((int)this + 0x84) = *(undefined4 *)(param_1 + 0x104);
  *(undefined4 *)((int)this + 0x88) = *(undefined4 *)(param_1 + 0x108);
  uVar1 = *(undefined4 *)(param_1 + 0x10c);
  *(undefined4 *)((int)this + 0x8c) = uVar1;
  *(undefined4 *)((int)this + 0x90) = *(undefined4 *)(param_1 + 0x110);
  return CONCAT31((int3)((uint)uVar1 >> 8),uVar2 < 0x80);
}


// ==== 00402a90 FUN_00402a90 ====

undefined4 * __fastcall FUN_00402a90(undefined4 *param_1)

{
  int iVar1;
  undefined4 *puVar2;
  
  puVar2 = param_1 + 0x14;
  for (iVar1 = 0x2500; iVar1 != 0; iVar1 = iVar1 + -1) {
    *puVar2 = 0;
    puVar2 = puVar2 + 1;
  }
  FUN_00402ab0(param_1);
  return param_1;
}


// ==== 00402ab0 FUN_00402ab0 ====

void __fastcall FUN_00402ab0(undefined4 *param_1)

{
  int iVar1;
  undefined4 *puVar2;
  time_t tVar3;
  
  *param_1 = 0x34576453;
  param_1[1] = 0;
  tVar3 = time((time_t *)0x0);
  param_1[2] = (int)tVar3;
  param_1[3] = 0;
  puVar2 = param_1 + 4;
  for (iVar1 = 0x10; iVar1 != 0; iVar1 = iVar1 + -1) {
    *puVar2 = 0;
    puVar2 = puVar2 + 1;
  }
  puVar2 = param_1 + 0x14;
  for (iVar1 = 0x2500; iVar1 != 0; iVar1 = iVar1 + -1) {
    *puVar2 = 0;
    puVar2 = puVar2 + 1;
  }
  puVar2 = param_1 + 0x2514;
  for (iVar1 = 0x40; iVar1 != 0; iVar1 = iVar1 + -1) {
    *puVar2 = 0;
    puVar2 = puVar2 + 1;
  }
  return;
}


// ==== 00402b00 FUN_00402b00 ====

bool __thiscall FUN_00402b00(void *this,int *param_1)

{
  uint uVar1;
  int iVar2;
  int *piVar3;
  
  if (*param_1 != 0x34576453) {
    return false;
  }
  piVar3 = this;
  for (iVar2 = 0x2554; iVar2 != 0; iVar2 = iVar2 + -1) {
    *piVar3 = *param_1;
    param_1 = param_1 + 1;
    piVar3 = piVar3 + 1;
  }
  FUN_00405230(0x9540,(int)this + 0x10,*(uint *)((int)this + 8));
  uVar1 = FUN_00405060(0x9540,(byte *)((int)this + 0x10));
  return uVar1 == *(uint *)((int)this + 4);
}


// ==== 00402b50 FUN_00402b50 ====

undefined4 * __fastcall FUN_00402b50(undefined4 *param_1)

{
  int iVar1;
  undefined4 *puVar2;
  
  puVar2 = param_1 + 0x14;
  for (iVar1 = 0x2500; iVar1 != 0; iVar1 = iVar1 + -1) {
    *puVar2 = 0;
    puVar2 = puVar2 + 1;
  }
  FUN_00402b70(param_1);
  return param_1;
}


// ==== 00402b70 FUN_00402b70 ====

void __fastcall FUN_00402b70(undefined4 *param_1)

{
  char cVar1;
  int iVar2;
  uint uVar3;
  uint uVar4;
  char *pcVar5;
  undefined4 *puVar6;
  char *pcVar7;
  time_t tVar8;
  
  *param_1 = 0x35576453;
  param_1[1] = 0;
  tVar8 = time((time_t *)0x0);
  param_1[2] = (int)tVar8;
  param_1[3] = 0;
  puVar6 = param_1 + 4;
  for (iVar2 = 0x10; iVar2 != 0; iVar2 = iVar2 + -1) {
    *puVar6 = 0;
    puVar6 = puVar6 + 1;
  }
  puVar6 = param_1 + 0x14;
  for (iVar2 = 0x2500; iVar2 != 0; iVar2 = iVar2 + -1) {
    *puVar6 = 0;
    puVar6 = puVar6 + 1;
  }
  puVar6 = param_1 + 0x2514;
  for (iVar2 = 0x40; iVar2 != 0; iVar2 = iVar2 + -1) {
    *puVar6 = 0;
    puVar6 = puVar6 + 1;
  }
  uVar3 = 0xffffffff;
  pcVar5 = (char *)&DAT_00407314;
  do {
    pcVar7 = pcVar5;
    if (uVar3 == 0) break;
    uVar3 = uVar3 - 1;
    pcVar7 = pcVar5 + 1;
    cVar1 = *pcVar5;
    pcVar5 = pcVar7;
  } while (cVar1 != '\0');
  uVar3 = ~uVar3;
  pcVar5 = pcVar7 + -uVar3;
  pcVar7 = (char *)(param_1 + 0x2554);
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar7 = *(undefined4 *)pcVar5;
    pcVar5 = pcVar5 + 4;
    pcVar7 = pcVar7 + 4;
  }
  for (uVar3 = uVar3 & 3; uVar3 != 0; uVar3 = uVar3 - 1) {
    *pcVar7 = *pcVar5;
    pcVar5 = pcVar5 + 1;
    pcVar7 = pcVar7 + 1;
  }
  uVar3 = 0xffffffff;
  pcVar5 = "http://charge1.soft-denchi.jp/redirect/sdrt.htm";
  do {
    pcVar7 = pcVar5;
    if (uVar3 == 0) break;
    uVar3 = uVar3 - 1;
    pcVar7 = pcVar5 + 1;
    cVar1 = *pcVar5;
    pcVar5 = pcVar7;
  } while (cVar1 != '\0');
  uVar3 = ~uVar3;
  pcVar5 = pcVar7 + -uVar3;
  pcVar7 = (char *)(param_1 + 0x2654);
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar7 = *(undefined4 *)pcVar5;
    pcVar5 = pcVar5 + 4;
    pcVar7 = pcVar7 + 4;
  }
  for (uVar3 = uVar3 & 3; uVar3 != 0; uVar3 = uVar3 - 1) {
    *pcVar7 = *pcVar5;
    pcVar5 = pcVar5 + 1;
    pcVar7 = pcVar7 + 1;
  }
  return;
}


// ==== 00402c10 FUN_00402c10 ====

bool __thiscall FUN_00402c10(void *this,int *param_1)

{
  uint uVar1;
  int iVar2;
  int *piVar3;
  
  if (*param_1 != 0x35576453) {
    return false;
  }
  piVar3 = this;
  for (iVar2 = 0x2754; iVar2 != 0; iVar2 = iVar2 + -1) {
    *piVar3 = *param_1;
    param_1 = param_1 + 1;
    piVar3 = piVar3 + 1;
  }
  FUN_00405230(0x9540,(int)this + 0x10,*(uint *)((int)this + 8));
  uVar1 = FUN_00405060(0x9540,(byte *)((int)this + 0x10));
  return uVar1 == *(uint *)((int)this + 4);
}


// ==== 00402c60 FUN_00402c60 ====

undefined4 * __fastcall FUN_00402c60(undefined4 *param_1)

{
  int iVar1;
  undefined4 *puVar2;
  
  puVar2 = param_1 + 0x14;
  for (iVar1 = 0x2500; iVar1 != 0; iVar1 = iVar1 + -1) {
    *puVar2 = 0;
    puVar2 = puVar2 + 1;
  }
  FUN_00402c80(param_1);
  return param_1;
}


// ==== 00402c80 FUN_00402c80 ====

void __fastcall FUN_00402c80(undefined4 *param_1)

{
  char cVar1;
  int iVar2;
  uint uVar3;
  uint uVar4;
  char *pcVar5;
  undefined4 *puVar6;
  char *pcVar7;
  time_t tVar8;
  
  *param_1 = 0x36576453;
  param_1[1] = 0;
  tVar8 = time((time_t *)0x0);
  param_1[2] = (int)tVar8;
  param_1[3] = 0;
  puVar6 = param_1 + 4;
  for (iVar2 = 0x10; iVar2 != 0; iVar2 = iVar2 + -1) {
    *puVar6 = 0;
    puVar6 = puVar6 + 1;
  }
  puVar6 = param_1 + 0x14;
  for (iVar2 = 0x2500; iVar2 != 0; iVar2 = iVar2 + -1) {
    *puVar6 = 0;
    puVar6 = puVar6 + 1;
  }
  puVar6 = param_1 + 0x2514;
  for (iVar2 = 0x40; iVar2 != 0; iVar2 = iVar2 + -1) {
    *puVar6 = 0;
    puVar6 = puVar6 + 1;
  }
  uVar3 = 0xffffffff;
  pcVar5 = (char *)&DAT_00407314;
  do {
    pcVar7 = pcVar5;
    if (uVar3 == 0) break;
    uVar3 = uVar3 - 1;
    pcVar7 = pcVar5 + 1;
    cVar1 = *pcVar5;
    pcVar5 = pcVar7;
  } while (cVar1 != '\0');
  uVar3 = ~uVar3;
  pcVar5 = pcVar7 + -uVar3;
  pcVar7 = (char *)(param_1 + 0x2554);
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar7 = *(undefined4 *)pcVar5;
    pcVar5 = pcVar5 + 4;
    pcVar7 = pcVar7 + 4;
  }
  for (uVar3 = uVar3 & 3; uVar3 != 0; uVar3 = uVar3 - 1) {
    *pcVar7 = *pcVar5;
    pcVar5 = pcVar5 + 1;
    pcVar7 = pcVar7 + 1;
  }
  uVar3 = 0xffffffff;
  pcVar5 = "http://charge1.soft-denchi.jp/redirect/sdrt.htm";
  do {
    pcVar7 = pcVar5;
    if (uVar3 == 0) break;
    uVar3 = uVar3 - 1;
    pcVar7 = pcVar5 + 1;
    cVar1 = *pcVar5;
    pcVar5 = pcVar7;
  } while (cVar1 != '\0');
  uVar3 = ~uVar3;
  pcVar5 = pcVar7 + -uVar3;
  pcVar7 = (char *)(param_1 + 0x2654);
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar7 = *(undefined4 *)pcVar5;
    pcVar5 = pcVar5 + 4;
    pcVar7 = pcVar7 + 4;
  }
  for (uVar3 = uVar3 & 3; uVar3 != 0; uVar3 = uVar3 - 1) {
    *pcVar7 = *pcVar5;
    pcVar5 = pcVar5 + 1;
    pcVar7 = pcVar7 + 1;
  }
  uVar3 = 0xffffffff;
  pcVar5 = (char *)&DAT_004072e4;
  do {
    pcVar7 = pcVar5;
    if (uVar3 == 0) break;
    uVar3 = uVar3 - 1;
    pcVar7 = pcVar5 + 1;
    cVar1 = *pcVar5;
    pcVar5 = pcVar7;
  } while (cVar1 != '\0');
  uVar3 = ~uVar3;
  pcVar5 = pcVar7 + -uVar3;
  pcVar7 = (char *)(param_1 + 0x2754);
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar7 = *(undefined4 *)pcVar5;
    pcVar5 = pcVar5 + 4;
    pcVar7 = pcVar7 + 4;
  }
  for (uVar3 = uVar3 & 3; uVar3 != 0; uVar3 = uVar3 - 1) {
    *pcVar7 = *pcVar5;
    pcVar5 = pcVar5 + 1;
    pcVar7 = pcVar7 + 1;
  }
  return;
}


// ==== 00402d40 FUN_00402d40 ====

bool __thiscall FUN_00402d40(void *this,int *param_1)

{
  uint uVar1;
  int iVar2;
  int *piVar3;
  
  if (*param_1 != 0x36576453) {
    return false;
  }
  piVar3 = this;
  for (iVar2 = 0x2854; iVar2 != 0; iVar2 = iVar2 + -1) {
    *piVar3 = *param_1;
    param_1 = param_1 + 1;
    piVar3 = piVar3 + 1;
  }
  FUN_00405230(0x9540,(int)this + 0x10,*(uint *)((int)this + 8));
  uVar1 = FUN_00405060(0x9540,(byte *)((int)this + 0x10));
  return uVar1 == *(uint *)((int)this + 4);
}


// ==== 00402d90 FUN_00402d90 ====

undefined4 * __fastcall FUN_00402d90(undefined4 *param_1)

{
  int iVar1;
  undefined4 *puVar2;
  
  puVar2 = param_1 + 0x14;
  for (iVar1 = 0x2500; iVar1 != 0; iVar1 = iVar1 + -1) {
    *puVar2 = 0;
    puVar2 = puVar2 + 1;
  }
  FUN_00402db0(param_1);
  return param_1;
}


// ==== 00402db0 FUN_00402db0 ====

void __fastcall FUN_00402db0(undefined4 *param_1)

{
  char cVar1;
  int iVar2;
  uint uVar3;
  uint uVar4;
  char *pcVar5;
  undefined4 *puVar6;
  char *pcVar7;
  time_t tVar8;
  
  *param_1 = 0x37576453;
  param_1[1] = 0;
  tVar8 = time((time_t *)0x0);
  param_1[2] = (int)tVar8;
  param_1[3] = 0;
  puVar6 = param_1 + 4;
  for (iVar2 = 0x10; iVar2 != 0; iVar2 = iVar2 + -1) {
    *puVar6 = 0;
    puVar6 = puVar6 + 1;
  }
  puVar6 = param_1 + 0x14;
  for (iVar2 = 0x2500; iVar2 != 0; iVar2 = iVar2 + -1) {
    *puVar6 = 0;
    puVar6 = puVar6 + 1;
  }
  puVar6 = param_1 + 0x2514;
  for (iVar2 = 0x40; iVar2 != 0; iVar2 = iVar2 + -1) {
    *puVar6 = 0;
    puVar6 = puVar6 + 1;
  }
  uVar3 = 0xffffffff;
  pcVar5 = (char *)&DAT_00407314;
  do {
    pcVar7 = pcVar5;
    if (uVar3 == 0) break;
    uVar3 = uVar3 - 1;
    pcVar7 = pcVar5 + 1;
    cVar1 = *pcVar5;
    pcVar5 = pcVar7;
  } while (cVar1 != '\0');
  uVar3 = ~uVar3;
  pcVar5 = pcVar7 + -uVar3;
  pcVar7 = (char *)(param_1 + 0x2554);
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar7 = *(undefined4 *)pcVar5;
    pcVar5 = pcVar5 + 4;
    pcVar7 = pcVar7 + 4;
  }
  for (uVar3 = uVar3 & 3; uVar3 != 0; uVar3 = uVar3 - 1) {
    *pcVar7 = *pcVar5;
    pcVar5 = pcVar5 + 1;
    pcVar7 = pcVar7 + 1;
  }
  uVar3 = 0xffffffff;
  pcVar5 = "http://charge1.soft-denchi.jp/redirect/sdrt.htm";
  do {
    pcVar7 = pcVar5;
    if (uVar3 == 0) break;
    uVar3 = uVar3 - 1;
    pcVar7 = pcVar5 + 1;
    cVar1 = *pcVar5;
    pcVar5 = pcVar7;
  } while (cVar1 != '\0');
  uVar3 = ~uVar3;
  pcVar5 = pcVar7 + -uVar3;
  pcVar7 = (char *)(param_1 + 0x2654);
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar7 = *(undefined4 *)pcVar5;
    pcVar5 = pcVar5 + 4;
    pcVar7 = pcVar7 + 4;
  }
  for (uVar3 = uVar3 & 3; uVar3 != 0; uVar3 = uVar3 - 1) {
    *pcVar7 = *pcVar5;
    pcVar5 = pcVar5 + 1;
    pcVar7 = pcVar7 + 1;
  }
  uVar3 = 0xffffffff;
  pcVar5 = (char *)&DAT_004072e4;
  do {
    pcVar7 = pcVar5;
    if (uVar3 == 0) break;
    uVar3 = uVar3 - 1;
    pcVar7 = pcVar5 + 1;
    cVar1 = *pcVar5;
    pcVar5 = pcVar7;
  } while (cVar1 != '\0');
  uVar3 = ~uVar3;
  pcVar5 = pcVar7 + -uVar3;
  pcVar7 = (char *)(param_1 + 0x2754);
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar7 = *(undefined4 *)pcVar5;
    pcVar5 = pcVar5 + 4;
    pcVar7 = pcVar7 + 4;
  }
  for (uVar3 = uVar3 & 3; uVar3 != 0; uVar3 = uVar3 - 1) {
    *pcVar7 = *pcVar5;
    pcVar5 = pcVar5 + 1;
    pcVar7 = pcVar7 + 1;
  }
  uVar3 = 0xffffffff;
  pcVar5 = "#title#";
  do {
    pcVar7 = pcVar5;
    if (uVar3 == 0) break;
    uVar3 = uVar3 - 1;
    pcVar7 = pcVar5 + 1;
    cVar1 = *pcVar5;
    pcVar5 = pcVar7;
  } while (cVar1 != '\0');
  uVar3 = ~uVar3;
  pcVar5 = pcVar7 + -uVar3;
  pcVar7 = (char *)(param_1 + 0x2854);
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar7 = *(undefined4 *)pcVar5;
    pcVar5 = pcVar5 + 4;
    pcVar7 = pcVar7 + 4;
  }
  for (uVar3 = uVar3 & 3; uVar3 != 0; uVar3 = uVar3 - 1) {
    *pcVar7 = *pcVar5;
    pcVar5 = pcVar5 + 1;
    pcVar7 = pcVar7 + 1;
  }
  uVar3 = 0xffffffff;
  pcVar5 = "00000400";
  do {
    pcVar7 = pcVar5;
    if (uVar3 == 0) break;
    uVar3 = uVar3 - 1;
    pcVar7 = pcVar5 + 1;
    cVar1 = *pcVar5;
    pcVar5 = pcVar7;
  } while (cVar1 != '\0');
  uVar3 = ~uVar3;
  pcVar5 = pcVar7 + -uVar3;
  pcVar7 = (char *)(param_1 + 0x2864);
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar7 = *(undefined4 *)pcVar5;
    pcVar5 = pcVar5 + 4;
    pcVar7 = pcVar7 + 4;
  }
  for (uVar3 = uVar3 & 3; uVar3 != 0; uVar3 = uVar3 - 1) {
    *pcVar7 = *pcVar5;
    pcVar5 = pcVar5 + 1;
    pcVar7 = pcVar7 + 1;
  }
  param_1[0x10] = 60000;
  param_1[0x13] = 2;
  param_1[0x286c] = 1000;
  return;
}


// ==== 00402ee0 FUN_00402ee0 ====

bool __thiscall FUN_00402ee0(void *this,int *param_1)

{
  uint uVar1;
  int iVar2;
  int *piVar3;
  
  if (*param_1 != 0x37576453) {
    return false;
  }
  piVar3 = this;
  for (iVar2 = 0x286d; iVar2 != 0; iVar2 = iVar2 + -1) {
    *piVar3 = *param_1;
    param_1 = param_1 + 1;
    piVar3 = piVar3 + 1;
  }
  FUN_00405230(0x9540,(int)this + 0x10,*(uint *)((int)this + 8));
  uVar1 = FUN_00405060(0x9540,(byte *)((int)this + 0x10));
  return uVar1 == *(uint *)((int)this + 4);
}


// ==== 00402f30 FUN_00402f30 ====

undefined4 __thiscall FUN_00402f30(void *this,int param_1)

{
  char cVar1;
  int iVar2;
  uint uVar3;
  uint uVar4;
  undefined4 *puVar5;
  char *pcVar6;
  undefined4 *puVar7;
  void *this_00;
  char *pcVar8;
  time_t tVar9;
  
  *(undefined4 *)this = 0x37576453;
  *(undefined4 *)((int)this + 4) = 0;
  tVar9 = time((time_t *)0x0);
  *(int *)((int)this + 8) = (int)tVar9;
  *(undefined4 *)((int)this + 0xc) = 0;
  puVar5 = (undefined4 *)(param_1 + 0x10);
  puVar7 = (undefined4 *)((int)this + 0x10);
  for (iVar2 = 0x10; iVar2 != 0; iVar2 = iVar2 + -1) {
    *puVar7 = *puVar5;
    puVar5 = puVar5 + 1;
    puVar7 = puVar7 + 1;
  }
  this_00 = (void *)((int)this + 0x50);
  puVar5 = (undefined4 *)(param_1 + 0x50);
  iVar2 = 0x10;
  do {
    FUN_004029d0(this_00,puVar5);
    puVar5 = puVar5 + 4;
    this_00 = (void *)((int)this_00 + 0x94);
    iVar2 = iVar2 + -1;
  } while (iVar2 != 0);
  puVar5 = (undefined4 *)((int)this + 0x9450);
  for (iVar2 = 0x40; iVar2 != 0; iVar2 = iVar2 + -1) {
    *puVar5 = 0;
    puVar5 = puVar5 + 1;
  }
  uVar3 = 0xffffffff;
  pcVar6 = (char *)&DAT_00407314;
  do {
    pcVar8 = pcVar6;
    if (uVar3 == 0) break;
    uVar3 = uVar3 - 1;
    pcVar8 = pcVar6 + 1;
    cVar1 = *pcVar6;
    pcVar6 = pcVar8;
  } while (cVar1 != '\0');
  uVar3 = ~uVar3;
  pcVar6 = pcVar8 + -uVar3;
  pcVar8 = (char *)((int)this + 0x9550);
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar8 = *(undefined4 *)pcVar6;
    pcVar6 = pcVar6 + 4;
    pcVar8 = pcVar8 + 4;
  }
  for (uVar3 = uVar3 & 3; uVar3 != 0; uVar3 = uVar3 - 1) {
    *pcVar8 = *pcVar6;
    pcVar6 = pcVar6 + 1;
    pcVar8 = pcVar8 + 1;
  }
  uVar3 = 0xffffffff;
  pcVar6 = "http://charge1.soft-denchi.jp/redirect/sdrt.htm";
  do {
    pcVar8 = pcVar6;
    if (uVar3 == 0) break;
    uVar3 = uVar3 - 1;
    pcVar8 = pcVar6 + 1;
    cVar1 = *pcVar6;
    pcVar6 = pcVar8;
  } while (cVar1 != '\0');
  uVar3 = ~uVar3;
  pcVar6 = pcVar8 + -uVar3;
  pcVar8 = (char *)((int)this + 0x9950);
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar8 = *(undefined4 *)pcVar6;
    pcVar6 = pcVar6 + 4;
    pcVar8 = pcVar8 + 4;
  }
  for (uVar3 = uVar3 & 3; uVar3 != 0; uVar3 = uVar3 - 1) {
    *pcVar8 = *pcVar6;
    pcVar6 = pcVar6 + 1;
    pcVar8 = pcVar8 + 1;
  }
  uVar3 = 0xffffffff;
  pcVar6 = (char *)&DAT_004072e4;
  do {
    pcVar8 = pcVar6;
    if (uVar3 == 0) break;
    uVar3 = uVar3 - 1;
    pcVar8 = pcVar6 + 1;
    cVar1 = *pcVar6;
    pcVar6 = pcVar8;
  } while (cVar1 != '\0');
  uVar3 = ~uVar3;
  pcVar6 = pcVar8 + -uVar3;
  pcVar8 = (char *)((int)this + 0x9d50);
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar8 = *(undefined4 *)pcVar6;
    pcVar6 = pcVar6 + 4;
    pcVar8 = pcVar8 + 4;
  }
  for (uVar3 = uVar3 & 3; uVar3 != 0; uVar3 = uVar3 - 1) {
    *pcVar8 = *pcVar6;
    pcVar6 = pcVar6 + 1;
    pcVar8 = pcVar8 + 1;
  }
  uVar3 = 0xffffffff;
  pcVar6 = "#title#";
  do {
    pcVar8 = pcVar6;
    if (uVar3 == 0) break;
    uVar3 = uVar3 - 1;
    pcVar8 = pcVar6 + 1;
    cVar1 = *pcVar6;
    pcVar6 = pcVar8;
  } while (cVar1 != '\0');
  uVar3 = ~uVar3;
  pcVar6 = pcVar8 + -uVar3;
  pcVar8 = (char *)((int)this + 0xa150);
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar8 = *(undefined4 *)pcVar6;
    pcVar6 = pcVar6 + 4;
    pcVar8 = pcVar8 + 4;
  }
  for (uVar3 = uVar3 & 3; uVar3 != 0; uVar3 = uVar3 - 1) {
    *pcVar8 = *pcVar6;
    pcVar6 = pcVar6 + 1;
    pcVar8 = pcVar8 + 1;
  }
  uVar3 = 0xffffffff;
  pcVar6 = "00000400";
  do {
    pcVar8 = pcVar6;
    if (uVar3 == 0) break;
    uVar3 = uVar3 - 1;
    pcVar8 = pcVar6 + 1;
    cVar1 = *pcVar6;
    pcVar6 = pcVar8;
  } while (cVar1 != '\0');
  uVar3 = ~uVar3;
  pcVar6 = pcVar8 + -uVar3;
  pcVar8 = (char *)((int)this + 0xa190);
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar8 = *(undefined4 *)pcVar6;
    pcVar6 = pcVar6 + 4;
    pcVar8 = pcVar8 + 4;
  }
  for (uVar4 = uVar3 & 3; uVar4 != 0; uVar4 = uVar4 - 1) {
    *pcVar8 = *pcVar6;
    pcVar6 = pcVar6 + 1;
    pcVar8 = pcVar8 + 1;
  }
  return CONCAT31((int3)(uVar3 >> 8),1);
}


// ==== 00403060 FUN_00403060 ====

undefined4 __thiscall FUN_00403060(void *this,int param_1)

{
  char cVar1;
  undefined4 uVar2;
  int iVar3;
  uint uVar4;
  void *this_00;
  undefined4 *puVar5;
  uint uVar6;
  char *pcVar7;
  undefined4 *puVar8;
  LPCSTR pCVar9;
  char *pcVar10;
  time_t tVar11;
  
  *(undefined4 *)this = 0x37576453;
  *(undefined4 *)((int)this + 4) = 0;
  tVar11 = time((time_t *)0x0);
  *(int *)((int)this + 8) = (int)tVar11;
  *(undefined4 *)((int)this + 0xc) = 0;
  puVar5 = (undefined4 *)(param_1 + 0x10);
  puVar8 = (undefined4 *)((int)this + 0x10);
  for (iVar3 = 0x10; iVar3 != 0; iVar3 = iVar3 + -1) {
    *puVar8 = *puVar5;
    puVar5 = puVar5 + 1;
    puVar8 = puVar8 + 1;
  }
  uVar6 = 0;
  this_00 = (void *)((int)this + 0x50);
  pCVar9 = (LPCSTR)(param_1 + 0x50);
  do {
    uVar2 = FUN_00402a10(this_00,pCVar9);
    if ((char)uVar2 == '\0') break;
    uVar6 = uVar6 + 1;
    pCVar9 = pCVar9 + 0x114;
    this_00 = (void *)((int)this_00 + 0x94);
  } while (uVar6 < 0x10);
  puVar5 = (undefined4 *)((int)this + 0x9450);
  for (iVar3 = 0x40; iVar3 != 0; iVar3 = iVar3 + -1) {
    *puVar5 = 0;
    puVar5 = puVar5 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar7 = (char *)&DAT_00407314;
  do {
    pcVar10 = pcVar7;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar10 = pcVar7 + 1;
    cVar1 = *pcVar7;
    pcVar7 = pcVar10;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar7 = pcVar10 + -uVar6;
  pcVar10 = (char *)((int)this + 0x9550);
  for (uVar4 = uVar6 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar7;
    pcVar7 = pcVar7 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar10 = *pcVar7;
    pcVar7 = pcVar7 + 1;
    pcVar10 = pcVar10 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar7 = "http://charge1.soft-denchi.jp/redirect/sdrt.htm";
  do {
    pcVar10 = pcVar7;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar10 = pcVar7 + 1;
    cVar1 = *pcVar7;
    pcVar7 = pcVar10;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar7 = pcVar10 + -uVar6;
  pcVar10 = (char *)((int)this + 0x9950);
  for (uVar4 = uVar6 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar7;
    pcVar7 = pcVar7 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar10 = *pcVar7;
    pcVar7 = pcVar7 + 1;
    pcVar10 = pcVar10 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar7 = (char *)&DAT_004072e4;
  do {
    pcVar10 = pcVar7;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar10 = pcVar7 + 1;
    cVar1 = *pcVar7;
    pcVar7 = pcVar10;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar7 = pcVar10 + -uVar6;
  pcVar10 = (char *)((int)this + 0x9d50);
  for (uVar4 = uVar6 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar7;
    pcVar7 = pcVar7 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar10 = *pcVar7;
    pcVar7 = pcVar7 + 1;
    pcVar10 = pcVar10 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar7 = "#title#";
  do {
    pcVar10 = pcVar7;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar10 = pcVar7 + 1;
    cVar1 = *pcVar7;
    pcVar7 = pcVar10;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar7 = pcVar10 + -uVar6;
  pcVar10 = (char *)((int)this + 0xa150);
  for (uVar4 = uVar6 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar7;
    pcVar7 = pcVar7 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar10 = *pcVar7;
    pcVar7 = pcVar7 + 1;
    pcVar10 = pcVar10 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar7 = "00000400";
  do {
    pcVar10 = pcVar7;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar10 = pcVar7 + 1;
    cVar1 = *pcVar7;
    pcVar7 = pcVar10;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar7 = pcVar10 + -uVar6;
  pcVar10 = (char *)((int)this + 0xa190);
  for (uVar4 = uVar6 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar7;
    pcVar7 = pcVar7 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar4 = uVar6 & 3; uVar4 != 0; uVar4 = uVar4 - 1) {
    *pcVar10 = *pcVar7;
    pcVar7 = pcVar7 + 1;
    pcVar10 = pcVar10 + 1;
  }
  return CONCAT31((int3)(uVar6 >> 8),1);
}


// ==== 004031a0 FUN_004031a0 ====

undefined4 __thiscall FUN_004031a0(void *this,int param_1)

{
  char cVar1;
  undefined4 uVar2;
  int iVar3;
  uint uVar4;
  void *this_00;
  undefined4 *puVar5;
  uint uVar6;
  char *pcVar7;
  undefined4 *puVar8;
  LPCSTR pCVar9;
  char *pcVar10;
  time_t tVar11;
  
  *(undefined4 *)this = 0x37576453;
  *(undefined4 *)((int)this + 4) = 0;
  tVar11 = time((time_t *)0x0);
  *(int *)((int)this + 8) = (int)tVar11;
  *(undefined4 *)((int)this + 0xc) = 0;
  puVar5 = (undefined4 *)(param_1 + 0x10);
  puVar8 = (undefined4 *)((int)this + 0x10);
  for (iVar3 = 0x10; iVar3 != 0; iVar3 = iVar3 + -1) {
    *puVar8 = *puVar5;
    puVar5 = puVar5 + 1;
    puVar8 = puVar8 + 1;
  }
  uVar6 = 0;
  this_00 = (void *)((int)this + 0x50);
  pCVar9 = (LPCSTR)(param_1 + 0x50);
  do {
    uVar2 = FUN_00402a10(this_00,pCVar9);
    if ((char)uVar2 == '\0') break;
    uVar6 = uVar6 + 1;
    pCVar9 = pCVar9 + 0x114;
    this_00 = (void *)((int)this_00 + 0x94);
  } while (uVar6 < 0x10);
  puVar5 = (undefined4 *)((int)this + 0x9450);
  for (iVar3 = 0x40; iVar3 != 0; iVar3 = iVar3 + -1) {
    *puVar5 = 0;
    puVar5 = puVar5 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar7 = (char *)&DAT_00407314;
  do {
    pcVar10 = pcVar7;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar10 = pcVar7 + 1;
    cVar1 = *pcVar7;
    pcVar7 = pcVar10;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar7 = pcVar10 + -uVar6;
  pcVar10 = (char *)((int)this + 0x9550);
  for (uVar4 = uVar6 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar7;
    pcVar7 = pcVar7 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar10 = *pcVar7;
    pcVar7 = pcVar7 + 1;
    pcVar10 = pcVar10 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar7 = "http://charge1.soft-denchi.jp/redirect/sdrt.htm";
  do {
    pcVar10 = pcVar7;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar10 = pcVar7 + 1;
    cVar1 = *pcVar7;
    pcVar7 = pcVar10;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar7 = pcVar10 + -uVar6;
  pcVar10 = (char *)((int)this + 0x9950);
  for (uVar4 = uVar6 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar7;
    pcVar7 = pcVar7 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar10 = *pcVar7;
    pcVar7 = pcVar7 + 1;
    pcVar10 = pcVar10 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar7 = (char *)&DAT_004072e4;
  do {
    pcVar10 = pcVar7;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar10 = pcVar7 + 1;
    cVar1 = *pcVar7;
    pcVar7 = pcVar10;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar7 = pcVar10 + -uVar6;
  pcVar10 = (char *)((int)this + 0x9d50);
  for (uVar4 = uVar6 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar7;
    pcVar7 = pcVar7 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar10 = *pcVar7;
    pcVar7 = pcVar7 + 1;
    pcVar10 = pcVar10 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar7 = "#title#";
  do {
    pcVar10 = pcVar7;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar10 = pcVar7 + 1;
    cVar1 = *pcVar7;
    pcVar7 = pcVar10;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar7 = pcVar10 + -uVar6;
  pcVar10 = (char *)((int)this + 0xa150);
  for (uVar4 = uVar6 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar7;
    pcVar7 = pcVar7 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar10 = *pcVar7;
    pcVar7 = pcVar7 + 1;
    pcVar10 = pcVar10 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar7 = "00000400";
  do {
    pcVar10 = pcVar7;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar10 = pcVar7 + 1;
    cVar1 = *pcVar7;
    pcVar7 = pcVar10;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar7 = pcVar10 + -uVar6;
  pcVar10 = (char *)((int)this + 0xa190);
  for (uVar4 = uVar6 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar7;
    pcVar7 = pcVar7 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar4 = uVar6 & 3; uVar4 != 0; uVar4 = uVar4 - 1) {
    *pcVar10 = *pcVar7;
    pcVar7 = pcVar7 + 1;
    pcVar10 = pcVar10 + 1;
  }
  return CONCAT31((int3)(uVar6 >> 8),1);
}


// ==== 004032e0 FUN_004032e0 ====

undefined4 __thiscall FUN_004032e0(void *this,int param_1)

{
  char cVar1;
  undefined4 *puVar2;
  int iVar3;
  uint uVar4;
  uint uVar5;
  undefined4 *puVar6;
  undefined4 *puVar7;
  char *pcVar8;
  undefined4 *puVar9;
  char *pcVar10;
  time_t tVar11;
  int local_4;
  
  *(undefined4 *)this = 0x37576453;
  *(undefined4 *)((int)this + 4) = 0;
  tVar11 = time((time_t *)0x0);
  *(undefined4 *)((int)this + 0xc) = 0;
  *(int *)((int)this + 8) = (int)tVar11;
  puVar2 = (undefined4 *)(param_1 + 0x10);
  puVar6 = (undefined4 *)((int)this + 0x10);
  for (iVar3 = 0x10; iVar3 != 0; iVar3 = iVar3 + -1) {
    *puVar6 = *puVar2;
    puVar2 = puVar2 + 1;
    puVar6 = puVar6 + 1;
  }
  puVar2 = (undefined4 *)((int)this + 0xd0);
  local_4 = 0x100;
  puVar6 = (undefined4 *)(param_1 + 0xd8);
  do {
    puVar7 = puVar6 + -0x22;
    puVar9 = puVar2 + -0x20;
    for (iVar3 = 0x20; iVar3 != 0; iVar3 = iVar3 + -1) {
      *puVar9 = *puVar7;
      puVar7 = puVar7 + 1;
      puVar9 = puVar9 + 1;
    }
    *puVar2 = *(undefined4 *)((int)puVar2 + (param_1 - (int)this));
    puVar2[1] = puVar6[-1];
    puVar2[2] = *puVar6;
    puVar2[3] = puVar6[1];
    puVar2[4] = puVar6[2];
    puVar2 = puVar2 + 0x25;
    local_4 = local_4 + -1;
    puVar6 = puVar6 + 0x25;
  } while (local_4 != 0);
  uVar4 = 0xffffffff;
  pcVar8 = (char *)(param_1 + 0x9450);
  do {
    pcVar10 = pcVar8;
    if (uVar4 == 0) break;
    uVar4 = uVar4 - 1;
    pcVar10 = pcVar8 + 1;
    cVar1 = *pcVar8;
    pcVar8 = pcVar10;
  } while (cVar1 != '\0');
  uVar4 = ~uVar4;
  pcVar8 = pcVar10 + -uVar4;
  pcVar10 = (char *)((int)this + 0x9450);
  for (uVar5 = uVar4 >> 2; uVar5 != 0; uVar5 = uVar5 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar8;
    pcVar8 = pcVar8 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar4 = uVar4 & 3; uVar4 != 0; uVar4 = uVar4 - 1) {
    *pcVar10 = *pcVar8;
    pcVar8 = pcVar8 + 1;
    pcVar10 = pcVar10 + 1;
  }
  uVar4 = 0xffffffff;
  pcVar8 = (char *)&DAT_00407314;
  do {
    pcVar10 = pcVar8;
    if (uVar4 == 0) break;
    uVar4 = uVar4 - 1;
    pcVar10 = pcVar8 + 1;
    cVar1 = *pcVar8;
    pcVar8 = pcVar10;
  } while (cVar1 != '\0');
  uVar4 = ~uVar4;
  pcVar8 = pcVar10 + -uVar4;
  pcVar10 = (char *)((int)this + 0x9550);
  for (uVar5 = uVar4 >> 2; uVar5 != 0; uVar5 = uVar5 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar8;
    pcVar8 = pcVar8 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar4 = uVar4 & 3; uVar4 != 0; uVar4 = uVar4 - 1) {
    *pcVar10 = *pcVar8;
    pcVar8 = pcVar8 + 1;
    pcVar10 = pcVar10 + 1;
  }
  uVar4 = 0xffffffff;
  pcVar8 = "http://charge1.soft-denchi.jp/redirect/sdrt.htm";
  do {
    pcVar10 = pcVar8;
    if (uVar4 == 0) break;
    uVar4 = uVar4 - 1;
    pcVar10 = pcVar8 + 1;
    cVar1 = *pcVar8;
    pcVar8 = pcVar10;
  } while (cVar1 != '\0');
  uVar4 = ~uVar4;
  pcVar8 = pcVar10 + -uVar4;
  pcVar10 = (char *)((int)this + 0x9950);
  for (uVar5 = uVar4 >> 2; uVar5 != 0; uVar5 = uVar5 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar8;
    pcVar8 = pcVar8 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar4 = uVar4 & 3; uVar4 != 0; uVar4 = uVar4 - 1) {
    *pcVar10 = *pcVar8;
    pcVar8 = pcVar8 + 1;
    pcVar10 = pcVar10 + 1;
  }
  uVar4 = 0xffffffff;
  pcVar8 = (char *)&DAT_004072e4;
  do {
    pcVar10 = pcVar8;
    if (uVar4 == 0) break;
    uVar4 = uVar4 - 1;
    pcVar10 = pcVar8 + 1;
    cVar1 = *pcVar8;
    pcVar8 = pcVar10;
  } while (cVar1 != '\0');
  uVar4 = ~uVar4;
  pcVar8 = pcVar10 + -uVar4;
  pcVar10 = (char *)((int)this + 0x9d50);
  for (uVar5 = uVar4 >> 2; uVar5 != 0; uVar5 = uVar5 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar8;
    pcVar8 = pcVar8 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar4 = uVar4 & 3; uVar4 != 0; uVar4 = uVar4 - 1) {
    *pcVar10 = *pcVar8;
    pcVar8 = pcVar8 + 1;
    pcVar10 = pcVar10 + 1;
  }
  uVar4 = 0xffffffff;
  pcVar8 = "#title#";
  do {
    pcVar10 = pcVar8;
    if (uVar4 == 0) break;
    uVar4 = uVar4 - 1;
    pcVar10 = pcVar8 + 1;
    cVar1 = *pcVar8;
    pcVar8 = pcVar10;
  } while (cVar1 != '\0');
  uVar4 = ~uVar4;
  pcVar8 = pcVar10 + -uVar4;
  pcVar10 = (char *)((int)this + 0xa150);
  for (uVar5 = uVar4 >> 2; uVar5 != 0; uVar5 = uVar5 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar8;
    pcVar8 = pcVar8 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar4 = uVar4 & 3; uVar4 != 0; uVar4 = uVar4 - 1) {
    *pcVar10 = *pcVar8;
    pcVar8 = pcVar8 + 1;
    pcVar10 = pcVar10 + 1;
  }
  uVar4 = 0xffffffff;
  pcVar8 = "00000400";
  do {
    pcVar10 = pcVar8;
    if (uVar4 == 0) break;
    uVar4 = uVar4 - 1;
    pcVar10 = pcVar8 + 1;
    cVar1 = *pcVar8;
    pcVar8 = pcVar10;
  } while (cVar1 != '\0');
  uVar4 = ~uVar4;
  pcVar8 = pcVar10 + -uVar4;
  pcVar10 = (char *)((int)this + 0xa190);
  for (uVar5 = uVar4 >> 2; uVar5 != 0; uVar5 = uVar5 - 1) {
    *(undefined4 *)pcVar10 = *(undefined4 *)pcVar8;
    pcVar8 = pcVar8 + 4;
    pcVar10 = pcVar10 + 4;
  }
  for (uVar5 = uVar4 & 3; uVar5 != 0; uVar5 = uVar5 - 1) {
    *pcVar10 = *pcVar8;
    pcVar8 = pcVar8 + 1;
    pcVar10 = pcVar10 + 1;
  }
  return CONCAT31((int3)(uVar4 >> 8),1);
}


// ==== 00403480 FUN_00403480 ====

undefined4 __thiscall FUN_00403480(void *this,int param_1)

{
  char cVar1;
  int iVar2;
  undefined4 *puVar3;
  int iVar4;
  int iVar5;
  uint uVar6;
  uint uVar7;
  undefined4 *puVar8;
  undefined4 *puVar9;
  char *pcVar10;
  undefined4 *puVar11;
  char *pcVar12;
  time_t tVar13;
  
  iVar2 = param_1;
  *(undefined4 *)this = 0x37576453;
  *(undefined4 *)((int)this + 4) = 0;
  tVar13 = time((time_t *)0x0);
  *(undefined4 *)((int)this + 0xc) = 0;
  *(int *)((int)this + 8) = (int)tVar13;
  puVar8 = (undefined4 *)(param_1 + 0x10);
  puVar3 = (undefined4 *)((int)this + 0x10);
  for (iVar4 = 0x10; iVar4 != 0; iVar4 = iVar4 + -1) {
    *puVar3 = *puVar8;
    puVar8 = puVar8 + 1;
    puVar3 = puVar3 + 1;
  }
  iVar4 = param_1 - (int)this;
  puVar8 = (undefined4 *)(param_1 + 0xd8);
  param_1 = 0x100;
  puVar3 = (undefined4 *)((int)this + 0xd0);
  do {
    puVar9 = puVar8 + -0x22;
    puVar11 = puVar3 + -0x20;
    for (iVar5 = 0x20; iVar5 != 0; iVar5 = iVar5 + -1) {
      *puVar11 = *puVar9;
      puVar9 = puVar9 + 1;
      puVar11 = puVar11 + 1;
    }
    *puVar3 = *(undefined4 *)(iVar4 + -0x94 + (int)(puVar3 + 0x25));
    puVar3[1] = puVar8[-1];
    puVar3[2] = *puVar8;
    puVar3[3] = puVar8[1];
    puVar3[4] = puVar8[2];
    param_1 = param_1 + -1;
    puVar3 = puVar3 + 0x25;
    puVar8 = puVar8 + 0x25;
  } while (param_1 != 0);
  uVar6 = 0xffffffff;
  pcVar10 = (char *)(iVar2 + 0x9450);
  do {
    pcVar12 = pcVar10;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar12 = pcVar10 + 1;
    cVar1 = *pcVar10;
    pcVar10 = pcVar12;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar10 = pcVar12 + -uVar6;
  pcVar12 = (char *)((int)this + 0x9450);
  for (uVar7 = uVar6 >> 2; uVar7 != 0; uVar7 = uVar7 - 1) {
    *(undefined4 *)pcVar12 = *(undefined4 *)pcVar10;
    pcVar10 = pcVar10 + 4;
    pcVar12 = pcVar12 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar12 = *pcVar10;
    pcVar10 = pcVar10 + 1;
    pcVar12 = pcVar12 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar10 = (char *)(iVar2 + 0x9550);
  do {
    pcVar12 = pcVar10;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar12 = pcVar10 + 1;
    cVar1 = *pcVar10;
    pcVar10 = pcVar12;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar10 = pcVar12 + -uVar6;
  pcVar12 = (char *)((int)this + 0x9550);
  for (uVar7 = uVar6 >> 2; uVar7 != 0; uVar7 = uVar7 - 1) {
    *(undefined4 *)pcVar12 = *(undefined4 *)pcVar10;
    pcVar10 = pcVar10 + 4;
    pcVar12 = pcVar12 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar12 = *pcVar10;
    pcVar10 = pcVar10 + 1;
    pcVar12 = pcVar12 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar10 = (char *)(iVar2 + 0x9950);
  do {
    pcVar12 = pcVar10;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar12 = pcVar10 + 1;
    cVar1 = *pcVar10;
    pcVar10 = pcVar12;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar10 = pcVar12 + -uVar6;
  pcVar12 = (char *)((int)this + 0x9950);
  for (uVar7 = uVar6 >> 2; uVar7 != 0; uVar7 = uVar7 - 1) {
    *(undefined4 *)pcVar12 = *(undefined4 *)pcVar10;
    pcVar10 = pcVar10 + 4;
    pcVar12 = pcVar12 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar12 = *pcVar10;
    pcVar10 = pcVar10 + 1;
    pcVar12 = pcVar12 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar10 = (char *)&DAT_004072e4;
  do {
    pcVar12 = pcVar10;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar12 = pcVar10 + 1;
    cVar1 = *pcVar10;
    pcVar10 = pcVar12;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar10 = pcVar12 + -uVar6;
  pcVar12 = (char *)((int)this + 0x9d50);
  for (uVar7 = uVar6 >> 2; uVar7 != 0; uVar7 = uVar7 - 1) {
    *(undefined4 *)pcVar12 = *(undefined4 *)pcVar10;
    pcVar10 = pcVar10 + 4;
    pcVar12 = pcVar12 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar12 = *pcVar10;
    pcVar10 = pcVar10 + 1;
    pcVar12 = pcVar12 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar10 = "#title#";
  do {
    pcVar12 = pcVar10;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar12 = pcVar10 + 1;
    cVar1 = *pcVar10;
    pcVar10 = pcVar12;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar10 = pcVar12 + -uVar6;
  pcVar12 = (char *)((int)this + 0xa150);
  for (uVar7 = uVar6 >> 2; uVar7 != 0; uVar7 = uVar7 - 1) {
    *(undefined4 *)pcVar12 = *(undefined4 *)pcVar10;
    pcVar10 = pcVar10 + 4;
    pcVar12 = pcVar12 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar12 = *pcVar10;
    pcVar10 = pcVar10 + 1;
    pcVar12 = pcVar12 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar10 = "00000400";
  do {
    pcVar12 = pcVar10;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar12 = pcVar10 + 1;
    cVar1 = *pcVar10;
    pcVar10 = pcVar12;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar10 = pcVar12 + -uVar6;
  pcVar12 = (char *)((int)this + 0xa190);
  for (uVar7 = uVar6 >> 2; uVar7 != 0; uVar7 = uVar7 - 1) {
    *(undefined4 *)pcVar12 = *(undefined4 *)pcVar10;
    pcVar10 = pcVar10 + 4;
    pcVar12 = pcVar12 + 4;
  }
  for (uVar7 = uVar6 & 3; uVar7 != 0; uVar7 = uVar7 - 1) {
    *pcVar12 = *pcVar10;
    pcVar10 = pcVar10 + 1;
    pcVar12 = pcVar12 + 1;
  }
  return CONCAT31((int3)(uVar6 >> 8),1);
}


// ==== 00403640 FUN_00403640 ====

undefined4 __thiscall FUN_00403640(void *this,int param_1)

{
  char cVar1;
  int iVar2;
  undefined4 *puVar3;
  int iVar4;
  int iVar5;
  uint uVar6;
  uint uVar7;
  undefined4 *puVar8;
  undefined4 *puVar9;
  char *pcVar10;
  undefined4 *puVar11;
  char *pcVar12;
  time_t tVar13;
  
  iVar2 = param_1;
  *(undefined4 *)this = 0x37576453;
  *(undefined4 *)((int)this + 4) = 0;
  tVar13 = time((time_t *)0x0);
  *(undefined4 *)((int)this + 0xc) = 0;
  *(int *)((int)this + 8) = (int)tVar13;
  puVar8 = (undefined4 *)(param_1 + 0x10);
  puVar3 = (undefined4 *)((int)this + 0x10);
  for (iVar4 = 0x10; iVar4 != 0; iVar4 = iVar4 + -1) {
    *puVar3 = *puVar8;
    puVar8 = puVar8 + 1;
    puVar3 = puVar3 + 1;
  }
  iVar4 = param_1 - (int)this;
  puVar8 = (undefined4 *)(param_1 + 0xd8);
  param_1 = 0x100;
  puVar3 = (undefined4 *)((int)this + 0xd0);
  do {
    puVar9 = puVar8 + -0x22;
    puVar11 = puVar3 + -0x20;
    for (iVar5 = 0x20; iVar5 != 0; iVar5 = iVar5 + -1) {
      *puVar11 = *puVar9;
      puVar9 = puVar9 + 1;
      puVar11 = puVar11 + 1;
    }
    *puVar3 = *(undefined4 *)(iVar4 + -0x94 + (int)(puVar3 + 0x25));
    puVar3[1] = puVar8[-1];
    puVar3[2] = *puVar8;
    puVar3[3] = puVar8[1];
    puVar3[4] = puVar8[2];
    param_1 = param_1 + -1;
    puVar3 = puVar3 + 0x25;
    puVar8 = puVar8 + 0x25;
  } while (param_1 != 0);
  uVar6 = 0xffffffff;
  pcVar10 = (char *)(iVar2 + 0x9450);
  do {
    pcVar12 = pcVar10;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar12 = pcVar10 + 1;
    cVar1 = *pcVar10;
    pcVar10 = pcVar12;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar10 = pcVar12 + -uVar6;
  pcVar12 = (char *)((int)this + 0x9450);
  for (uVar7 = uVar6 >> 2; uVar7 != 0; uVar7 = uVar7 - 1) {
    *(undefined4 *)pcVar12 = *(undefined4 *)pcVar10;
    pcVar10 = pcVar10 + 4;
    pcVar12 = pcVar12 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar12 = *pcVar10;
    pcVar10 = pcVar10 + 1;
    pcVar12 = pcVar12 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar10 = (char *)(iVar2 + 0x9550);
  do {
    pcVar12 = pcVar10;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar12 = pcVar10 + 1;
    cVar1 = *pcVar10;
    pcVar10 = pcVar12;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar10 = pcVar12 + -uVar6;
  pcVar12 = (char *)((int)this + 0x9550);
  for (uVar7 = uVar6 >> 2; uVar7 != 0; uVar7 = uVar7 - 1) {
    *(undefined4 *)pcVar12 = *(undefined4 *)pcVar10;
    pcVar10 = pcVar10 + 4;
    pcVar12 = pcVar12 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar12 = *pcVar10;
    pcVar10 = pcVar10 + 1;
    pcVar12 = pcVar12 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar10 = (char *)(iVar2 + 0x9950);
  do {
    pcVar12 = pcVar10;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar12 = pcVar10 + 1;
    cVar1 = *pcVar10;
    pcVar10 = pcVar12;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar10 = pcVar12 + -uVar6;
  pcVar12 = (char *)((int)this + 0x9950);
  for (uVar7 = uVar6 >> 2; uVar7 != 0; uVar7 = uVar7 - 1) {
    *(undefined4 *)pcVar12 = *(undefined4 *)pcVar10;
    pcVar10 = pcVar10 + 4;
    pcVar12 = pcVar12 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar12 = *pcVar10;
    pcVar10 = pcVar10 + 1;
    pcVar12 = pcVar12 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar10 = (char *)(iVar2 + 0x9d50);
  do {
    pcVar12 = pcVar10;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar12 = pcVar10 + 1;
    cVar1 = *pcVar10;
    pcVar10 = pcVar12;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar10 = pcVar12 + -uVar6;
  pcVar12 = (char *)((int)this + 0x9d50);
  for (uVar7 = uVar6 >> 2; uVar7 != 0; uVar7 = uVar7 - 1) {
    *(undefined4 *)pcVar12 = *(undefined4 *)pcVar10;
    pcVar10 = pcVar10 + 4;
    pcVar12 = pcVar12 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar12 = *pcVar10;
    pcVar10 = pcVar10 + 1;
    pcVar12 = pcVar12 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar10 = "#title#";
  do {
    pcVar12 = pcVar10;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar12 = pcVar10 + 1;
    cVar1 = *pcVar10;
    pcVar10 = pcVar12;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar10 = pcVar12 + -uVar6;
  pcVar12 = (char *)((int)this + 0xa150);
  for (uVar7 = uVar6 >> 2; uVar7 != 0; uVar7 = uVar7 - 1) {
    *(undefined4 *)pcVar12 = *(undefined4 *)pcVar10;
    pcVar10 = pcVar10 + 4;
    pcVar12 = pcVar12 + 4;
  }
  for (uVar6 = uVar6 & 3; uVar6 != 0; uVar6 = uVar6 - 1) {
    *pcVar12 = *pcVar10;
    pcVar10 = pcVar10 + 1;
    pcVar12 = pcVar12 + 1;
  }
  uVar6 = 0xffffffff;
  pcVar10 = "00000400";
  do {
    pcVar12 = pcVar10;
    if (uVar6 == 0) break;
    uVar6 = uVar6 - 1;
    pcVar12 = pcVar10 + 1;
    cVar1 = *pcVar10;
    pcVar10 = pcVar12;
  } while (cVar1 != '\0');
  uVar6 = ~uVar6;
  pcVar10 = pcVar12 + -uVar6;
  pcVar12 = (char *)((int)this + 0xa190);
  for (uVar7 = uVar6 >> 2; uVar7 != 0; uVar7 = uVar7 - 1) {
    *(undefined4 *)pcVar12 = *(undefined4 *)pcVar10;
    pcVar10 = pcVar10 + 4;
    pcVar12 = pcVar12 + 4;
  }
  for (uVar7 = uVar6 & 3; uVar7 != 0; uVar7 = uVar7 - 1) {
    *pcVar12 = *pcVar10;
    pcVar10 = pcVar10 + 1;
    pcVar12 = pcVar12 + 1;
  }
  return CONCAT31((int3)(uVar6 >> 8),1);
}


// ==== 00403800 FUN_00403800 ====

undefined1 * FUN_00403800(void)

{
  HMODULE hModule;
  DWORD DVar1;
  
  if (DAT_00413bf8 == '\0') {
    hModule = GetModuleHandleA((LPCSTR)0x0);
    DVar1 = GetModuleFileNameA(hModule,&DAT_00413bf8,0x104);
    if (DVar1 == 0) {
      return (undefined1 *)0x0;
    }
  }
  return &DAT_00413bf8;
}


// ==== 00403830 FUN_00403830 ====

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

undefined4 __cdecl
FUN_00403830(undefined4 param_1,char *param_2,uint param_3,char *param_4,LPCSTR param_5,int param_6,
            DWORD param_7)

{
  char cVar1;
  LPCSTR lpszLongPath;
  uint uVar2;
  int iVar3;
  BOOL BVar4;
  DWORD DVar5;
  HANDLE hObject;
  undefined4 uVar6;
  uintptr_t uVar7;
  int iVar8;
  _STARTUPINFOA *p_Var9;
  char *pcVar10;
  LPCSTR pCVar11;
  _STARTUPINFOA local_18c;
  CHAR local_148 [64];
  CHAR local_108 [264];
  
  DAT_00413e24 = param_6;
  DAT_00413e20 = param_5;
  _DAT_00413e28 = param_3;
  DAT_00413e18 = param_1;
  lpszLongPath = FUN_00403b20(param_3,param_4);
  if (lpszLongPath == (LPCSTR)0x0) {
    return 0;
  }
  if ((param_3 & 0x100) != 0) {
    cVar1 = FUN_00404230(lpszLongPath,(uint)param_5,param_6,0);
    if (cVar1 == '\0') {
      FUN_004046c0(s_unpatch_failed__00409160);
      CloseHandle(DAT_00413d04);
      uVar2 = CloseHandle(DAT_00413d00);
      return uVar2 & 0xffffff00;
    }
  }
  if ((((param_3 & 0x48) != 0) || (param_4 == (char *)0x0)) || (*param_4 == '\0')) {
    param_4 = param_2;
  }
  if (param_4 == (char *)0x0) {
    param_4 = &DAT_004139e4;
  }
  iVar3 = FUN_00403d90(lpszLongPath);
  if ((param_3 & 1) == 0) {
    pcVar10 = s___s___s_00409150;
    pCVar11 = lpszLongPath;
  }
  else {
    GetShortPathNameA(lpszLongPath,local_108,0x104);
    pcVar10 = s__s__s_00409158;
    pCVar11 = local_108;
  }
  wsprintfA(&DAT_00413d10,pcVar10,pCVar11,param_4);
  p_Var9 = &local_18c;
  for (iVar8 = 0x11; iVar8 != 0; iVar8 = iVar8 + -1) {
    p_Var9->cb = 0;
    p_Var9 = (_STARTUPINFOA *)&p_Var9->lpReserved;
  }
  DAT_00413d00 = (HANDLE)0x0;
  DAT_00413d04 = (HANDLE)0x0;
  DAT_00413d08 = 0;
  local_18c.cb = 0x44;
  _DAT_00413d0c = 0;
  BVar4 = CreateProcessA(lpszLongPath,&DAT_00413d10,(LPSECURITY_ATTRIBUTES)0x0,
                         (LPSECURITY_ATTRIBUTES)0x0,0,0x24,(LPVOID)0x0,(LPCSTR)0x0,&local_18c,
                         (LPPROCESS_INFORMATION)&DAT_00413d00);
  if (BVar4 == 0) {
    DVar5 = GetLastError();
    wsprintfA(local_148,s_CreateProcess__d_0040913c,DVar5);
    uVar2 = FUN_004046c0(local_148);
    return uVar2 & 0xffffff00;
  }
  GetCurrentDirectoryA(0x104,local_108);
  FUN_004046c0(local_108);
  FUN_004046c0(lpszLongPath);
  FUN_004046c0(&DAT_00413d10);
  hObject = OpenProcess(0x1f0fff,0,DAT_00413d08);
  if (hObject == (HANDLE)0x0) {
    FUN_004046c0(s_OpenProcess_failed__00409128);
    CloseHandle(DAT_00413d04);
    uVar2 = CloseHandle(DAT_00413d00);
    return uVar2 & 0xffffff00;
  }
  if ((param_3 & 0x100) == 0) {
    uVar6 = FUN_00403e30(hObject,iVar3,param_5,param_6);
    if ((char)uVar6 == '\0') {
      FUN_004046c0(s_unpatch_failed__00409160);
      CloseHandle(DAT_00413d04);
      uVar2 = CloseHandle(DAT_00413d00);
      return uVar2 & 0xffffff00;
    }
  }
  CloseHandle(hObject);
  ResumeThread(DAT_00413d04);
  uVar7 = _beginthread((_StartAddress *)&LAB_00404040,0,DAT_00413d00);
  if ((param_3 & 0x200) != 0) {
    Sleep(param_7);
    uVar7 = FUN_00403f00();
  }
  return CONCAT31((int3)(uVar7 >> 8),1);
}


// ==== 00403b00 FUN_00403b00 ====

undefined4 __cdecl FUN_00403b00(byte param_1,char *param_2)

{
  char *in_EAX;
  
  if ((((param_1 & 0x48) == 0) || (in_EAX = param_2, param_2 == (char *)0x0)) || (*param_2 == '\0'))
  {
    return (uint)in_EAX & 0xffffff00;
  }
  return CONCAT31((int3)((uint)param_2 >> 8),1);
}


// ==== 00403b20 FUN_00403b20 ====

undefined * __cdecl FUN_00403b20(uint param_1,char *param_2)

{
  undefined4 uVar1;
  DWORD DVar2;
  HANDLE hFile;
  int iVar3;
  void *lpBuffer;
  BOOL BVar4;
  HANDLE hFile_00;
  uint nNumberOfBytesToRead;
  DWORD lDistanceToMove;
  char *_Ext;
  char local_30c [4];
  DWORD local_308;
  uint local_304;
  char local_300 [256];
  char local_200 [256];
  char local_100 [256];
  
  _splitpath(&DAT_00413bf8,local_30c,local_300,local_100,local_200);
  uVar1 = FUN_00403b00((byte)param_1,param_2);
  if ((char)uVar1 == '\0') {
    _Ext = &DAT_004091f0;
    param_2 = &DAT_004091e8;
  }
  else {
    _Ext = &DAT_004139e4;
  }
  _makepath(&DAT_00413af0,local_30c,local_300,param_2,_Ext);
  local_304 = param_1 & 0x100;
  if (local_304 == 0) {
    DAT_004139e8 = 0;
  }
  else {
    DVar2 = FUN_004052a0(&DAT_004139e8);
    if (DVar2 != 0) {
      return (undefined *)0x0;
    }
    SetFileAttributesA(&DAT_004139e8,6);
    _makepath(&DAT_00413af0,(char *)0x0,&DAT_004139e8,local_100,local_200);
  }
  if ((param_1 & 0x40) == 0) {
    hFile = FUN_004040b0();
    DVar2 = GetFileSize(hFile,(LPDWORD)0x0);
    iVar3 = FUN_004049f0();
    lDistanceToMove = iVar3 + 0x10000;
    nNumberOfBytesToRead = DVar2 - lDistanceToMove;
    lpBuffer = operator_new(nNumberOfBytesToRead);
    DVar2 = SetFilePointer(hFile,lDistanceToMove,(PLONG)0x0,0);
    if (DVar2 != lDistanceToMove) {
      operator_delete(lpBuffer);
      CloseHandle(hFile);
      FUN_004046c0(s_SetFilePointer_for_input__004091cc);
      return (undefined *)0x0;
    }
    local_308 = 0;
    BVar4 = ReadFile(hFile,lpBuffer,nNumberOfBytesToRead,&local_308,(LPOVERLAPPED)0x0);
    if (BVar4 == 0) {
      operator_delete(lpBuffer);
      CloseHandle(hFile);
      FUN_004046c0(s_ReadFile_for_input__004091b8);
      return (undefined *)0x0;
    }
    DVar2 = 0x80;
    if (local_304 != 0) {
      DVar2 = 6;
    }
    hFile_00 = CreateFileA(&DAT_00413af0,0x40000000,1,(LPSECURITY_ATTRIBUTES)0x0,2,DVar2,(HANDLE)0x0
                          );
    if (hFile_00 == (HANDLE)0xffffffff) {
      operator_delete(lpBuffer);
      CloseHandle(hFile);
      FUN_004046c0(s_Cannot_open_output_file__0040919c);
      return (undefined *)0x0;
    }
    BVar4 = WriteFile(hFile_00,lpBuffer,nNumberOfBytesToRead,&local_308,(LPOVERLAPPED)0x0);
    if (BVar4 == 0) {
      operator_delete(lpBuffer);
      CloseHandle(hFile);
      CloseHandle(hFile_00);
      FUN_004046c0(s_WriteFile_for_output_file__00409180);
      return (undefined *)0x0;
    }
    FUN_004046c0(s_Copy_succeeded__00409170);
    operator_delete(lpBuffer);
    CloseHandle(hFile);
    CloseHandle(hFile_00);
  }
  return &DAT_00413af0;
}


// ==== 00403d90 FUN_00403d90 ====

undefined4 __cdecl FUN_00403d90(LPCSTR param_1)

{
  HANDLE hFile;
  BOOL BVar1;
  DWORD local_404;
  int local_400 [13];
  int aiStack_3cc [243];
  
  hFile = CreateFileA(param_1,0x80000000,1,(LPSECURITY_ATTRIBUTES)0x0,3,0x10000000,(HANDLE)0x0);
  if (hFile != (HANDLE)0xffffffff) {
    local_404 = 0;
    BVar1 = ReadFile(hFile,local_400,0x400,&local_404,(LPOVERLAPPED)0x0);
    if ((BVar1 == 0) || (local_404 != 0x400)) {
      CloseHandle(hFile);
    }
    else {
      CloseHandle(hFile);
      if (((short)local_400[0] == 0x5a4d) && (*(int *)((int)local_400 + aiStack_3cc[2]) == 0x4550))
      {
        return *(undefined4 *)((int)aiStack_3cc + aiStack_3cc[2]);
      }
    }
  }
  return 0;
}


// ==== 00403e30 FUN_00403e30 ====

undefined4 __cdecl FUN_00403e30(HANDLE param_1,int param_2,LPCSTR param_3,int param_4)

{
  char cVar1;
  uint in_EAX;
  int iVar2;
  
  iVar2 = 0;
  if (0 < param_4) {
    do {
      if ((*(int *)(param_3 + 0x80) == 0) && (in_EAX = 0, *param_3 == '\0')) break;
      if (*param_3 == '\0') {
        in_EAX = FUN_004043b0(param_1,param_2,(uint)param_3,'\0');
        cVar1 = (char)in_EAX;
      }
      else {
        in_EAX = FUN_004040f0(param_3,'\0');
        cVar1 = (char)in_EAX;
      }
      if (cVar1 == '\0') {
        return in_EAX & 0xffffff00;
      }
      iVar2 = iVar2 + 1;
      param_3 = param_3 + 0x94;
    } while (iVar2 < param_4);
  }
  return CONCAT31((int3)(in_EAX >> 8),1);
}


// ==== 00403ea0 FUN_00403ea0 ====

undefined4 FUN_00403ea0(void)

{
  LPCSTR pCVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  
  iVar4 = 0;
  pCVar1 = DAT_00413e24;
  if (0 < (int)DAT_00413e24) {
    iVar3 = 0;
    do {
      pCVar1 = (LPCSTR)(DAT_00413e20 + iVar3);
      if ((*(int *)(pCVar1 + 0x80) == 0) && (*pCVar1 == '\0')) break;
      if (*pCVar1 != '\0') {
        uVar2 = FUN_004040f0(pCVar1,'\x01');
        if ((char)uVar2 == '\0') {
          return uVar2 & 0xffffff00;
        }
      }
      iVar4 = iVar4 + 1;
      iVar3 = iVar3 + 0x94;
      pCVar1 = DAT_00413e24;
    } while (iVar4 < (int)DAT_00413e24);
  }
  return CONCAT31((int3)((uint)pCVar1 >> 8),1);
}


// ==== 00403f00 FUN_00403f00 ====

void FUN_00403f00(void)

{
  BOOL BVar1;
  DWORD DVar2;
  CHAR local_400 [1024];
  
  if ((DAT_00413e28 & 0x40) == 0) {
    DAT_00413e1c = CreateFileA(&DAT_00413af0,0x80000000,0,(LPSECURITY_ATTRIBUTES)0x0,3,6,(HANDLE)0x0
                              );
    BVar1 = LockFile(DAT_00413e1c,0,0,0x1000,0);
    DVar2 = GetLastError();
    wsprintfA(local_400,s_LockFile__s___d___d_004091f8,&DAT_00413af0,BVar1,DVar2);
    FUN_004046c0(local_400);
  }
  return;
}


// ==== 00403f80 FUN_00403f80 ====

void FUN_00403f80(void)

{
  BOOL BVar1;
  DWORD DVar2;
  CHAR local_400 [1024];
  
  if ((DAT_00413e28 & 0x40) == 0) {
    if ((DAT_00413e1c != (HANDLE)0x0) && (DAT_00413e1c != (HANDLE)0xffffffff)) {
      CloseHandle(DAT_00413e1c);
      DAT_00413e1c = (HANDLE)0x0;
    }
    BVar1 = DeleteFileA(&DAT_00413af0);
    DVar2 = GetLastError();
    wsprintfA(local_400,s_DeleteFile__s___d___d_00409228,&DAT_00413af0,BVar1,DVar2);
    FUN_004046c0(local_400);
  }
  if (DAT_004139e8 != '\0') {
    BVar1 = RemoveDirectoryA(&DAT_004139e8);
    DVar2 = GetLastError();
    wsprintfA(local_400,s_RemoveDirectory__s___d___d_0040920c,&DAT_004139e8,BVar1,DVar2);
    FUN_004046c0(local_400);
  }
  DAT_00413af0 = 0;
  DAT_004139e8 = '\0';
  return;
}


// ==== 004040b0 FUN_004040b0 ====

HANDLE FUN_004040b0(void)

{
  HANDLE pvVar1;
  
  pvVar1 = CreateFileA(&DAT_00413bf8,0x80000000,1,(LPSECURITY_ATTRIBUTES)0x0,3,0x80,(HANDLE)0x0);
  if (pvVar1 == (HANDLE)0xffffffff) {
    FUN_004046c0(s_Cannot_open_input_file__00409240);
  }
  return pvVar1;
}


// ==== 004040f0 FUN_004040f0 ====

uint __cdecl FUN_004040f0(LPCSTR param_1,char param_2)

{
  LONG *pLVar1;
  LPCSTR pCVar2;
  HANDLE hFile;
  uint uVar3;
  int *lpBuffer;
  DWORD DVar4;
  BOOL BVar5;
  undefined4 uVar6;
  DWORD local_c;
  _FILETIME local_8;
  
  pCVar2 = param_1;
  hFile = CreateFileA(param_1,0xc0000000,3,(LPSECURITY_ATTRIBUTES)0x0,3,0x80,(HANDLE)0x0);
  if (hFile == (HANDLE)0xffffffff) {
    uVar3 = FUN_004046c0(s_UnPatchFile__cannnot_open_the_fi_00409280);
    return uVar3 & 0xffffff00;
  }
  local_8.dwLowDateTime = 0;
  local_8.dwHighDateTime = 0;
  GetFileTime(hFile,(LPFILETIME)0x0,(LPFILETIME)0x0,&local_8);
  uVar3 = *(uint *)(param_1 + 0x88);
  if (uVar3 == 0) {
    uVar3 = 4;
  }
  lpBuffer = operator_new(uVar3);
  if (lpBuffer == (int *)0x0) {
    uVar3 = FUN_004046c0(s_UnPatchFile__cannnot_allocate_bu_00409258);
    return uVar3 & 0xffffff00;
  }
  pLVar1 = (LONG *)(param_1 + 0x80);
  param_1._0_1_ = '\0';
  DVar4 = SetFilePointer(hFile,*pLVar1,(PLONG)0x0,0);
  if (DVar4 == *(DWORD *)(pCVar2 + 0x80)) {
    local_c = 0;
    BVar5 = ReadFile(hFile,lpBuffer,uVar3,&local_c,(LPOVERLAPPED)0x0);
    if ((BVar5 != 0) && (local_c == uVar3)) {
      uVar6 = FUN_00404800(lpBuffer,(int)pCVar2,param_2);
      param_1._0_1_ = (char)uVar6;
      SetFilePointer(hFile,*(LONG *)(pCVar2 + 0x80),(PLONG)0x0,0);
      if ((char)param_1 != '\0') {
        BVar5 = WriteFile(hFile,lpBuffer,uVar3,&local_c,(LPOVERLAPPED)0x0);
        if (BVar5 != 0) {
          SetFileTime(hFile,(FILETIME *)0x0,(FILETIME *)0x0,&local_8);
        }
      }
    }
  }
  operator_delete(lpBuffer);
  BVar5 = CloseHandle(hFile);
  return CONCAT31((int3)((uint)BVar5 >> 8),(char)param_1);
}


// ==== 00404230 FUN_00404230 ====

undefined1 __cdecl FUN_00404230(LPCSTR param_1,uint param_2,int param_3,undefined4 param_4)

{
  HANDLE hFile;
  int *lpBuffer;
  DWORD DVar1;
  BOOL BVar2;
  DWORD nNumberOfBytesToRead;
  DWORD *pDVar3;
  int local_c;
  _FILETIME local_8;
  
  hFile = CreateFileA(param_1,0xc0000000,3,(LPSECURITY_ATTRIBUTES)0x0,3,0x80,(HANDLE)0x0);
  if (hFile == (HANDLE)0xffffffff) {
    FUN_004046c0(s_UnPatchFile__cannnot_open_the_fi_00409280);
    return 0;
  }
  local_8.dwLowDateTime = 0;
  local_8.dwHighDateTime = 0;
  GetFileTime(hFile,(LPFILETIME)0x0,(LPFILETIME)0x0,&local_8);
  param_1._0_1_ = 1;
  local_c = 0;
  if (0 < param_3) {
    pDVar3 = (DWORD *)(param_2 + 0x80);
    do {
      if ((*pDVar3 == 0) && ((char)pDVar3[-0x20] == '\0')) break;
      nNumberOfBytesToRead = pDVar3[2];
      if (nNumberOfBytesToRead == 0) {
        nNumberOfBytesToRead = 4;
      }
      lpBuffer = operator_new(nNumberOfBytesToRead);
      if (lpBuffer == (int *)0x0) {
        param_1._0_1_ = 0;
        FUN_004046c0(s_UnPatchFile__cannnot_allocate_bu_00409258);
        break;
      }
      DVar1 = SetFilePointer(hFile,*pDVar3,(PLONG)0x0,0);
      if (DVar1 != *pDVar3) {
LAB_0040437e:
        operator_delete(lpBuffer);
        param_1._0_1_ = 0;
        break;
      }
      param_2 = 0;
      BVar2 = ReadFile(hFile,lpBuffer,nNumberOfBytesToRead,&param_2,(LPOVERLAPPED)0x0);
      if ((BVar2 == 0) || (param_2 != nNumberOfBytesToRead)) goto LAB_0040437e;
      FUN_00404800(lpBuffer,(int)(pDVar3 + -0x20),(char)param_4);
      DVar1 = SetFilePointer(hFile,*pDVar3,(PLONG)0x0,0);
      if ((DVar1 != *pDVar3) ||
         (BVar2 = WriteFile(hFile,lpBuffer,nNumberOfBytesToRead,&param_2,(LPOVERLAPPED)0x0),
         BVar2 == 0)) goto LAB_0040437e;
      operator_delete(lpBuffer);
      local_c = local_c + 1;
      pDVar3 = pDVar3 + 0x25;
    } while (local_c < param_3);
  }
  SetFileTime(hFile,(FILETIME *)0x0,(FILETIME *)0x0,&local_8);
  CloseHandle(hFile);
  return param_1._0_1_;
}


// ==== 004043b0 FUN_004043b0 ====

uint __cdecl FUN_004043b0(HANDLE param_1,int param_2,uint param_3,char param_4)

{
  int *lpBuffer;
  BOOL BVar1;
  DWORD DVar2;
  uint extraout_EAX;
  undefined4 uVar3;
  undefined4 extraout_EAX_00;
  int iVar4;
  uint uVar5;
  LPCVOID lpBaseAddress;
  PVOID *ppvVar6;
  char *pcVar7;
  SIZE_T local_60;
  _MEMORY_BASIC_INFORMATION local_5c;
  CHAR local_40 [64];
  
  uVar5 = *(uint *)(param_3 + 0x88);
  if (uVar5 == 0) {
    uVar5 = 4;
  }
  lpBuffer = operator_new(uVar5);
  if (lpBuffer == (int *)0x0) {
    uVar5 = FUN_004046c0(s_SdwPatchProcess__cannnot_allocat_004092fc);
    return uVar5 & 0xffffff00;
  }
  lpBaseAddress = (LPCVOID)(*(int *)(param_3 + 0x80) + param_2);
  local_60 = 0;
  BVar1 = ReadProcessMemory(param_1,lpBaseAddress,lpBuffer,uVar5,&local_60);
  if (BVar1 == 0) {
    DVar2 = GetLastError();
    wsprintfA(local_40,s_ReadProcessMemory_failed___d__004092dc,DVar2);
    FUN_004046c0(local_40);
    operator_delete(lpBuffer);
    return extraout_EAX & 0xffffff00;
  }
  uVar3 = FUN_00404800(lpBuffer,param_3,param_4);
  param_3._0_1_ = (char)uVar3;
  if ((char)param_3 != '\0') {
    local_5c.BaseAddress = (LPVOID)0x0;
    ppvVar6 = &local_5c.AllocationBase;
    for (iVar4 = 6; iVar4 != 0; iVar4 = iVar4 + -1) {
      *ppvVar6 = (PVOID)0x0;
      ppvVar6 = ppvVar6 + 1;
    }
    VirtualQueryEx(param_1,lpBaseAddress,&local_5c,0x1c);
    BVar1 = VirtualProtectEx(param_1,local_5c.BaseAddress,local_5c.RegionSize,0x40,&local_5c.Protect
                            );
    if (BVar1 == 0) {
      pcVar7 = s_VirtualProtectEx_failed__004092c0;
    }
    else {
      BVar1 = WriteProcessMemory(param_1,lpBaseAddress,lpBuffer,uVar5,&local_60);
      if (BVar1 != 0) goto LAB_004044d9;
      pcVar7 = s_WriteProcessMemory_failed__004092a4;
    }
    FUN_004046c0(pcVar7);
    param_3._0_1_ = '\0';
  }
LAB_004044d9:
  operator_delete(lpBuffer);
  return CONCAT31((int3)((uint)extraout_EAX_00 >> 8),(char)param_3);
}


// ==== 00404500 FUN_00404500 ====

undefined4 FUN_00404500(void)

{
  char *pcVar1;
  DWORD DVar2;
  BOOL BVar3;
  uintptr_t uVar4;
  int iVar5;
  _STARTUPINFOA *p_Var6;
  _PROCESS_INFORMATION local_15c;
  _STARTUPINFOA local_14c;
  CHAR local_108 [264];
  
  lstrcpyA(local_108,&DAT_00413bf8);
  lstrcatA(local_108,&DAT_00409334);
  pcVar1 = strchr(local_108,0);
  DVar2 = GetCurrentProcessId();
  wsprintfA(pcVar1,s__sd__x__x_00409328,DAT_00413d08,DVar2);
  p_Var6 = &local_14c;
  for (iVar5 = 0x11; iVar5 != 0; iVar5 = iVar5 + -1) {
    p_Var6->cb = 0;
    p_Var6 = (_STARTUPINFOA *)&p_Var6->lpReserved;
  }
  local_15c.hProcess = (void *)0x0;
  local_15c.hThread = (HANDLE)0x0;
  local_15c.dwProcessId = 0;
  local_15c.dwThreadId = 0;
  local_14c.cb = 0x44;
  BVar3 = CreateProcessA((LPCSTR)0x0,local_108,(LPSECURITY_ATTRIBUTES)0x0,(LPSECURITY_ATTRIBUTES)0x0
                         ,0,0x20,(LPVOID)0x0,(LPCSTR)0x0,&local_14c,&local_15c);
  if (BVar3 == 0) {
    return 0;
  }
  uVar4 = _beginthread((_StartAddress *)&LAB_004045d0,0,local_15c.hProcess);
  return CONCAT31((int3)(uVar4 >> 8),1);
}


// ==== 004045f0 FUN_004045f0 ====

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

bool FUN_004045f0(void)

{
  bool bVar1;
  
  bVar1 = DAT_00413d00 != (HANDLE)0x0;
  if (bVar1) {
    TerminateProcess(DAT_00413d00,0);
    DAT_00413d00 = (HANDLE)0x0;
    DAT_00413d04 = 0;
    DAT_00413d08 = 0;
    _DAT_00413d0c = 0;
  }
  Sleep(200);
  FUN_00403f80();
  return bVar1;
}


// ==== 00404640 FUN_00404640 ====

undefined4 * FUN_00404640(void)

{
  return &DAT_00413d00;
}


// ==== 00404650 FUN_00404650 ====

bool __cdecl FUN_00404650(LPCSTR param_1,LPCSTR param_2)

{
  BOOL BVar1;
  int iVar2;
  _STARTUPINFOA *p_Var3;
  _PROCESS_INFORMATION local_54;
  _STARTUPINFOA local_44;
  
  if (param_1 == (LPCSTR)0x0) {
    param_1 = &DAT_00413bf8;
  }
  p_Var3 = &local_44;
  for (iVar2 = 0x11; iVar2 != 0; iVar2 = iVar2 + -1) {
    p_Var3->cb = 0;
    p_Var3 = (_STARTUPINFOA *)&p_Var3->lpReserved;
  }
  local_54.hProcess = (HANDLE)0x0;
  local_54.hThread = (HANDLE)0x0;
  local_54.dwProcessId = 0;
  local_54.dwThreadId = 0;
  local_44.cb = 0x44;
  BVar1 = CreateProcessA(param_1,(LPSTR)0x0,(LPSECURITY_ATTRIBUTES)0x0,(LPSECURITY_ATTRIBUTES)0x0,0,
                         0x20,(LPVOID)0x0,param_2,&local_44,&local_54);
  return BVar1 != 0;
}


// ==== 004046c0 FUN_004046c0 ====

void __cdecl FUN_004046c0(LPCSTR param_1)

{
  HKEY hKey;
  LSTATUS LVar1;
  HANDLE hFile;
  DWORD nNumberOfBytesToWrite;
  HKEY *lpNumberOfBytesWritten;
  LPOVERLAPPED lpOverlapped;
  HKEY local_c;
  DWORD local_8 [2];
  
  if (DAT_00409338 == -1) {
    DAT_00409338 = 0;
    local_c = (HKEY)0x0;
    LVar1 = RegOpenKeyExA((HKEY)0x80000002,s_Software_Paltiosoft_Wrapping_00409354,0,0x20019,
                          &local_c);
    hKey = local_c;
    if (LVar1 == 0) {
      local_8[1] = 0;
      local_8[0] = 4;
      RegQueryValueExA(local_c,s_logging_0040934c,(LPDWORD)0x0,local_8 + 1,(LPBYTE)&DAT_00409338,
                       local_8);
      if (hKey != (HKEY)0x0) {
        RegCloseKey(hKey);
      }
    }
  }
  if (DAT_00409338 != 0) {
    hFile = CreateFileA(s_launch3_log_00409340,0xc0000000,0,(LPSECURITY_ATTRIBUTES)0x0,4,0x80,
                        (HANDLE)0x0);
    if (hFile != (HANDLE)0xffffffff) {
      SetFilePointer(hFile,0,(PLONG)0x0,2);
      lpNumberOfBytesWritten = &local_c;
      lpOverlapped = (LPOVERLAPPED)0x0;
      local_c = (HKEY)0x0;
      nNumberOfBytesToWrite = lstrlenA(param_1);
      WriteFile(hFile,param_1,nNumberOfBytesToWrite,(LPDWORD)lpNumberOfBytesWritten,lpOverlapped);
      WriteFile(hFile,&DAT_0040933c,2,(LPDWORD)&local_c,(LPOVERLAPPED)0x0);
      CloseHandle(hFile);
    }
  }
  return;
}


// ==== 004047b0 FUN_004047b0 ====

int __cdecl FUN_004047b0(short *param_1)

{
  int iVar1;
  int *piVar2;
  int *piVar3;
  uint uVar4;
  
  if (*param_1 != 0x5a4d) {
    return 0;
  }
  piVar2 = (int *)(*(int *)(param_1 + 0x1e) + (int)param_1);
  if (*piVar2 != 0x4550) {
    return 0;
  }
  piVar3 = (int *)((uint)*(ushort *)(piVar2 + 5) + (int)piVar2);
  iVar1 = piVar2[0x15];
  for (uVar4 = (uint)*(ushort *)((int)piVar2 + 6); uVar4 != 0; uVar4 = uVar4 - 1) {
    piVar3 = piVar3 + 10;
    iVar1 = iVar1 + *piVar3;
  }
  return iVar1;
}


// ==== 00404800 FUN_00404800 ====

uint __cdecl FUN_00404800(int *param_1,int param_2,char param_3)

{
  uint uVar1;
  uint uVar2;
  
  uVar1 = *(uint *)(param_2 + 0x8c);
  uVar2 = *(uint *)(param_2 + 0x84);
  if (param_3 == '\0') {
    uVar1 = *(uint *)(param_2 + 0x84);
    uVar2 = *(uint *)(param_2 + 0x8c);
  }
  if (*param_1 != uVar2) {
    return uVar1 & 0xffffff00;
  }
  *param_1 = uVar1;
  uVar1 = *(uint *)(param_2 + 0x88);
  if (4 < uVar1) {
    uVar1 = FUN_00405230(uVar1 - 4,(int)(param_1 + 1),*(uint *)(param_2 + 0x80));
  }
  return CONCAT31((int3)(uVar1 >> 8),1);
}


// ==== 00404860 FUN_00404860 ====

undefined * __cdecl FUN_00404860(int param_1,LPCSTR param_2)

{
  uchar *puVar1;
  int iVar2;
  uint uVar3;
  uint uVar4;
  uchar *puVar5;
  uchar local_24 [36];
  
  lstrcpyA(&DAT_00413e2c,param_2);
  puVar1 = (uchar *)strstr(&DAT_00413e2c,s__title__00409374);
  if (puVar1 == (uchar *)0x0) {
    return &DAT_00413e2c;
  }
  iVar2 = FUN_00405330((IID *)(param_1 + 0x10),local_24,0x20);
  if (iVar2 < 1) {
    local_24[0] = '\0';
  }
  uVar3 = lstrlenA((LPCSTR)local_24);
  iVar2 = lstrlenA((LPCSTR)(puVar1 + 8));
  memmove(puVar1 + uVar3,puVar1 + 8,iVar2 + 1);
  puVar5 = local_24;
  for (uVar4 = uVar3 >> 2; uVar4 != 0; uVar4 = uVar4 - 1) {
    *(undefined4 *)puVar1 = *(undefined4 *)puVar5;
    puVar5 = puVar5 + 4;
    puVar1 = puVar1 + 4;
  }
  for (uVar3 = uVar3 & 3; uVar3 != 0; uVar3 = uVar3 - 1) {
    *puVar1 = *puVar5;
    puVar5 = puVar5 + 1;
    puVar1 = puVar1 + 1;
  }
  return &DAT_00413e2c;
}


// ==== 00404900 FUN_00404900 ====

uint __cdecl FUN_00404900(HANDLE param_1)

{
  HANDLE hFile;
  short *lpBuffer;
  BOOL BVar1;
  uint _NewSize;
  
  hFile = param_1;
  SetFilePointer(param_1,0,(PLONG)0x0,0);
  lpBuffer = malloc(0x400);
  param_1 = (HANDLE)0x0;
  BVar1 = ReadFile(hFile,lpBuffer,0x400,(LPDWORD)&param_1,(LPOVERLAPPED)0x0);
  if ((BVar1 == 0) || (param_1 != (HANDLE)0x400)) {
    free(lpBuffer);
    return 0;
  }
  _NewSize = FUN_004049c0(lpBuffer);
  if (0x400 < _NewSize) {
    lpBuffer = realloc(lpBuffer,_NewSize);
    ReadFile(hFile,lpBuffer + 0x200,_NewSize - 0x400,(LPDWORD)&param_1,(LPOVERLAPPED)0x0);
    _NewSize = FUN_004049c0(lpBuffer);
  }
  DAT_00414250 = FUN_004047b0(lpBuffer);
  free(lpBuffer);
  return _NewSize;
}


// ==== 004049c0 FUN_004049c0 ====

int __cdecl FUN_004049c0(short *param_1)

{
  if (*param_1 != 0x5a4d) {
    return 0;
  }
  if (*(int *)(*(int *)(param_1 + 0x1e) + (int)param_1) == 0x4550) {
    return ((int *)(*(int *)(param_1 + 0x1e) + (int)param_1))[0x15];
  }
  return 0;
}


// ==== 004049f0 FUN_004049f0 ====

undefined4 FUN_004049f0(void)

{
  return DAT_00414250;
}


// ==== 00404a00 FUN_00404a00 ====

DWORD __cdecl FUN_00404a00(LPCSTR param_1,int *param_2,uint *param_3,char param_4)

{
  DWORD DVar1;
  HANDLE hFile;
  HANDLE hFileMappingObject;
  short *lpBaseAddress;
  uint uVar2;
  DWORD dwDesiredAccess;
  int *piVar3;
  DWORD flProtect;
  DWORD local_4;
  
  local_4 = 0;
  DVar1 = 0x80000000;
  flProtect = 2;
  dwDesiredAccess = 4;
  if (param_4 != '\0') {
    flProtect = 4;
    DVar1 = 0xc0000000;
    dwDesiredAccess = 0xf001f;
  }
  hFile = CreateFileA(param_1,DVar1,1,(LPSECURITY_ATTRIBUTES)0x0,3,0x10000000,(HANDLE)0x0);
  if (hFile == (HANDLE)0xffffffff) {
    DVar1 = GetLastError();
    return DVar1;
  }
  DVar1 = GetFileSize(hFile,(LPDWORD)0x0);
  hFileMappingObject =
       CreateFileMappingA(hFile,(LPSECURITY_ATTRIBUTES)0x0,flProtect,0,0,(LPCSTR)0x0);
  if (hFileMappingObject == (HANDLE)0x0) {
    local_4 = GetLastError();
  }
  else {
    lpBaseAddress = MapViewOfFile(hFileMappingObject,dwDesiredAccess,0,0,0);
    if (lpBaseAddress == (short *)0x0) {
      local_4 = GetLastError();
    }
    else if (*lpBaseAddress == 0x5a4d) {
      piVar3 = (int *)((int)lpBaseAddress + *(int *)(lpBaseAddress + 0x1e));
      if (*piVar3 == 0x4550) {
        *param_2 = piVar3[0x16];
        uVar2 = FUN_00405060(DVar1 - (*(int *)(lpBaseAddress + 0x1e) + 0xf8),
                             (byte *)((int)lpBaseAddress + *(int *)(lpBaseAddress + 0x1e) + 0xf8));
        *param_3 = uVar2;
        if (param_4 != '\0') {
          piVar3[0x16] = uVar2;
        }
      }
      else {
        local_4 = 0xb;
      }
    }
    else {
      local_4 = 0xb;
    }
    if (lpBaseAddress != (short *)0x0) {
      UnmapViewOfFile(lpBaseAddress);
    }
  }
  if (hFileMappingObject != (HANDLE)0x0) {
    CloseHandle(hFileMappingObject);
  }
  CloseHandle(hFile);
  return local_4;
}


// ==== 00404b40 FUN_00404b40 ====

undefined4 __cdecl FUN_00404b40(BYTE *param_1,uint param_2,undefined4 param_3,uint param_4)

{
  ulong *puVar1;
  undefined4 uVar2;
  int iVar3;
  HRESULT HVar4;
  bool bVar5;
  undefined4 local_28;
  uint local_24;
  IID local_20;
  undefined4 local_10 [4];
  
  DAT_0041429c = 0;
  puVar1 = (ulong *)FUN_004058e0(local_10);
  local_20.Data1 = *puVar1;
  local_20._4_4_ = puVar1[1];
  local_20.Data4._0_4_ = puVar1[2];
  local_20.Data4._4_4_ = puVar1[3];
  uVar2 = FUN_00405970();
  local_28 = 0;
  local_24 = 0;
  iVar3 = FUN_00404e50(&local_28,&local_24);
  if (iVar3 != 0) {
    return 10;
  }
  if (((uint)CONCAT21((short)((uint)local_28 >> 0x10),(char)local_28) << 0x10 |
       (local_24 >> 0x10 & 0xff) << 8 | local_24 & 0xff) < param_4) {
    return 10;
  }
  if (DAT_00414294 == 0) {
    if (param_2 == 0xffffffff) {
      param_2 = 0;
      bVar5 = false;
    }
    else {
      HVar4 = CoInitialize((LPVOID)0x0);
      bVar5 = HVar4 == 0;
      if (HVar4 < 0) {
        return 1;
      }
    }
    DAT_004142ac = bVar5;
    HVar4 = CoCreateInstance((IID *)&DAT_00407460,(LPUNKNOWN)0x0,7,(IID *)&DAT_00407540,
                             (LPVOID *)&DAT_00414294);
    if (HVar4 < 0) {
      return 1;
    }
  }
  DAT_00414298 = param_2;
  uVar2 = FUN_00405600(param_2,(int *)&local_20,param_3,uVar2);
  if (((DAT_00414294 != 0) && (param_1 != (BYTE *)0x0)) && (*param_1 != '\0')) {
    FUN_00405420(&local_20,param_1);
  }
  return uVar2;
}


// ==== 00404ca0 FUN_00404ca0 ====

undefined4 FUN_00404ca0(void)

{
  DAT_0041429c = 0;
  FUN_00405820(DAT_00414298);
  DAT_00414298 = 0;
  if (DAT_00414294 != (int *)0x0) {
    (**(code **)(*DAT_00414294 + 8))(DAT_00414294);
    DAT_00414294 = (int *)0x0;
  }
  if (DAT_004142ac != '\0') {
    CoUninitialize();
  }
  return 0;
}


// ==== 00404cf0 FUN_00404cf0 ====

void FUN_00404cf0(void)

{
  if (DAT_004142b0 == 0) {
    DAT_004142b0 = FUN_00405980();
  }
  return;
}


// ==== 00404d10 FUN_00404d10 ====

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

undefined4 __cdecl FUN_00404d10(undefined4 param_1)

{
  byte bVar1;
  undefined3 extraout_var;
  UINT Msg;
  undefined3 extraout_var_00;
  WPARAM wParam;
  LPARAM lParam;
  
  switch(param_1) {
  case 1:
    DAT_0041429c = 1;
    if (DAT_00414298 != (HWND)0x0) {
      bVar1 = FUN_00405530((int)DAT_00414298,&DAT_004142a0,&DAT_004142a4,&DAT_004142a8);
      if (CONCAT31(extraout_var,bVar1) != 0) {
        DAT_004142a0 = 0;
        _DAT_004142a4 = 0;
      }
      wParam = 4;
      lParam = DAT_004142a0;
      Msg = FUN_00404cf0();
      PostMessageA(DAT_00414298,Msg,wParam,lParam);
    }
    return 0;
  case 2:
  case 3:
  case 5:
  case 10:
  case 0xb:
  case 0xc:
  case 100:
    goto switchD_00404d26_caseD_2;
  case 4:
    if ((DAT_00414298 != (HWND)0x0) &&
       (bVar1 = FUN_00405530((int)DAT_00414298,&DAT_004142a0,&DAT_004142a4,&DAT_004142a8),
       CONCAT31(extraout_var_00,bVar1) != 0)) {
      DAT_004142a0 = 0;
      _DAT_004142a4 = 0;
    }
switchD_00404d26_caseD_2:
    return 0;
  default:
    return 3;
  }
}


// ==== 00404e50 FUN_00404e50 ====

void __cdecl FUN_00404e50(undefined4 *param_1,undefined4 *param_2)

{
  FUN_00405a60(param_1,param_2);
  return;
}


// ==== 00404e70 FUN_00404e70 ====

bool __cdecl FUN_00404e70(uint param_1)

{
  bool bVar1;
  int in_stack_00000014;
  
  bVar1 = FUN_00405910(&param_1,in_stack_00000014);
  return bVar1;
}


// ==== 00404e90 FUN_00404e90 ====

void __cdecl
FUN_00404e90(undefined4 param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4)

{
  FUN_004055c0(param_1,param_2,param_3,param_4);
  return;
}


// ==== 00404eb0 FUN_00404eb0 ====

uint __cdecl FUN_00404eb0(undefined4 param_1,undefined4 param_2)

{
  byte bVar1;
  undefined3 extraout_var;
  uint local_c [3];
  
  local_c[2] = 0;
  local_c[0] = 0;
  local_c[1] = 0;
  bVar1 = FUN_00405580(param_1,param_2,local_c + 2,local_c,local_c + 1);
  return ~-(uint)(CONCAT31(extraout_var,bVar1) != 0) & local_c[0];
}


// ==== 00404ef0 FUN_00404ef0 ====

void __cdecl
FUN_00404ef0(undefined4 param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4,
            undefined4 param_5)

{
  FUN_00405840(param_1,param_2,param_3,param_4,param_5);
  return;
}


// ==== 00404f20 FUN_00404f20 ====

undefined * __cdecl FUN_00404f20(IID *param_1)

{
  IID *lpWideCharStr;
  int iVar1;
  CHAR *lpString2;
  undefined1 unaff_DI;
  CHAR aCStackY_b0 [128];
  
  StringFromCLSID(param_1,(LPOLESTR *)&param_1);
  lpWideCharStr = param_1;
  if (param_1 == (IID *)0x0) {
    lpString2 = (LPCSTR)0x0;
  }
  else {
    lpString2 = aCStackY_b0 + 0xa0;
    iVar1 = lstrlenW((LPCWSTR)param_1);
    FUN_00406520(unaff_DI);
    WideCharToMultiByte(0,0,(LPCWSTR)lpWideCharStr,-1,aCStackY_b0 + 0xa0,iVar1 * 2 + 2,(LPCSTR)0x0,
                        (LPBOOL)0x0);
  }
  lstrcpyA(&DAT_004142c0,lpString2);
  CoTaskMemFree(param_1);
  return &DAT_004142c0;
}


// ==== 00404fa0 FUN_00404fa0 ====

uint __cdecl FUN_00404fa0(uint param_1)

{
  return (param_1 & 0x1f) << 0xb | (param_1 & 0xffff) >> 5;
}


// ==== 00404fc0 FUN_00404fc0 ====

undefined4 __cdecl FUN_00404fc0(undefined4 *param_1)

{
  uint uVar1;
  undefined4 local_10;
  undefined4 local_c;
  undefined4 local_8;
  undefined4 local_4;
  
  local_10 = *param_1;
  local_8 = param_1[2];
  local_c = 0x45455c5c;
  local_4 = param_1[3];
  uVar1 = FUN_00405140(0x10,(byte *)&local_10);
  if (*(short *)(param_1 + 1) == (short)uVar1) {
    uVar1 = FUN_00404fa0(uVar1);
    if (*(short *)((int)param_1 + 6) == (short)uVar1) {
      return 1;
    }
  }
  return 0;
}


// ==== 00405030 FUN_00405030 ====

uint __cdecl FUN_00405030(uint param_1)

{
  uint uVar1;
  
  uVar1 = (param_1 ^ 0xcb6359f4) >> 3;
  return (uVar1 | (param_1 ^ 0xcb6359f4) << 0x1d) >> 0x10 | uVar1 << 0x10;
}


// ==== 00405050 FUN_00405050 ====

void __cdecl FUN_00405050(uint *param_1)

{
  FUN_00405030(*param_1);
  return;
}


// ==== 00405060 FUN_00405060 ====

uint __cdecl FUN_00405060(uint param_1,byte *param_2)

{
  byte bVar1;
  uint uVar2;
  uint uVar3;
  
  if (DAT_004148f0 == 0) {
    FUN_004050b0();
  }
  uVar2 = 0xffffffff;
  if (-1 < (int)(param_1 - 1)) {
    uVar3 = param_1;
    do {
      bVar1 = *param_2;
      param_2 = param_2 + 1;
      param_1 = (uint)(byte)((byte)(uVar2 >> 0x18) ^ bVar1);
      uVar2 = uVar2 << 8 ^ (&DAT_004144f0)[param_1];
      uVar3 = uVar3 - 1;
    } while (uVar3 != 0);
  }
  return ~uVar2;
}


// ==== 004050b0 FUN_004050b0 ====

void FUN_004050b0(void)

{
  uint uVar1;
  ushort *puVar2;
  ushort uVar3;
  uint *puVar4;
  int iVar5;
  ushort uVar6;
  int iVar7;
  
  DAT_004148f0 = 1;
  iVar5 = 0;
  puVar4 = &DAT_004144f0;
  do {
    iVar7 = 8;
    uVar1 = iVar5 << 0x18;
    do {
      if ((uVar1 & 0x80000000) == 0) {
        uVar1 = uVar1 << 1;
      }
      else {
        uVar1 = uVar1 * 2 ^ 0x4c11db7;
      }
      iVar7 = iVar7 + -1;
    } while (iVar7 != 0);
    *puVar4 = uVar1;
    puVar4 = puVar4 + 1;
    iVar5 = iVar5 + 1;
  } while (puVar4 < &DAT_004148f0);
  uVar3 = 0;
  puVar2 = &DAT_004142f0;
  do {
    iVar5 = 8;
    uVar6 = uVar3;
    do {
      if ((uVar6 & 1) == 0) {
        uVar6 = uVar6 >> 1;
      }
      else {
        uVar6 = uVar6 >> 1 ^ 0x8408;
      }
      iVar5 = iVar5 + -1;
    } while (iVar5 != 0);
    uVar3 = uVar3 + 1;
    *puVar2 = uVar6;
    puVar2 = puVar2 + 1;
  } while (puVar2 < &DAT_004144f0);
  return;
}


// ==== 00405140 FUN_00405140 ====

uint __cdecl FUN_00405140(uint param_1,byte *param_2)

{
  uint uVar1;
  uint uVar2;
  
  if (DAT_004148f0 == 0) {
    FUN_004050b0();
  }
  uVar1 = 0xffff;
  if (-1 < (int)(param_1 - 1)) {
    uVar2 = param_1;
    do {
      param_1 = (uint)(byte)(*param_2 ^ (byte)uVar1);
      param_2 = param_2 + 1;
      uVar1 = (uint)(ushort)((&DAT_004142f0)[param_1] ^ (ushort)(byte)(uVar1 >> 8));
      uVar2 = uVar2 - 1;
    } while (uVar2 != 0);
  }
  return ~uVar1;
}


// ==== 00405190 FUN_00405190 ====

void __cdecl FUN_00405190(int param_1,int param_2,uint param_3)

{
  uint uVar1;
  int iVar2;
  uint local_834 [522];
  void *local_c;
  undefined1 *puStack_8;
  undefined4 local_4;
  
  local_4 = 0xffffffff;
  puStack_8 = &LAB_0040676b;
  local_c = ExceptionList;
  ExceptionList = &local_c;
  FUN_00405db0(local_834);
  iVar2 = 0;
  local_4 = 0;
  FUN_00405dd0(local_834,param_3);
  if (0 < param_1) {
    do {
      uVar1 = FUN_00405e70(local_834);
      *(byte *)(iVar2 + param_2) = *(byte *)(iVar2 + param_2) ^ (byte)uVar1;
      iVar2 = iVar2 + 1;
    } while (iVar2 < param_1);
  }
  local_4 = 0xffffffff;
  FUN_00405dc0();
  ExceptionList = local_c;
  return;
}


// ==== 00405230 FUN_00405230 ====

void __cdecl FUN_00405230(int param_1,int param_2,uint param_3)

{
  FUN_00405190(param_1,param_2,param_3);
  return;
}


// ==== 00405250 FUN_00405250 ====

uchar * __cdecl FUN_00405250(uchar *param_1,LPCSTR param_2)

{
  uchar uVar1;
  uchar uVar2;
  
  uVar1 = *param_1;
  if (*param_1 != '\0') {
    do {
      uVar2 = uVar1;
      param_1 = _mbsinc(param_1);
      uVar1 = *param_1;
    } while (*param_1 != '\0');
    if ((uVar2 != '\0') && (uVar2 != '\\')) {
      *param_1 = '\\';
      param_1 = param_1 + 1;
    }
  }
  lstrcpyA((LPSTR)param_1,param_2);
  return param_1;
}


// ==== 004052a0 FUN_004052a0 ====

DWORD __cdecl FUN_004052a0(LPSTR param_1)

{
  DWORD DVar1;
  UINT UVar2;
  BOOL BVar3;
  CHAR local_108 [264];
  
  *param_1 = '\0';
  DVar1 = GetTempPathA(0x104,local_108);
  if (DVar1 == 0) {
    DVar1 = GetLastError();
    return DVar1;
  }
  UVar2 = GetTempFileNameA(local_108,&DAT_00409394,0,param_1);
  if (UVar2 == 0) {
    DVar1 = GetLastError();
    return DVar1;
  }
  DeleteFileA(param_1);
  BVar3 = CreateDirectoryA(param_1,(LPSECURITY_ATTRIBUTES)0x0);
  if (BVar3 == 0) {
    DVar1 = GetLastError();
    return DVar1;
  }
  return 0;
}


// ==== 00405330 FUN_00405330 ====

void __cdecl FUN_00405330(IID *param_1,uchar *param_2,int param_3)

{
  LPCWSTR lpWideCharStr;
  undefined1 *puVar1;
  int iVar2;
  uchar *_Source;
  uchar unaff_DI;
  undefined4 local_1c;
  int local_18;
  undefined1 *local_14;
  void *local_10;
  undefined1 *puStack_c;
  undefined4 local_8;
  
  puStack_c = &LAB_00406780;
  local_10 = ExceptionList;
  local_14 = (undefined1 *)((int)register0x00000010 + -0x28);
  ExceptionList = &local_10;
  puVar1 = (undefined1 *)((int)register0x00000010 + -0x28);
  if (DAT_00414294 != (int *)0x0) {
    local_1c = (LPCWSTR)0x0;
    local_18 = -1;
    local_8 = 0;
    ExceptionList = &local_10;
    iVar2 = (**(code **)(*DAT_00414294 + 0xc0))(DAT_00414294,param_1,&local_1c,&local_18);
    lpWideCharStr = local_1c;
    puVar1 = local_14;
    if ((-1 < iVar2) && (local_18 == 0)) {
      _Source = (uchar *)0x0;
      if (local_1c != (LPCWSTR)0x0) {
        iVar2 = lstrlenW(local_1c);
        FUN_00406520(unaff_DI);
        WideCharToMultiByte(0,0,lpWideCharStr,-1,(LPSTR)((int)register0x00000010 + -0x28),
                            iVar2 * 2 + 2,(LPCSTR)0x0,(LPBOOL)0x0);
        _Source = (uchar *)((int)register0x00000010 + -0x28);
      }
      _mbsnbcpy(param_2,_Source,param_3 - 1);
      lstrlenA((LPCSTR)param_2);
      ExceptionList = local_10;
      return;
    }
  }
  local_14 = puVar1;
  local_8 = 0xffffffff;
  FUN_00405cb0(param_1,param_2,param_3);
  ExceptionList = local_10;
  return;
}


// ==== 004053df Catch@004053df ====

undefined4 Catch_004053df(void)

{
  return 0x4053e5;
}


// ==== 00405420 FUN_00405420 ====

bool __cdecl FUN_00405420(IID *param_1,BYTE *param_2)

{
  undefined1 *puVar1;
  bool bVar2;
  int iVar3;
  LPWSTR lpWideCharStr;
  undefined1 auStack_1e [6];
  int local_18;
  undefined1 *local_14;
  void *local_10;
  undefined1 *puStack_c;
  undefined4 local_8;
  
  local_8 = 0xffffffff;
  puStack_c = &LAB_00406798;
  local_10 = ExceptionList;
  local_14 = auStack_1e + -0xe;
  ExceptionList = &local_10;
  puVar1 = auStack_1e + -0xe;
  if (DAT_00414294 != (int *)0x0) {
    ExceptionList = &local_10;
    iVar3 = MultiByteToWideChar(0,0,(LPCSTR)param_2,-1,(LPWSTR)0x0,0);
    lpWideCharStr = (LPWSTR)Ordinal_4(0,iVar3 + -1);
    if (lpWideCharStr != (LPWSTR)0x0) {
      MultiByteToWideChar(0,0,(LPCSTR)param_2,-1,lpWideCharStr,iVar3 + -1);
    }
    local_18 = -1;
    local_8 = 1;
    iVar3 = (**(code **)(*DAT_00414294 + 0xbc))(DAT_00414294,param_1,lpWideCharStr,&local_18);
    if ((-1 < iVar3) && (local_18 == 0)) {
      Ordinal_6(lpWideCharStr);
      ExceptionList = local_10;
      return true;
    }
    local_8 = 0xffffffff;
    Ordinal_6(lpWideCharStr);
    puVar1 = local_14;
  }
  local_14 = puVar1;
  bVar2 = FUN_00405b20(param_1,param_2,'\x01');
  ExceptionList = local_10;
  return bVar2;
}


// ==== 004054d8 Catch@004054d8 ====

undefined * Catch_004054d8(void)

{
  return &DAT_004054de;
}


// ==== 00405520 FUN_00405520 ====

void __fastcall FUN_00405520(undefined4 *param_1)

{
  Ordinal_6(*param_1);
  return;
}


// ==== 00405530 FUN_00405530 ====

byte __cdecl FUN_00405530(int param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4)

{
  int iVar1;
  
  if (DAT_00414294 == (int *)0x0) {
    return 1;
  }
  if (param_1 == 0) {
    return 2;
  }
  iVar1 = (**(code **)(*DAT_00414294 + 0x28))(DAT_00414294,param_1,param_2,param_3,param_4);
  return (-1 < iVar1) - 1U & 2;
}


// ==== 00405580 FUN_00405580 ====

byte __cdecl
FUN_00405580(undefined4 param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4,
            undefined4 param_5)

{
  int iVar1;
  
  if (DAT_00414294 == (int *)0x0) {
    return 1;
  }
  iVar1 = (**(code **)(*DAT_00414294 + 0x5c))(DAT_00414294,param_1,param_2,param_3,param_4,param_5);
  return (-1 < iVar1) - 1U & 2;
}


// ==== 004055c0 FUN_004055c0 ====

byte __cdecl
FUN_004055c0(undefined4 param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4)

{
  int iVar1;
  
  if (DAT_00414294 == (int *)0x0) {
    return 1;
  }
  iVar1 = (**(code **)(*DAT_00414294 + 0x4c))(DAT_00414294,param_1,param_2,param_3,param_4);
  return (-1 < iVar1) - 1U & 2;
}


// ==== 00405600 FUN_00405600 ====

undefined4 __cdecl FUN_00405600(uint param_1,int *param_2,undefined4 param_3,undefined4 param_4)

{
  undefined4 uVar1;
  int iVar2;
  LPCSTR lpString2;
  char unaff_DI;
  undefined1 auStack_136 [2];
  uint local_134;
  int local_130;
  uint uStack_12c;
  undefined4 uStack_128;
  DWORD DStack_124;
  CHAR local_104 [260];
  
  if (*param_2 != 0) {
    uVar1 = FUN_00404fc0(param_2);
    if ((char)uVar1 == '\0') {
      FUN_00405880(DAT_00414b5c,0,0x1b64,s_UCR_SDKID_ERROR_004093d0,unaff_DI);
      return 7;
    }
  }
  if (DAT_00414294 == (int *)0x0) {
    FUN_00405880(DAT_00414b5c,0,0x1b64,s_UCR_ERROR_ON_COM_004093bc,unaff_DI);
    return 1;
  }
  local_134 = 0;
  local_130 = 0;
  iVar2 = FUN_00404e50(&local_134,&local_130);
  if (iVar2 != 0) {
    FUN_00405880(DAT_00414b5c,0,0x1b64,s_UCR_UNKNOWN_VERSION_004093a8,unaff_DI);
    return 0x13;
  }
  FUN_00405880(DAT_00414b5c,1,0x1b64,s__d__d_004093a0,(char)local_134);
  if (local_134 < 0x40000) {
    iVar2 = (**(code **)(*DAT_00414294 + 0x44))(DAT_00414294,param_2,param_4,param_3,param_1);
    if (iVar2 < 0) {
      return param_4;
    }
  }
  else {
    lstrcpyA(local_104,s_sdsdk__00409398);
    lpString2 = (LPCSTR)FUN_00406420();
    lstrcatA(local_104,lpString2);
    FUN_004063e0(param_1,local_104,DAT_00414b5c);
    iVar2 = (**(code **)(*DAT_00414294 + 0xa0))(DAT_00414294,param_2,param_4,param_3,param_1,0);
    if (iVar2 < 0) {
      return param_3;
    }
  }
  local_134 = param_2[2];
  local_130 = param_2[3];
  uStack_12c = param_1;
  uStack_128 = 0;
  DStack_124 = GetCurrentProcessId();
  (**(code **)(*DAT_00414294 + 0x80))(DAT_00414294,0x28,auStack_136 + -0x12);
  return 0;
}


// ==== 00405820 FUN_00405820 ====

void __cdecl FUN_00405820(undefined4 param_1)

{
  if (DAT_00414294 != (int *)0x0) {
    (**(code **)(*DAT_00414294 + 0x2c))(DAT_00414294,param_1);
  }
  return;
}


// ==== 00405840 FUN_00405840 ====

int * __cdecl
FUN_00405840(undefined4 param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4,
            undefined4 param_5)

{
  int *piVar1;
  undefined4 local_4;
  
  piVar1 = (int *)0x1;
  local_4 = 1;
  if (DAT_00414294 != (int *)0x0) {
    piVar1 = DAT_00414294;
    (**(code **)(*DAT_00414294 + 0x60))
              (DAT_00414294,param_1,param_2,param_3,param_4,param_5,&local_4);
  }
  return piVar1;
}


// ==== 00405880 FUN_00405880 ====

void __cdecl FUN_00405880(void *param_1,int param_2,undefined4 param_3,char *param_4,char param_5)

{
  char local_200 [512];
  
  if (param_1 != (void *)0x0) {
    vsprintf(local_200,param_4,&param_5);
    FUN_004064a0(param_1,param_2,param_3,local_200);
  }
  return;
}


// ==== 004058e0 FUN_004058e0 ====

void __cdecl FUN_004058e0(undefined4 *param_1)

{
  *param_1 = DAT_004093e0;
  param_1[1] = DAT_004093e4;
  param_1[2] = DAT_004093e8;
  param_1[3] = DAT_004093ec;
  return;
}


// ==== 00405910 FUN_00405910 ====

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

bool __cdecl FUN_00405910(uint *param_1,int param_2)

{
  undefined4 uVar1;
  int iVar2;
  
  uVar1 = FUN_00404fc0(param_1);
  if (((char)uVar1 == '\0') && (*param_1 != 0)) {
    return false;
  }
  DAT_004093e0 = *param_1;
  DAT_004093e4 = param_1[1];
  DAT_004093e8 = param_1[2];
  DAT_004093ec = param_1[3];
  _DAT_004093f0 = param_2;
  iVar2 = FUN_00405050(param_1);
  return iVar2 == param_2;
}


// ==== 00405970 FUN_00405970 ====

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

undefined4 FUN_00405970(void)

{
  return _DAT_004093f0;
}


// ==== 00405980 FUN_00405980 ====

void FUN_00405980(void)

{
  if (DAT_00414b60 == 0) {
    DAT_00414b60 = RegisterWindowMessageA(s_kUChargeMessage_004093f3 + 1);
  }
  return;
}


// ==== 004059a0 FUN_004059a0 ====

void FUN_004059a0(void)

{
  if (DAT_00414b64 == 0) {
    DAT_00414b64 = RegisterWindowMessageA(s_UChargeChargeMessage_00409404);
  }
  return;
}


// ==== 004059c0 FUN_004059c0 ====

undefined * FUN_004059c0(void)

{
  LPCSTR pCVar1;
  int iVar2;
  LPCSTR pCVar3;
  LPBYTE pBVar4;
  DWORD DVar5;
  
  DVar5 = 0x104;
  pBVar4 = (LPBYTE)&DAT_00414c68;
  pCVar3 = &DAT_004139e4;
  pCVar1 = FUN_00405a20();
  FUN_00405ee0((HKEY)0x80000000,pCVar1,pCVar3,pBVar4,DVar5);
  iVar2 = lstrlenA(&DAT_00414c68);
  if (((iVar2 < 1) || (DAT_00414c68 != '\"')) || ((&DAT_00414c67)[iVar2] != '\"')) {
    return &DAT_00414c68;
  }
  (&DAT_00414c67)[iVar2] = 0;
  return &DAT_00414c69;
}


// ==== 00405a20 FUN_00405a20 ====

undefined * FUN_00405a20(void)

{
  LPCSTR pCVar1;
  
  lstrcpyA(&DAT_00414b68,s_CLSID_0040942c);
  pCVar1 = FUN_00404f20((IID *)&DAT_00407570);
  FUN_00405250(&DAT_00414b68,pCVar1);
  FUN_00405250(&DAT_00414b68,s_LocalServer32_0040941c);
  return &DAT_00414b68;
}


// ==== 00405a60 FUN_00405a60 ====

bool __cdecl FUN_00405a60(undefined4 *param_1,undefined4 *param_2)

{
  char *pcVar1;
  DWORD dwLen;
  void *lpData;
  BOOL BVar2;
  uint local_c;
  LPVOID local_8;
  DWORD local_4;
  
  pcVar1 = FUN_004059c0();
  pcVar1 = _strdup(pcVar1);
  dwLen = GetFileVersionInfoSizeA(pcVar1,&local_4);
  if (dwLen == 0) {
    GetLastError();
    free(pcVar1);
    return true;
  }
  lpData = operator_new(dwLen);
  GetFileVersionInfoA(pcVar1,0,dwLen,lpData);
  local_c = 0;
  BVar2 = VerQueryValueA(lpData,&DAT_00409390,&local_8,&local_c);
  if (BVar2 != 0) {
    *param_1 = *(undefined4 *)((int)local_8 + 8);
    *param_2 = *(undefined4 *)((int)local_8 + 0xc);
  }
  operator_delete(lpData);
  free(pcVar1);
  return BVar2 == 0;
}


// ==== 00405b20 FUN_00405b20 ====

bool __cdecl FUN_00405b20(IID *param_1,BYTE *param_2,char param_3)

{
  bool bVar1;
  undefined *puVar2;
  CHAR local_208 [256];
  HKEY__ local_108 [66];
  
  puVar2 = FUN_00404f20(param_1);
  wsprintfA(local_208,"%s\\apps\\%s","Software\\SoftDenchi",puVar2);
  if (param_3 == '\0') {
    bVar1 = FUN_00405c20((HKEY)0x80000002,local_208,local_108);
    if (bVar1) {
      return false;
    }
  }
  bVar1 = FUN_00405ba0((HKEY)0x80000002,local_208,param_2);
  return bVar1;
}


// ==== 00405ba0 FUN_00405ba0 ====

bool __cdecl FUN_00405ba0(HKEY param_1,LPCSTR param_2,BYTE *param_3)

{
  BYTE *lpData;
  HKEY hKey;
  LSTATUS LVar1;
  int iVar2;
  bool bVar3;
  HKEY local_4;
  
  bVar3 = false;
  local_4 = (HKEY)0x0;
  LVar1 = RegCreateKeyExA(param_1,param_2,0,(LPSTR)0x0,0,0xf003f,(LPSECURITY_ATTRIBUTES)0x0,&local_4
                          ,(LPDWORD)&param_2);
  lpData = param_3;
  if (LVar1 == 0) {
    iVar2 = lstrlenA((LPCSTR)param_3);
    hKey = local_4;
    LVar1 = RegSetValueExA(local_4,"app",0,1,lpData,iVar2 + 1);
    bVar3 = LVar1 == 0;
    if (hKey != (HKEY)0x0) {
      RegCloseKey(hKey);
    }
  }
  return bVar3;
}


// ==== 00405c20 FUN_00405c20 ====

bool __cdecl FUN_00405c20(HKEY param_1,LPCSTR param_2,HKEY param_3)

{
  HKEY lpData;
  HKEY hKey;
  LSTATUS LVar1;
  int iVar2;
  bool bVar3;
  
  lpData = param_3;
  for (iVar2 = 0x41; iVar2 != 0; iVar2 = iVar2 + -1) {
    param_3->unused = 0;
    param_3 = param_3 + 1;
  }
  bVar3 = false;
  param_3 = (HKEY)0x0;
  param_1 = (HKEY)RegOpenKeyExA(param_1,param_2,0,0x20019,&param_3);
  hKey = param_3;
  if (param_1 == (HKEY)0x0) {
    param_2 = (LPCSTR)0x104;
    LVar1 = RegQueryValueExA(param_3,"app",(LPDWORD)0x0,(LPDWORD)&param_1,(LPBYTE)lpData,
                             (LPDWORD)&param_2);
    bVar3 = LVar1 == 0;
    if (bVar3) {
      param_2[(int)lpData] = '\0';
    }
    if (hKey != (HKEY)0x0) {
      RegCloseKey(hKey);
    }
  }
  return bVar3;
}


// ==== 00405cb0 FUN_00405cb0 ====

int __cdecl FUN_00405cb0(IID *param_1,uchar *param_2,int param_3)

{
  bool bVar1;
  undefined *puVar2;
  int iVar3;
  CHAR local_210 [264];
  HKEY__ local_108 [66];
  
  if (param_2 == (uchar *)0x0) {
    return -1;
  }
  puVar2 = FUN_00404f20(param_1);
  wsprintfA(local_210,"%s\\apps\\%s","Software\\SoftDenchi",puVar2);
  bVar1 = FUN_00405c20((HKEY)0x80000002,local_210,local_108);
  if (!bVar1) {
    puVar2 = FUN_00404f20(param_1);
    wsprintfA(local_210,"%s\\apps\\%s",s_Software_UCharge_00409434,puVar2);
    bVar1 = FUN_00405c20((HKEY)0x80000002,local_210,local_108);
    if (!bVar1) {
      *param_2 = '\0';
      return 0;
    }
    FUN_00405b20(param_1,(BYTE *)local_108,'\x01');
  }
  _mbsnbcpy(param_2,(uchar *)local_108,param_3 - 1);
  param_2[param_3 + -1] = '\0';
  iVar3 = lstrlenA((LPCSTR)param_2);
  return iVar3;
}


// ==== 00405db0 FUN_00405db0 ====

void * __fastcall FUN_00405db0(void *param_1)

{
  FUN_00405dd0(param_1,0);
  return param_1;
}


// ==== 00405dc0 FUN_00405dc0 ====

void FUN_00405dc0(void)

{
  return;
}


// ==== 00405dd0 FUN_00405dd0 ====

void __thiscall FUN_00405dd0(void *this,uint param_1)

{
  uint *puVar1;
  uint uVar2;
  int iVar3;
  int iVar4;
  
  uVar2 = 0;
  iVar4 = 0x11;
  puVar1 = this;
  do {
    puVar1 = puVar1 + 1;
    iVar3 = 0x20;
    do {
      param_1 = param_1 * 0x5d588b65 + 1;
      uVar2 = uVar2 >> 1 | param_1 & 0x80000000;
      iVar3 = iVar3 + -1;
    } while (iVar3 != 0);
    *puVar1 = uVar2;
    iVar4 = iVar4 + -1;
  } while (iVar4 != 0);
  iVar4 = 0x1f8;
  *(uint *)((int)this + 0x44) =
       *(int *)((int)this + 0x44) << 0x17 ^ *(uint *)((int)this + 4) >> 9 ^
       *(uint *)((int)this + 0x40);
  puVar1 = (uint *)((int)this + 8);
  do {
    iVar4 = iVar4 + -1;
    puVar1[0x10] = puVar1[-1] << 0x17 ^ *puVar1 >> 9 ^ puVar1[0xf];
    puVar1 = puVar1 + 1;
  } while (iVar4 != 0);
  FUN_00405ea0(this);
  FUN_00405ea0(this);
  FUN_00405ea0(this);
  *(undefined4 *)this = 0x208;
  return;
}


// ==== 00405e70 FUN_00405e70 ====

uint __fastcall FUN_00405e70(uint *param_1)

{
  uint uVar1;
  
  uVar1 = *param_1;
  *param_1 = uVar1 + 1;
  if ((int)(uVar1 + 1) < 0x209) {
    return param_1[*param_1 + 1];
  }
  FUN_00405ea0(param_1);
  *param_1 = 0;
  return param_1[*param_1 + 1];
}


// ==== 00405ea0 FUN_00405ea0 ====

void __fastcall FUN_00405ea0(uint *param_1)

{
  uint *puVar1;
  uint *puVar2;
  int iVar3;
  
  iVar3 = 0x20;
  puVar2 = param_1;
  do {
    puVar1 = puVar2 + 1;
    *puVar1 = *puVar1 ^ puVar2[0x1ea];
    iVar3 = iVar3 + -1;
    puVar2 = puVar1;
  } while (iVar3 != 0);
  puVar2 = param_1 + 0x21;
  iVar3 = 0x1e9;
  do {
    *puVar2 = *puVar2 ^ puVar2[-0x20];
    puVar2 = puVar2 + 1;
    iVar3 = iVar3 + -1;
  } while (iVar3 != 0);
  return;
}


// ==== 00405ee0 FUN_00405ee0 ====

DWORD __cdecl FUN_00405ee0(HKEY param_1,LPCSTR param_2,LPCSTR param_3,LPBYTE param_4,DWORD param_5)

{
  LSTATUS LVar1;
  DWORD local_8;
  HKEY local_4;
  
  local_8 = 0;
  local_4 = (HKEY)0x0;
  LVar1 = RegOpenKeyExA(param_1,param_2,0,0x20019,&local_4);
  if (LVar1 == 0) {
    local_8 = param_5;
    LVar1 = RegQueryValueExA(local_4,param_3,(LPDWORD)0x0,(LPDWORD)&param_2,param_4,&local_8);
    if (LVar1 != 0) {
      local_8 = 0;
    }
    RegCloseKey(local_4);
  }
  return local_8;
}


// ==== 00405f60 FUN_00405f60 ====

DWORD __cdecl FUN_00405f60(HKEY param_1,LPCSTR param_2,LPCSTR param_3,LPBYTE param_4)

{
  LSTATUS LVar1;
  LPCSTR pCVar2;
  HKEY local_4;
  
  pCVar2 = (LPCSTR)0x0;
  local_4 = (HKEY)0x0;
  LVar1 = RegOpenKeyExA(param_1,param_2,0,0x20019,&local_4);
  if (LVar1 == 0) {
    param_2 = (LPCSTR)0x4;
    LVar1 = RegQueryValueExA(local_4,param_3,(LPDWORD)0x0,(LPDWORD)&param_1,param_4,
                             (LPDWORD)&param_2);
    if ((LVar1 == 0) && (param_1 == (HKEY)0x4)) {
      pCVar2 = param_2;
    }
    RegCloseKey(local_4);
  }
  return (DWORD)pCVar2;
}


// ==== 00405fd0 FUN_00405fd0 ====

undefined4 __cdecl FUN_00405fd0(void *param_1)

{
  DWORD DVar1;
  undefined4 uVar2;
  LPCSTR lpName;
  uint uVar3;
  HANDLE hFile;
  BOOL BVar4;
  int iVar5;
  char unaff_DI;
  DWORD local_8 [2];
  
  iVar5 = 0;
  if ((DAT_0040944c < 0) &&
     (DVar1 = FUN_00405f60((HKEY)0x80000002,s_Software_Paltiosoft_ucharge_004094c0,
                           s_RETRY_MAX_004094dc,(LPBYTE)&DAT_0040944c), DVar1 != 4)) {
    DAT_0040944c = 10;
  }
  if (((int)DAT_00409450 < 0) &&
     (DVar1 = FUN_00405f60((HKEY)0x80000002,s_Software_Paltiosoft_ucharge_004094c0,
                           s_RETRY_DELAY_004094b4,(LPBYTE)&DAT_00409450), DVar1 != 4)) {
    DAT_00409450 = 200;
  }
  if ((DAT_00445b08 != (HANDLE)0x0) && (DAT_00445b08 != (HANDLE)0xffffffff)) {
    uVar2 = FUN_00405880(param_1,1,0x1b64,s__StartMonitor_1__004094a0,unaff_DI);
    return CONCAT31((int3)((uint)uVar2 >> 8),1);
  }
  lpName = FUN_00406450(&DAT_00445a04,0);
  DAT_00445b08 = CreateNamedPipeA(lpName,0x40001,0,0xff,0x80,0x80,500,(LPSECURITY_ATTRIBUTES)0x0);
  if (DAT_00445b08 == (HANDLE)0xffffffff) {
    GetLastError();
    FUN_00405880(param_1,0,0x1b64,s__StartMonitor_2__s___d_00409488,(char)lpName);
    uVar3 = FUN_004061e0();
    return uVar3 & 0xffffff00;
  }
  GetLastError();
  FUN_00405880(param_1,1,0x1b64,s__StartMonitor_3__s___d_00409470,(char)lpName);
  local_8[1] = 0;
  DAT_00445b0c = CreateThread((LPSECURITY_ATTRIBUTES)0x0,0,(LPTHREAD_START_ROUTINE)&LAB_004062c0,
                              (LPVOID)0x0,0,local_8 + 1);
  if ((DAT_00445b0c != (HANDLE)0x0) && (0 < DAT_0040944c)) {
    while (hFile = CreateFileA(s_____pipe_softdenchi_login_00409454,0x40000000,0,
                               (LPSECURITY_ATTRIBUTES)0x0,3,0,(HANDLE)0x0),
          hFile == (HANDLE)0xffffffff) {
      Sleep(DAT_00409450);
      iVar5 = iVar5 + 1;
      if (DAT_0040944c <= iVar5) {
        uVar3 = FUN_004061e0();
        return uVar3 & 0xffffff00;
      }
    }
    local_8[0] = 0;
    BVar4 = WriteFile(hFile,&DAT_00445a04,0x100,local_8,(LPOVERLAPPED)0x0);
    if ((BVar4 != 0) && (local_8[0] == 0x100)) {
      BVar4 = CloseHandle(hFile);
      return CONCAT31((int3)((uint)BVar4 >> 8),1);
    }
    CloseHandle(hFile);
  }
  uVar3 = FUN_004061e0();
  return uVar3 & 0xffffff00;
}


// ==== 004061e0 FUN_004061e0 ====

void FUN_004061e0(void)

{
  LPCSTR lpFileName;
  DWORD dwDesiredAccess;
  DWORD dwShareMode;
  LPSECURITY_ATTRIBUTES lpSecurityAttributes;
  DWORD dwCreationDisposition;
  DWORD dwFlagsAndAttributes;
  HANDLE pvVar1;
  DWORD local_14 [5];
  
  if (DAT_00445b0c != (HANDLE)0x0) {
    pvVar1 = (HANDLE)0x0;
    dwFlagsAndAttributes = 0;
    dwCreationDisposition = 3;
    lpSecurityAttributes = (LPSECURITY_ATTRIBUTES)0x0;
    dwShareMode = 0;
    dwDesiredAccess = 0x40000000;
    local_14[1] = 0;
    local_14[2] = 0;
    local_14[3] = 8000;
    local_14[4] = 0;
    lpFileName = FUN_00406450(&DAT_00445a04,0);
    pvVar1 = CreateFileA(lpFileName,dwDesiredAccess,dwShareMode,lpSecurityAttributes,
                         dwCreationDisposition,dwFlagsAndAttributes,pvVar1);
    if (pvVar1 != (HANDLE)0xffffffff) {
      local_14[0] = 0;
      WriteFile(pvVar1,local_14 + 1,0x10,local_14,(LPOVERLAPPED)0x0);
      CloseHandle(pvVar1);
    }
    WaitForSingleObject(DAT_00445b0c,1000);
  }
  if ((DAT_00445b08 != (HANDLE)0x0) && (DAT_00445b08 != (HANDLE)0xffffffff)) {
    CloseHandle(DAT_00445b08);
    DAT_00445b08 = (HANDLE)0x0;
  }
  if ((DAT_00445b0c != (HANDLE)0x0) && (DAT_00445b0c != (HANDLE)0xffffffff)) {
    TerminateThread(DAT_00445b0c,0xffffffff);
    DAT_00445b0c = (HANDLE)0x0;
  }
  FUN_004063b0();
  DAT_00445b14 = 0;
  return;
}


// ==== 004063b0 FUN_004063b0 ====

void FUN_004063b0(void)

{
  UINT Msg;
  
  if (DAT_00409448 != 0) {
    Msg = FUN_00405980();
    PostMessageA((HWND)0xffff,Msg,4,0);
  }
  DAT_00409448 = 0;
  return;
}


// ==== 004063e0 FUN_004063e0 ====

uint __cdecl FUN_004063e0(uint param_1,LPCSTR param_2,void *param_3)

{
  uint uVar1;
  
  uVar1 = DAT_00445b14;
  if (DAT_00445b14 == 0) {
    DAT_00445b14 = param_1;
    lstrcpynA(&DAT_00445a04,param_2,0x100);
    uVar1 = FUN_00405fd0(param_3);
  }
  return uVar1 & 0xffffff00;
}


// ==== 00406420 FUN_00406420 ====

uint FUN_00406420(void)

{
  BOOL BVar1;
  DWORD local_4;
  
  local_4 = 0x100;
  BVar1 = GetUserNameA(&DAT_00445c58,&local_4);
  return -(uint)(BVar1 != 0) & 0x445c58;
}


// ==== 00406450 FUN_00406450 ====

undefined * __cdecl FUN_00406450(undefined4 param_1,int param_2)

{
  if (param_2 == 0) {
    wsprintfA(&DAT_00445b18,s_____pipe_softdenchi__s_004094e8,param_1);
    return &DAT_00445b18;
  }
  wsprintfA(&DAT_00445b18,s_____pipe_softdenchi_charge__s_00409500,param_1);
  return &DAT_00445b18;
}


// ==== 004064a0 FUN_004064a0 ====

void __thiscall FUN_004064a0(void *this,int param_1,undefined4 param_2,undefined4 param_3)

{
  if (param_1 == 3) {
    param_1 = *(int *)((int)this + 0x114);
  }
  if (param_1 <= *(int *)((int)this + 4)) {
    (**(code **)(*(int *)this + 4))(param_1,param_2,param_3);
  }
  return;
}


// ==== 004064d0 FUN_004064d0 ====

void __cdecl FUN_004064d0(_onexit_t param_1)

{
  if (DAT_00445e44 == -1) {
    _onexit(param_1);
    return;
  }
  __dllonexit(param_1,&DAT_00445e44,&DAT_00445e40);
  return;
}


// ==== 004064fc FUN_004064fc ====

int __cdecl FUN_004064fc(_onexit_t param_1)

{
  int iVar1;
  
  iVar1 = FUN_004064d0(param_1);
  return (iVar1 != 0) - 1;
}


// ==== 00406520 FUN_00406520 ====

/* WARNING: Unable to track spacebase fully for stack */

void __cdecl FUN_00406520(undefined1 param_1)

{
  uint in_EAX;
  undefined1 *puVar1;
  undefined4 unaff_retaddr;
  
  puVar1 = &param_1;
  for (; 0xfff < in_EAX; in_EAX = in_EAX - 0x1000) {
    puVar1 = puVar1 + -0x1000;
  }
  *(undefined4 *)(puVar1 + (-4 - in_EAX)) = unaff_retaddr;
  return;
}


// ==== 00406550 entry ====

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void entry(void)

{
  undefined4 *puVar1;
  HMODULE pHVar2;
  byte *pbVar3;
  undefined4 uVar4;
  undefined1 auStack_7a [6];
  char **local_74;
  _startupinfo local_70;
  int local_6c;
  char **local_68;
  int local_64;
  _STARTUPINFOA local_60;
  undefined1 *local_1c;
  void *pvStack_14;
  undefined *puStack_10;
  undefined *puStack_c;
  undefined4 local_8;
  
  puStack_c = &DAT_00407630;
  puStack_10 = &DAT_004066e2;
  pvStack_14 = ExceptionList;
  local_1c = auStack_7a + -0xe;
  local_8 = 0;
  ExceptionList = &pvStack_14;
  __set_app_type(2);
  _DAT_00445e40 = 0xffffffff;
  DAT_00445e44 = 0xffffffff;
  puVar1 = (undefined4 *)__p__fmode();
  *puVar1 = DAT_00445e30;
  puVar1 = (undefined4 *)__p__commode();
  *puVar1 = DAT_00445e2c;
  _DAT_00445e3c = *(undefined4 *)_adjust_fdiv_exref;
  FUN_004066e1();
  if (DAT_00409530 == 0) {
    __setusermatherr(&LAB_004066de);
  }
  FUN_004066cc();
  initterm(&DAT_00409010,&DAT_00409014);
  local_70.newmode = DAT_00445e28;
  __getmainargs(&local_64,&local_74,&local_68,DAT_00445e24,&local_70);
  initterm(&DAT_00409000,&DAT_0040900c);
  pbVar3 = *(byte **)_acmdln_exref;
  if (*pbVar3 != 0x22) {
    do {
      if (*pbVar3 < 0x21) goto LAB_00406643;
      pbVar3 = pbVar3 + 1;
    } while( true );
  }
  do {
    pbVar3 = pbVar3 + 1;
    if (*pbVar3 == 0) break;
  } while (*pbVar3 != 0x22);
  if (*pbVar3 != 0x22) goto LAB_00406643;
  do {
    pbVar3 = pbVar3 + 1;
LAB_00406643:
  } while ((*pbVar3 != 0) && (*pbVar3 < 0x21));
  local_60.dwFlags = 0;
  GetStartupInfoA(&local_60);
  uVar4 = 0;
  pHVar2 = GetModuleHandleA((LPCSTR)0x0);
  local_6c = FUN_00401d20(pHVar2,uVar4,(LPCSTR)pbVar3);
                    /* WARNING: Subroutine does not return */
  exit(local_6c);
}


// ==== 004066cc FUN_004066cc ====

void FUN_004066cc(void)

{
  _controlfp(0x10000,0x30000);
  return;
}


// ==== 004066e1 FUN_004066e1 ====

void FUN_004066e1(void)

{
  return;
}


// ==== 00406710 Unwind@00406710 ====

void Unwind_00406710(void)

{
  FUN_00405dc0();
  return;
}


// ==== 0040671b Unwind@0040671b ====

void Unwind_0040671b(void)

{
  FUN_00405dc0();
  return;
}


// ==== 00406726 Unwind@00406726 ====

void Unwind_00406726(void)

{
  FUN_00405dc0();
  return;
}


// ==== 00406731 Unwind@00406731 ====

void Unwind_00406731(void)

{
  FUN_00405dc0();
  return;
}


// ==== 0040673c Unwind@0040673c ====

void Unwind_0040673c(void)

{
  FUN_00405dc0();
  return;
}


// ==== 00406747 Unwind@00406747 ====

void Unwind_00406747(void)

{
  FUN_00405dc0();
  return;
}


// ==== 00406760 Unwind@00406760 ====

void Unwind_00406760(void)

{
  FUN_00405dc0();
  return;
}


// ==== 00406790 Unwind@00406790 ====

void Unwind_00406790(void)

{
  int unaff_EBP;
  
  FUN_00405520((undefined4 *)(unaff_EBP + -0x1c));
  return;
}


