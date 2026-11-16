#!/usr/bin/env python3
# Convert a Unicode mapping table (http://unicode.org/Public/MAPPINGS) for an
# 8-bit encoding into the C table of its upper 128 code points.
#
# Usage: conv.py <input_file>

import sys


def main():
    if len(sys.argv) != 2:
        sys.exit("Usage: conv.py <input_file>")
    table = []
    with open(sys.argv[1], encoding="latin-1") as f:
        for line in f:
            if line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 2 or int(parts[0], 16) < 0x80:
                continue
            table.append("0xFFFF" if parts[1].startswith("#") else parts[1])
    rows = [", ".join(table[i:i + 8]) for i in range(0, len(table), 8)]
    print("static uint32_t %s[128] = {\n\t%s, \n};\n" % (sys.argv[1], ", \n\t".join(rows)))


if __name__ == "__main__":
    main()
