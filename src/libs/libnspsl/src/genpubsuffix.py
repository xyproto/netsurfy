#!/usr/bin/env python3
# Public suffix C include generator
#
# Generate a C include file representing the public suffix list
# (https://publicsuffix.org/list/public_suffix_list.dat) as a directed
# acyclic graph of huffman encoded domain labels.
#
# This file is part of libnspsl.
# Licensed under the MIT License,
#                http://www.opensource.org/licenses/mit-license.php

import heapq
import itertools
import os
import sys
import time


def punycode(label):
    if all(ord(c) < 0x80 for c in label):
        return label
    return "xn--" + label.encode("punycode").decode("ascii")


def add_rule(tree, strtab, parts):
    label = parts.pop()
    node = {}
    exception = label.startswith("!")
    if exception:
        label = label[1:]
        node["!"] = {}
    label = punycode(label)
    strtab.setdefault(label, None)
    tree.setdefault(label, node)
    if not exception and parts:
        add_rule(tree[label], strtab, parts)


def huffman_tree(labels):
    table = "*!"
    for label in labels:
        if label not in table:
            table += label
    freq = {}
    for c in table:
        freq[c] = freq.get(c, 0) + 1
    order = itertools.count()
    heap = [(count, next(order), c) for c, count in sorted(freq.items())]
    heapq.heapify(heap)
    while len(heap) > 1:
        a = heapq.heappop(heap)
        b = heapq.heappop(heap)
        heapq.heappush(heap, (a[0] + b[0], next(order), (a[2], b[2])))
    return heap[0][2]


def huffman_codes(node, code="", codes=None):
    if codes is None:
        codes = {}
    if isinstance(node, tuple):
        huffman_codes(node[0], code + "0", codes)
        huffman_codes(node[1], code + "1", codes)
    else:
        codes[node] = code
    return codes


def huffman_leaf(char, codes):
    return "    { 1, '%s' }, /* '%s': %s */\n" % (char, char, codes[char])


def huffman_node(codes, node, index):
    left, right = node
    index += 2
    table = ""
    left_table = ""
    if isinstance(left, tuple):
        left_table, left_index = huffman_node(codes, left, index)
        table += "    { 0, %d },\n" % index
    else:
        table += huffman_leaf(left, codes)
        left_index = index
    if isinstance(right, tuple):
        right_table, right_index = huffman_node(codes, right, left_index)
        table += "    { 0, %d },\n" % left_index + left_table + right_table
    else:
        table += huffman_leaf(right, codes) + left_table
        right_index = left_index
    return table, right_index


def huffman_table(tree, codes):
    table, size = huffman_node(codes, tree, 0)
    return ("/**\n"
            " * Huffman coding node\n"
            " */\n"
            "struct hnode {\n"
            "    uint8_t term:1; /**< non zero if the node terminates a code */\n"
            "    uint8_t value:7; /**< value in node */\n"
            "};\n\n"
            "/**\n"
            " * Huffman decoding table\n"
            " *\n"
            " * nodes are in pairs even nodes are for 0bit odd for 1\n"
            " */\n"
            "static const struct hnode htable[%d] = {\n%s};\n\n" % (size, table))


def hex_bits(bits):
    out = "   "
    count = 0
    for i in range(0, len(bits), 32):
        word = int(bits[i:i + 32][::-1], 2)
        out += " 0x%08X," % word
        count += 1
        if count == 6:
            out += "\n   "
            count = 0
    if count != 0:
        out += "\n"
    return out


def string_table(strtab, out):
    labels = sorted(strtab, key=len, reverse=True)
    label_size = sum(len(label) for label in strtab)
    tree = huffman_tree(labels)
    codes = huffman_codes(tree)

    def encode(text):
        return "".join(codes[c] for c in text)

    bits = encode("*")
    exception_index = len(bits)
    bits += encode("!")
    full_count = 2
    for label in labels:
        encoded = encode(label)
        index = bits.find(encoded)
        if index == -1:
            index = len(bits)
            bits += encoded
            full_count += 1
        strtab[label] = index
    if len(bits) % 32:
        bits += "0" * (32 - len(bits) % 32)

    out.append("enum stab_entities {\n")
    out.append("    STAB_WILDCARD = 0,\n")
    out.append("    STAB_EXCEPTION = %d\n" % exception_index)
    out.append("};\n\n")
    out.append(huffman_table(tree, codes))
    out.append("/**\n")
    out.append(" * Domain label string table huffman encoded.\n")
    out.append(" * %d labels(%d bits) reduced to %d labels(%d bits)\n" %
               (len(strtab), label_size * 8, full_count, len(bits)))
    out.append(" */\n")
    out.append("static const uint32_t stab[%d] = {\n" % (len(bits) // 32))
    out.append(hex_bits(bits))
    out.append("};\n\n")


def graph_nodes(parent, strtab, state):
    start = state["index"]
    for child in parent.values():
        state["index"] += 2 if child else 1
    if start == state["index"] - 1:
        text = "\n    /* entry %d */\n" % start
    else:
        text = "\n    /* entries %d to %d */\n" % (start, state["index"] - 1)
    children = ""
    for label, child in parent.items():
        state["count"] += 1
        text += "    { .label = {% 7d,% 3d" % (strtab[label], len(label.encode("utf-8")))
        if not child:
            text += ", 0 } },"
        else:
            text += ", 1 } }, { .child = { %d, %d } }," % (state["index"], len(child))
            children += graph_nodes(child, strtab, state)
        text += " /* %s */\n" % label
    return text + children


def main():
    if len(sys.argv) < 2:
        sys.exit("need filename")
    filename = sys.argv[1]
    tree = {}
    strtab = {"*": 0, "!": 1}
    with open(filename, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line != "" and "//" not in line:
                add_rule(tree, strtab, line.split("."))

    out = ["/*\n",
           " * Generated with the genpubsuffix tool.\n",
           " *  From file %s\n" % os.path.basename(filename),
           " *  Converted on %s\n" % time.ctime(),
           " */\n\n",
           "/**\n",
           " * Public suffix list graph node\n",
           " */\n",
           "union pnode {\n",
           "    struct {\n",
           "        unsigned int idx:24; /**< index of domain element in string table */\n",
           "        unsigned int len:6; /**< length of domain element in string table */\n",
           "        unsigned int children:1; /**< has children */\n",
           "    } label;\n",
           "    struct {\n",
           "        uint16_t index; /**< index of first child node */\n",
           "        uint16_t count; /**< number of children of this node */\n",
           "    } child;\n",
           "};\n\n"]
    string_table(strtab, out)

    state = {"index": 2, "count": 1}
    nodes = "    /* root entry */\n"
    nodes += "    { .label = { 0, 0, 1 } }, { .child = { 2, %d } }," % len(tree)
    nodes += graph_nodes(tree, strtab, state)
    out.append("/**\n")
    out.append(" * PSL represented as a directed acyclic graph\n")
    out.append(" * There are %d labels in %d nodes\n" % (state["count"], state["index"]))
    out.append(" */\n")
    out.append("static const union pnode pnodes[%d] = {\n" % state["index"])
    out.append(nodes)
    out.append("\n};\n\n")
    sys.stdout.write("".join(out))


if __name__ == "__main__":
    main()
