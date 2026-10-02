"""Pushes the name of every REBUILD_FUNCTION / REBUILD_FUNCTION_TOO_SMALL in
src/ into the open Ghidra project (GhidraMCP plugin on 127.0.0.1:8080), so the
Ghidra listing always shows which functions are rebuilt and what they are.

  python tools/sync_ghidra_names.py
"""
import pathlib
import re
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
PATTERN = re.compile(r"REBUILD_FUNCTION(?:_TOO_SMALL)?\(\s*(\w+)\s*,\s*(0x[0-9a-fA-F]+)")


def main():
    names = {}
    for source in (ROOT / "src").rglob("*.cpp"):
        for name, address in PATTERN.findall(source.read_text()):
            names[int(address, 16)] = name
    failed = []
    for address, name in sorted(names.items()):
        body = urllib.parse.urlencode({"function_address": hex(address), "new_name": name}).encode()
        reply = urllib.request.urlopen("http://127.0.0.1:8080/rename_function_by_address", body,
                                       timeout=60).read().decode()
        if "success" not in reply.lower():
            failed.append(f"{hex(address)} {name}: {reply.strip()}")
    print(f"synced {len(names) - len(failed)}/{len(names)} names")
    for line in failed:
        print("  failed", line)


if __name__ == "__main__":
    main()
