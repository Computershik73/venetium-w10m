#!/usr/bin/env python3
# The renderer thread's hottest engine code, by function: reads the last
# "sample: ... hottest engine code" line of a phone's log (vw_hang_watch.cc),
# names each engine+offset with llvm-symbolizer against the engine the phone
# ran (out/w10m/venetium_engine.dll and its .pdb: run this before the next
# build), and adds up the pieces of each function.
#
#   python tools/hot-functions.py work/phone-650/venetium.log
import collections
import os
import re
import subprocess
import sys

SRC = os.environ.get("VW_SRC", r"C:\vw\src")
OUT = os.environ.get("VW_OUT", r"out\w10m")
SYMBOLIZER = os.path.join(SRC, r"third_party\llvm-build\Release+Asserts\bin\llvm-symbolizer.exe")
ENGINE = os.path.join(SRC, OUT, "venetium_engine.dll")


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: hot-functions.py <venetium.log>...")
    line = None
    for path in sys.argv[1:]:
        with open(path, encoding="utf-8", errors="replace") as f:
            for text in f:
                if "hottest engine code" in text:
                    line = text
    if not line:
        sys.exit("no 'hottest engine code' line")
    print(line.strip()[:160] + " ...")
    pieces = re.findall(r"engine\+0x([0-9a-f]+) ([0-9.]+)%", line)
    if not pieces:
        sys.exit("no addresses")
    # The middle of each 64-byte piece.
    query = "".join("0x%x\n" % (int(offset, 16) + 32) for offset, _ in pieces)
    result = subprocess.run(
        [SYMBOLIZER, "--obj=" + ENGINE, "--relative-address", "--functions=linkage",
         "--demangle", "--no-inlines"],
        input=query, capture_output=True, text=True, check=True)
    blocks = [b for b in result.stdout.split("\n\n") if b.strip()]
    by_function = collections.OrderedDict()
    for (offset, percent), block in zip(pieces, blocks):
        name = block.strip().splitlines()[0]
        if name == "??":
            name = "?? (engine+0x%s)" % offset
        by_function[name] = by_function.get(name, 0.0) + float(percent)
    for name, percent in sorted(by_function.items(), key=lambda kv: -kv[1]):
        print("%5.1f%%  %s" % (percent, name[:150]))


if __name__ == "__main__":
    main()
