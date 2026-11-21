#!/usr/bin/env python3
# Run self-checking HTML test pages in the headless headless frontend.

import argparse
import os
import select
import subprocess
import sys
import time

LOG_MARKER = " NOT-FOLDABLE LOG "


def run_page(headless, page, timeout):
    proc = subprocess.Popen([headless, "--enable_javascript=1"],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, bufsize=1)
    results = []
    errors = []
    done = False

    def send(command):
        try:
            proc.stdin.write(command + "\n")
            proc.stdin.flush()
        except OSError:
            pass

    send("WINDOW NEW file://" + os.path.abspath(page))
    deadline = time.monotonic() + timeout
    while not done and time.monotonic() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.1)
        if not ready:
            if proc.poll() is not None:
                break
            continue
        line = proc.stdout.readline()
        if not line:
            break
        line = line.rstrip("\n")
        if LOG_MARKER in line:
            message = line.split(LOG_MARKER, 1)[1]
            if message.startswith("TEST DONE"):
                done = True
            elif message.startswith("TEST "):
                results.append(message[5:])
        elif "Uncaught" in line or "scripting-error" in line or "AddressSanitizer" in line or "runtime error" in line:
            errors.append(line)
    send("QUIT")
    try:
        proc.stdin.close()
    except OSError:
        pass
    try:
        rest, _ = proc.communicate(timeout=20)
        for line in rest.splitlines():
            if "AddressSanitizer" in line or "runtime error" in line:
                errors.append(line)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()
        errors.append("headless did not exit after QUIT")
    if proc.returncode not in (0, None) and not errors:
        errors.append("headless exited with status %d" % proc.returncode)
    if not done:
        errors.append("page did not finish within %ds" % timeout)
    return results, errors


def main():
    parser = argparse.ArgumentParser(description="Run TinySurf browser test pages")
    parser.add_argument("--headless", required=True, help="path to tinysurf-headless")
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("-v", "--verbose", action="store_true", help="also print passing tests")
    parser.add_argument("pages", nargs="+")
    args = parser.parse_args()

    failed = 0
    for page in args.pages:
        results, errors = run_page(args.headless, page, args.timeout)
        name = os.path.basename(page)
        for result in results:
            if args.verbose and result.startswith("PASS"):
                print("%s: %s" % (name, result))
            if result.startswith("FAIL"):
                failed += 1
                print("%s: %s" % (name, result))
        for error in errors:
            failed += 1
            print("%s: ERROR %s" % (name, error))
        passes = sum(1 for r in results if r.startswith("PASS"))
        print("%s: %d passed, %d failed" % (name, passes, len(results) - passes + len(errors)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
