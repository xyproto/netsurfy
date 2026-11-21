#!/usr/bin/env python3
# Take a single-language messages file and merge it back in to the
# TinySurf master messages (i10n) file.

import argparse
import re
import sys
import xml.etree.ElementTree as ElementTree

LANG_RE = re.compile(r"^[a-z]{2}(?:_[A-Z]{2})?$")
LINE_RE = re.compile(r"(^[a-z]{2}(?:_[A-Z]{2})?)\.([^.]+)\.([^:]+):(.*)")


def java_unescape(value):
    return re.sub(r"\\([^abfnrtv])", r"\1", value)


def parse_java(text, args):
    messages = {}
    for line in text.splitlines():
        m = re.search(r"([^#]\S+)\s*=\s?(.*)", line)
        if m:
            messages[m.group(1)] = java_unescape(m.group(2))
    return messages


def parse_messages(text, args):
    messages = {}
    for line in text.splitlines():
        m = LINE_RE.match(line)
        if not m:
            continue
        lang, plat, key, value = m.groups()
        if lang != args.lang:
            continue
        if args.plat not in ("any", plat) and plat != "all":
            continue
        messages[key] = value
    return messages


def parse_android(text, args):
    messages = {}
    root = ElementTree.fromstring(text)
    if root.tag != "resources":
        return messages
    for string in root.findall("string"):
        name = string.get("name")
        if name:
            messages[name] = "".join(string.itertext())
    return messages


PARSERS = {
    "messages": parse_messages,
    "java": parse_java,
    "transifex": parse_java,
    "android": parse_android,
}


def merge(lines, messages, args):
    out = []
    last_key = None
    last_plat = None
    for line in lines:
        lang = plat = key = None
        m = re.search(r"(^[a-z]{2}(?:_[A-Z]{2})?)\.([^.]+)\.([^:]+):", line)
        if m:
            lang, plat, key = m.groups()
        if key or messages.get(last_key):
            if last_key and messages.get(last_key) and key != last_key:
                out.append("%s.%s.%s:%s\n" % (args.lang, last_plat, last_key, messages.pop(last_key)))
                if line.strip() != "":
                    out.append("\n")
            last_key = key
            last_plat = plat
            if lang == args.lang:
                value = messages.get(key)
                if value and args.plat in ("any", plat):
                    out.append("%s.%s.%s:%s\n" % (lang, plat, key, value))
                    del messages[key]
                    continue
        out.append(line)
    return out


def main():
    parser = argparse.ArgumentParser(
        description="Merge a single-language messages file into the master messages file. "
                    "The input file may be the same as the output file, in which case it is altered in place.")
    parser.add_argument("-l", "--lang", required=True, help="language code, e.g. en, fr, zh_CN")
    parser.add_argument("-p", "--plat", "--platform", default="any", help="platform (default: any)")
    parser.add_argument("-f", "--format", "--fmt", default="messages", choices=sorted(PARSERS))
    parser.add_argument("-i", "--input", help="master messages file (default: standard input)")
    parser.add_argument("-I", "--import", dest="import_file", help="file to import")
    parser.add_argument("-o", "--output", help="output file (default: standard output)")
    parser.add_argument("files", nargs="*", help="[input-file [import-file [output-file]]]")
    args = parser.parse_args()

    positional = list(args.files)
    for name in ("input", "import_file", "output"):
        if positional and not getattr(args, name):
            setattr(args, name, positional.pop(0))
    if not LANG_RE.match(args.lang):
        parser.error("invalid language code '%s'" % args.lang)
    if not args.import_file:
        parser.error("no file specified for import-file")

    if args.input:
        with open(args.input, encoding="utf-8") as f:
            lines = f.readlines()
    else:
        lines = sys.stdin.readlines()
    with open(args.import_file, encoding="utf-8") as f:
        messages = PARSERS[args.format](f.read(), args)

    text = "".join(merge(lines, messages, args))
    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
