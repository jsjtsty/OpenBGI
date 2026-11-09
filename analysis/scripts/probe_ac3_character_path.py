"""Evaluate Amairo Chocolate 3's native character-bob spline without a window.

uv run --no-project --with unicorn python analysis/scripts/probe_ac3_character_path.py [scene.log]
An optional OBGI_TRACE_SCENE log is checked against the native first movement.
Executable identity and PE/SSE setup are shared with probe_ac3_crossfade.py.
"""
import math
import re
import struct
import sys

from probe_ac3_crossfade import load_native, HEAP, STACK, RETURN
from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_XMM1,
                              UC_X86_REG_XMM0, UC_X86_REG_FPCW)

BUILD, EVAL = 0x494650, 0x4945B0


def main():
    cpu = load_native("/Volumes/HDD-EXT/Games/巧可甜恋3/amachoco3.exe")
    # The CRT floor dispatcher needs an uninitialized Windows runtime. For its
    # non-negative input, use x87 floor directly; all spline arithmetic stays native.
    cpu.mem_write(0x503810, bytes.fromhex("dd442404d9fcc3"))  # fld [esp+4]; frndint; ret
    cpu.reg_write(UC_X86_REG_FPCW, 0x77F)  # round toward minus infinity
    vector, data, stack = HEAP + 0x3000, HEAP + 0x4000, STACK + 0x80000
    cpu.mem_write(data, struct.pack("<3i", 0, 2 << 16, 5 << 16))
    cpu.mem_write(vector, struct.pack("<3I", data, data + 12, data + 12))
    cpu.mem_write(stack, struct.pack("<2I", RETURN, vector))
    cpu.reg_write(UC_X86_REG_ESP, stack)
    cpu.reg_write(UC_X86_REG_ECX, HEAP)
    cpu.emu_start(BUILD, RETURN, count=200000)
    expected = []
    for ms in range(0, 200, 16):
        time24 = ms * 0x1000000 // 200
        angle = time24 * 90 // 256
        factor = int(math.sin(angle * 2.663161090079238e-7) * 65536)
        cpu.mem_write(stack, struct.pack("<I", RETURN))
        cpu.reg_write(UC_X86_REG_ESP, stack)
        cpu.reg_write(UC_X86_REG_ECX, HEAP)
        cpu.reg_write(UC_X86_REG_XMM1, struct.unpack("<Q", struct.pack("<d", factor / 65536))[0])
        cpu.emu_start(EVAL, RETURN, count=200000)
        value = int(struct.unpack("<d", struct.pack("<Q", cpu.reg_read(UC_X86_REG_XMM0) & ((1 << 64) - 1)))[0])
        expected.append(value)
        print(f"ms={ms:3} native_fixed_y={value:6} pixels={value / 65536:.8f}")
    if len(sys.argv) > 1:
        with open(sys.argv[1]) as log:
            actual = [int(value) for value in re.findall(r"PROJECT id=80000004 fixed_x=\d+ fixed_y=(-?\d+)", log.read())]
        if actual[:len(expected)] != expected:
            raise AssertionError(f"scene differs: {actual[:len(expected)]} != {expected}")
        print(f"PASS: {len(expected)} successive scene positions match native 16.16 results exactly")


if __name__ == "__main__":
    main()
