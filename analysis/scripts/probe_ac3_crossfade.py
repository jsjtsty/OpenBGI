"""Probe Amairo Chocolate 3's native CrossfadeTo without a window or images.

Run with: uv run --no-project --with unicorn python analysis/scripts/probe_ac3_crossfade.py
The executable is read only. Addresses are pinned to its SHA-256 below.
"""
import hashlib
import pathlib
import random
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import (UC_CPU_X86_HASWELL, UC_X86_REG_CR0, UC_X86_REG_CR4, UC_X86_REG_MXCSR,
                              UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP)

EXE_SHA256 = "05ec253500eb7300490feb7995e247abe2f9b7b55134fe129754f4653df324f8"
ENTRY = 0x456340
COOKIE_CHECK = 0x424B17
HEAP, STACK, RETURN = 0x30000000, 0x20000000, 0x40000000


def load_native(path):
    raw = pathlib.Path(path).read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXE_SHA256:
        raise ValueError("Executable differs from the analyzed amachoco3.exe; recheck addresses first")
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    count = struct.unpack_from("<H", raw, pe + 6)[0]
    opt_size = struct.unpack_from("<H", raw, pe + 20)[0]
    base = struct.unpack_from("<I", raw, pe + 24 + 28)[0]
    size = struct.unpack_from("<I", raw, pe + 24 + 56)[0]
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    cpu.ctl_set_cpu_model(UC_CPU_X86_HASWELL)
    cpu.mem_map(base, (size + 4095) & ~4095)
    for index in range(count):
        off = pe + 24 + opt_size + index * 40
        _, rva, length, file_off = struct.unpack_from("<4I", raw, off + 8)
        cpu.mem_write(base + rva, raw[file_off:file_off + length])
    cpu.mem_write(COOKIE_CHECK, b"\xc3")  # bypass only the stack-cookie epilogue
    cpu.mem_map(HEAP, 0x400000)
    cpu.mem_map(STACK, 0x100000)
    cpu.mem_map(RETURN, 4096)
    cpu.reg_write(UC_X86_REG_CR0, (cpu.reg_read(UC_X86_REG_CR0) & ~4) | 2)
    cpu.reg_write(UC_X86_REG_CR4, cpu.reg_read(UC_X86_REG_CR4) | 0x600)
    cpu.reg_write(UC_X86_REG_MXCSR, 0x1F80)
    return cpu


def reference(a, b, ratio, alpha):
    av = [(a >> (8 * i)) & 255 for i in range(4)]
    bv = [(b >> (8 * i)) & 255 for i in range(4)]
    if not alpha:
        out = [av[i] + (((bv[i] - av[i]) * ratio) >> 8) for i in range(4)]
    else:
        wa, wb = av[3] * (256 - ratio), bv[3] * ratio
        total = (wa + wb) & 65535
        if not total:
            return 0
        weight = (wa << 7) // total
        out = [bv[i] + (((av[i] - bv[i]) * weight) >> 7) for i in range(3)] + [total >> 8]
    return sum(max(0, min(255, value)) << (8 * i) for i, value in enumerate(out))


def main(path):
    cpu = load_native(path)
    rng = random.Random(17)
    cases = differences = max_rgb = max_alpha = 0
    for fmt in (1, 2):
        for ratio in (0, 1, 16, 64, 128, 245, 255, 256):
            for _ in range(20):
                old, new = rng.getrandbits(32), rng.getrandbits(32)
                for off, pixel in ((0, old), (64, new)):
                    cpu.mem_write(HEAP + off, struct.pack("<I", pixel))
                    cpu.mem_write(HEAP + 128 + off, struct.pack("<6I", HEAP + off, 4, 1, 1, fmt, 4))
                sp = STACK + 0x80000
                # ecx=dst, edx=old, stack=(new, ratio, parallel=0).
                cpu.mem_write(sp, struct.pack("<4I", RETURN, HEAP + 192, ratio, 0))
                cpu.reg_write(UC_X86_REG_ESP, sp)
                cpu.reg_write(UC_X86_REG_ECX, HEAP + 128)
                cpu.reg_write(UC_X86_REG_EDX, HEAP + 128)
                cpu.emu_start(ENTRY, RETURN, count=200000)
                if cpu.reg_read(UC_X86_REG_EIP) != RETURN:
                    raise RuntimeError("Native probe did not return")
                actual = struct.unpack("<I", cpu.mem_read(HEAP, 4))[0]
                expected = reference(old, new, ratio, fmt == 2)
                max_rgb = max(max_rgb, *(abs(((actual >> (8 * i)) & 255) - ((expected >> (8 * i)) & 255)) for i in range(3)))
                max_alpha = max(max_alpha, abs((actual >> 24) - (expected >> 24)))
                differences += actual != expected
                cases += 1
    print(f"native 0x{ENTRY:X}: {cases} cases, {differences} rounding differences, max RGB difference={max_rgb}, max alpha difference={max_alpha}")
    # The native reciprocal/SIMD path can round differently from the integer model.
    assert max_alpha == 0 and max_rgb <= 3


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "/Volumes/HDD-EXT/Games/巧可甜恋3/amachoco3.exe")
