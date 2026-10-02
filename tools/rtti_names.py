"""Names every virtual function in H1Z1.exe after its owning class, using the
MSVC RTTI left in the binary, and pushes the names into the open Ghidra
project through the GhidraMCP HTTP plugin.

  python tools/rtti_names.py --dry-run      # write rtti_vtables.tsv only
  python tools/rtti_names.py                # also rename in Ghidra

Only functions still called FUN_<addr> are renamed, so names set by hand win.
A function shared by many vtables (COMDAT-folded stubs like `return 0`) is
left alone because no single class owns it.
"""
import argparse
import concurrent.futures
import re
import struct
import sys
import urllib.parse
import urllib.request
import zlib

EXE = r"C:\Users\zam\Documents\H1emu\H1Z1.exe"
GHIDRA = "http://127.0.0.1:8080"
MAX_OWNERS = 12  # more vtables than this sharing one function -> skip it


class Pe:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        opt = struct.unpack_from("<H", d, pe + 20)[0]
        self.base = struct.unpack_from("<Q", d, pe + 24 + 24)[0]
        self.sections = []
        for i in range(nsec):
            name, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", d, pe + 24 + opt + i * 40)
            self.sections.append((name.rstrip(b"\0").decode(), va, max(vsize, rsize), raw, rsize))
        self.code = [(self.base + va, self.base + va + size)
                     for n, va, size, _, _ in self.sections if n == ".text"]

    def offset(self, va):
        rva = va - self.base
        for _, sva, size, raw, rsize in self.sections:
            if sva <= rva < sva + size and rva - sva < rsize:
                return raw + rva - sva
        return None

    def is_code(self, va):
        return any(lo <= va < hi for lo, hi in self.code)

    def section(self, name):
        return [(self.base + va, raw, rsize) for n, va, _, raw, rsize in self.sections if n == name]


def demangle_type(mangled):
    """'.?AVUdpPlatformDriver@UdpLibrary@@' -> 'UdpLibrary__UdpPlatformDriver'."""
    body = mangled[4:] if mangled.startswith((".?AV", ".?AU")) else mangled
    if "?$" in body:
        # Template instance: keep the template's name, disambiguate by hash.
        name = re.match(r"\?\$([A-Za-z_0-9]+)", body[body.index("?$"):]).group(1)
        return f"{name}_T{zlib.crc32(mangled.encode()) & 0xFFFFFF:06x}"
    parts = [p for p in body.rstrip("@").split("@") if p]
    parts = ["anon" if p.startswith("?A") else p for p in parts]
    return "__".join(re.sub(r"\W", "_", p) for p in reversed(parts))


def find_vtables(pe):
    d = pe.data
    cols = {}
    for va0, raw, size in pe.section(".rdata"):
        for o in range(raw, raw + size - 24, 4):
            if d[o:o + 4] != b"\x01\0\0\0":
                continue
            self_rva = struct.unpack_from("<I", d, o + 20)[0]
            if self_rva != va0 - pe.base + (o - raw):
                continue
            td_off = pe.offset(pe.base + struct.unpack_from("<I", d, o + 12)[0])
            if td_off is None:
                continue
            end = d.find(b"\0", td_off + 16)
            name = d[td_off + 16:end].decode(errors="replace")
            offset_in_class = struct.unpack_from("<I", d, o + 4)[0]
            cols[va0 + (o - raw)] = (name, offset_in_class)

    vtables = []
    for va0, raw, size in pe.section(".rdata"):
        for o in range(raw, raw + size - 8, 8):
            ptr = struct.unpack_from("<Q", d, o)[0]
            if ptr not in cols:
                continue
            vt = va0 + (o - raw) + 8
            slots = []
            p = o + 8
            while p + 8 <= raw + size:
                f = struct.unpack_from("<Q", d, p)[0]
                if not pe.is_code(f) or (slots and struct.unpack_from("<Q", d, p)[0] in cols):
                    break
                slots.append(f)
                p += 8
                # the next vtable's COL pointer ends this one
                if p + 8 <= raw + size and struct.unpack_from("<Q", d, p)[0] in cols:
                    break
            if slots:
                name, sub = cols[ptr]
                vtables.append((vt, name, sub, slots))
    return vtables


def ghidra_functions():
    url = f"{GHIDRA}/methods?offset=0&limit=1000000"
    names = urllib.request.urlopen(url, timeout=300).read().decode().splitlines()
    return {int(n[4:], 16) for n in names if re.fullmatch(r"FUN_[0-9a-f]+", n)}


def rename(addr, name):
    body = urllib.parse.urlencode({"function_address": f"0x{addr:x}", "new_name": name}).encode()
    try:
        return urllib.request.urlopen(f"{GHIDRA}/rename_function_by_address", body, timeout=120).read().decode()
    except Exception as e:  # noqa: BLE001 - report and continue
        return f"error {e}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--out", default="rtti_vtables.tsv")
    args = ap.parse_args()

    pe = Pe(EXE)
    vtables = find_vtables(pe)
    print(f"{len(vtables)} vtables, {len({n for _, n, _, _ in vtables})} classes", flush=True)

    owners = {}  # function -> [(vtable length, class, sub-object offset, slot)]
    with open(args.out, "w") as tsv:
        tsv.write("vtable\tclass\tmangled\tsubobject\tslots\n")
        for vt, mangled, sub, slots in vtables:
            cls = demangle_type(mangled)
            tsv.write(f"0x{vt:x}\t{cls}\t{mangled}\t0x{sub:x}\t{len(slots)}\n")
            for i, f in enumerate(slots):
                owners.setdefault(f, []).append((len(slots), cls, sub, i))

    plan = {}
    for f, refs in owners.items():
        if len({r[1] for r in refs}) > MAX_OWNERS:
            continue
        _, cls, sub, slot = min(refs)  # smallest vtable = most basic class
        plan[f] = f"{cls}_vf{slot:02d}" if sub == 0 else f"{cls}_b{sub:x}_vf{slot:02d}"
    print(f"{len(owners)} virtual functions, {len(plan)} with a clear owner", flush=True)
    if args.dry_run:
        return

    unnamed = ghidra_functions()
    todo = [(f, n) for f, n in plan.items() if f in unnamed]
    print(f"{len(todo)} still unnamed in Ghidra -> renaming", flush=True)
    done = failed = 0
    with concurrent.futures.ThreadPoolExecutor(4) as pool:
        for result in pool.map(lambda t: rename(*t), todo):
            if "success" in result.lower():
                done += 1
            else:
                failed += 1
            if (done + failed) % 2000 == 0:
                print(f"  {done + failed}/{len(todo)} ({failed} failed)", flush=True)
    print(f"renamed {done}, failed {failed}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
