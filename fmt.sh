#!/bin/sh
set -e
cd "$(dirname "$0")"
git ls-files -z '*.c' '*.h' '*.cpp' ':!content/handlers/javascript/duktape/duktape.[ch]' ':!content/handlers/javascript/duktape/duk_config.h' | xargs -0 -P "$(nproc)" clang-format -style=file -i
