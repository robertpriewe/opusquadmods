#!/usr/bin/env python3
"""Minimum OPUS firmware for a built add-on (mod.so), from the API size checks it makes.

Every SDK release grows struct oqmod_api. A mod that checks `api->size` against a size
(sizeof *a, or offsetof(field)+8 for a newer call) needs the firmware whose API is that big.
This reads the aarch64 disassembly (objdump -d, given on stdin or run in the SDK image) and
reports the newest release whose API size appears as a compare immediate. Features that do
not change the API size (R124: LINK USB sources) are declared in MODS/<mod>/min-firmware.txt;
the larger of the two wins.

    min_firmware.py build/<mod>/mod.so [--declared R120]   -> prints e.g. R123
"""
import re, subprocess, sys
from pathlib import Path
# sizeof(struct oqmod_api) per firmware release (aarch64). R114..R120 share 0x238.
API_SIZES = [("R120", 0x238), ("R121", 0x240), ("R122", 0x250), ("R123", 0x268)]
BASELINE = "R120"
LOAD = re.compile(r"\bldr\s+w(\d+),\s*\[x\d+,\s*#4\]")          # api->size (offset 4)
CMP = re.compile(r"\bcmp\s+w(\d+),\s*#(0x[0-9a-f]+|\d+)")

def rnum(r): return int(r.lstrip("Rr"))

def detect(disassembly):
    """Only compares of a register loaded from [x?, #4] up to 12 instructions earlier."""
    found = BASELINE
    loaded = {}   # register -> instruction index of its api->size load
    for i, line in enumerate(disassembly.splitlines()):
        m = LOAD.search(line)
        if m: loaded[m.group(1)] = i; continue
        m = CMP.search(line)
        if not m or m.group(1) not in loaded or i - loaded[m.group(1)] > 12: continue
        v = int(m.group(2), 0)
        # `size < X` compiles to cmp #X or #X-1. X is sizeof *a or offsetof(call)+8:
        # the release is the first whose API is at least that big (optional calls count
        # too: the minimum means every feature works).
        for want in (v, v + 1):
            for release, size in API_SIZES:
                if size >= want:
                    if want > API_SIZES[0][1] - 8 and rnum(release) > rnum(found): found = release
                    break
    return found

def disassemble(so):
    so = Path(so).resolve()
    for tool in ("aarch64-linux-gnu-objdump", "llvm-objdump"):
        try: return subprocess.run([tool, "-d", str(so)], capture_output=True, text=True, check=True).stdout
        except (FileNotFoundError, subprocess.CalledProcessError): pass
    return subprocess.run(["docker", "run", "--rm", "-v", f"{so.parent}:/m:ro", "oqmod-sdk",
                           "aarch64-linux-gnu-objdump", "-d", f"/m/{so.name}"], capture_output=True, text=True, check=True).stdout

def main():
    args = sys.argv[1:]
    declared = BASELINE
    if "--declared" in args:
        i = args.index("--declared"); declared = args[i + 1]; del args[i:i + 2]
    text = sys.stdin.read() if args == ["-"] else disassemble(args[0])
    detected = detect(text)
    print(max(declared, detected, key=rnum))
    if rnum(declared) < rnum(detected):
        print(f"note: min-firmware.txt says {declared}, but the binary checks for the {detected} API", file=sys.stderr)

if __name__ == "__main__":
    main()
