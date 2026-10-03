"""Disassembles raw bytes of H1Z1.exe (for code Ghidra has no function at,
or jump-table cases it missed).

  python tools/disasm.py 0x14162d320 [count]
"""
import sys

import capstone

from rtti_names import EXE, Pe

pe = Pe(EXE)
va = int(sys.argv[1], 16)
count = int(sys.argv[2]) if len(sys.argv) > 2 else 40
off = pe.offset(va)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
for n, ins in enumerate(md.disasm(pe.data[off:off + count * 15], va)):
    if n >= count:
        break
    print(f"{ins.address:x}: {ins.mnemonic} {ins.op_str}")
    if ins.mnemonic in ("ret", "int3") or (ins.mnemonic == "jmp" and n > 0 and "[" not in ins.op_str):
        break
