#!/bin/sh

set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 helper built-module temporary-root" >&2
    exit 2
fi

helper=$1
built_module=$2
temporary_root=$3
test_root=$temporary_root/nixbench-gtk-menu-bridge-env-test.$$
build_dir=$test_root/build
expected_module=$build_dir/gtk-modules/libnixbench_gtk_menu_bridge.so

cleanup()
{
    rm -rf "$test_root"
}
trap cleanup EXIT INT TERM HUP

. "$helper"

mkdir -p "$build_dir/gtk-modules"

NIXBENCH_GTK_MENU_BRIDGE=0
GTK3_MODULES=existing-module
export NIXBENCH_GTK_MENU_BRIDGE GTK3_MODULES
nixbench_enable_gtk_menu_bridge "$build_dir" test-probe
if [ "$GTK3_MODULES" != existing-module ]; then
    echo "disabled bridge changed GTK3_MODULES" >&2
    exit 1
fi

NIXBENCH_GTK_MENU_BRIDGE=1
if nixbench_enable_gtk_menu_bridge "$build_dir" test-probe 2>/dev/null; then
    echo "missing requested bridge was accepted" >&2
    exit 1
fi

cp "$built_module" "$expected_module"
GTK3_MODULES=existing-module
nixbench_enable_gtk_menu_bridge "$build_dir" test-probe
if [ "$GTK3_MODULES" != "$expected_module:existing-module" ]; then
    echo "bridge module was not prepended to GTK3_MODULES" >&2
    exit 1
fi

echo "GTK menu bridge environment tests: ok"
