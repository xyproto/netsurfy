#!/bin/sh
set -eu
cd "$(dirname "$0")"

current=$(sed -n 's/^project(NetSurfy VERSION \([0-9][0-9.]*\) .*/\1/p' CMakeLists.txt)
[ -n "$current" ] || { echo "version.sh: no version in CMakeLists.txt" >&2; exit 1; }

lines() {
  major=${1%%.*}
  rest=${1#*.}
  minor=${rest%%.*}
  printf '%s\t%s\n' \
    CMakeLists.txt "project(NetSurfy VERSION $1 LANGUAGES C)" \
    README.md "* Version: $1" \
    src/desktop/version.c "const char *const netsurfy_version = \"$1\"" \
    src/desktop/version.c "const int netsurfy_version_major = $major;" \
    src/desktop/version.c "const int netsurfy_version_minor = $minor;"
}

check() {
  bad=0
  tab=$(printf '\t')
  while IFS=$tab read -r file line; do
    if ! grep -qxF "$line" "$file"; then
      echo "  $file does not have: $line" >&2
      bad=1
    fi
  done <<EOF
$(lines "$1")
EOF
  return $bad
}

newer() {
  echo "$1 $2" | awk '{ split($1, a, "."); split($2, b, ".");
    for (i = 1; i <= 3; i++) { if (a[i] + 0 > b[i] + 0) exit 0; if (a[i] + 0 < b[i] + 0) exit 1 } exit 1 }'
}

edit() {
  sed "$2" "$1" > "$1.tmp"
  cat "$1.tmp" > "$1"
  rm -f "$1.tmp"
}

if [ $# -eq 0 ]; then
  echo "NetSurfy $current"
  check "$current" || { echo "Not every file has version $current, run: $0 $current" >&2; exit 1; }
  echo "Pass a new version, like $0 X.Y.Z, to change it."
  exit 0
fi

new=$1
echo "$new" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$' || { echo "version.sh: $new is not a version like 1.2.3" >&2; exit 1; }
if [ "$new" = "$current" ]; then
  echo "Setting every file to version $new"
elif newer "$new" "$current"; then
  echo "Changing the version from $current to $new"
else
  echo "version.sh: $new is older than the current version $current" >&2
  exit 1
fi

major=${new%%.*}
rest=${new#*.}
minor=${rest%%.*}
edit CMakeLists.txt "s/^project(NetSurfy VERSION [0-9][0-9.]* /project(NetSurfy VERSION $new /"
edit README.md "s/^\\* Version: .*/* Version: $new/"
edit src/desktop/version.c "s/^\\(const char \\*const netsurfy_version = \"\\)[^\"]*\"/\\1$new\"/
s/^\\(const int netsurfy_version_major = \\)[0-9]*;/\\1$major;/
s/^\\(const int netsurfy_version_minor = \\)[0-9]*;/\\1$minor;/"

check "$new" || { echo "version.sh: some files were not updated, see above" >&2; exit 1; }
git diff --stat 2>/dev/null || true
