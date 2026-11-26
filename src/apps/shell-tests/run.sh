#!/bin/sh
# Runs a gesture script against a native DiaDraw app (macOS or gtkmm) and compares its log with
# the expected one. Every shell must produce the same text.
# usage: run.sh <path to the app executable> <name>   (<name>.script and <name>.expected here)
set -eu
app=$1
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$here/gestures.draw" "$work/test.draw"
cp "$here/$2.script" "$work/$2.script"
DIADRAW_SCRIPT="$work/$2.script" "$app" "$work/test.draw" >/dev/null 2>&1
diff -u "$here/$2.expected" "$work/$2.script.log"
