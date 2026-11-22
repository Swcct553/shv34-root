#!/usr/bin/env python3
"""Disassemble a virtual-address range of the SHV34 SBL1 (xbl.bin) ELF.

usage: dis.py <va_hex> <len_hex> [out.txt]
SBL1 PH2 mapping: file 0x3000 -> VA 0x6208000 (filesz 0x5fdac)
"""
import sys, struct, re
from capstone import *
from capstone.arm64 import *

XBL = 'bl-research/bldump/xbl.bin'
d = open(XBL, 'rb').read()
phoff, n, es = 0x40, 20, 56
segs = []
for i in range(n):
    o = phoff + i * es
    t, fl, off, va, pa, fsz, msz, al = struct.unpack_from('<IIQQQQQQ', d, o)
    if t == 1 and fsz:
        segs.append((va, off, fsz))

def va2off(va):
    for v, o, sz in segs:
        if v <= va < v + sz:
            return o + (va - v)
    return None

def off2va(off):
    for v, o, sz in segs:
        if o <= off < o + sz:
            return v + (off - o)
    return None

def str_at(va):
    o = va2off(va)
    if o is None:
        return None
    m = re.match(rb'[ -~]{4,80}', d[o:o + 80])
    return m.group().decode('ascii', 'replace') if m else None

md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
md.detail = True

def dis(va, length, out=None):
    off = va2off(va)
    if off is None:
        print('VA not mapped'); return
    code = d[off:off + length]
    lines = []
    for ins in md.disasm(code, va):
        s = '%08x: %-10s %-8s %s' % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str)
        # annotate branch targets and literal addresses
        for op in ins.operands:
            if op.type == ARM64_OP_IMM:
                t = op.imm
                st = str_at(t)
                if st:
                    s += '   ; "%s"' % st[:60]
        if ins.mnemonic in ('bl', 'b', 'b.eq', 'b.ne', 'cbz', 'cbnz', 'tbz', 'tbnz'):
            pass
        lines.append(s)
    txt = '\n'.join(lines)
    if out:
        open(out, 'w', encoding='utf-8').write(txt + '\n')
        print('wrote', out, len(lines), 'instructions')
    else:
        print(txt)

if __name__ == '__main__':
    va = int(sys.argv[1], 16)
    ln = int(sys.argv[2], 16)
    out = sys.argv[3] if len(sys.argv) > 3 else None
    dis(va, ln, out)
