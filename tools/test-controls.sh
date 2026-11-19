#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
sdk_root=${VITASDK:-"$PWD/.deps/vitasdk"}
if [ ! -f "$sdk_root/arm-vita-eabi/include/psp2/ctrl.h" ]; then
    echo 'Set VITASDK or download the headers with: python3 -I tools/deps.py vitasdk' >&2
    exit 1
fi
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Iinclude \
    -idirafter "$sdk_root/arm-vita-eabi/include" \
    tests/controls_test.cpp src/lib/controls.cpp src/platform/config.cpp -o "$test_dir/controls-test"
"$test_dir/controls-test"
"$test_dir/controls-test" examples/nfsug1vita.cfg
printf 'rear_touch=on\n' > "$test_dir/rear-on.cfg"
"$test_dir/controls-test" "$test_dir/rear-on.cfg" on
