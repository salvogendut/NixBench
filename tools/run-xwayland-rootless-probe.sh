#!/bin/sh

set -eu

fail()
{
    echo "nixbench rootless Xwayland probe: $*" >&2
    exit 1
}

if [ "$#" -ne 0 ]; then
    echo "usage: $0" >&2
    exit 2
fi

script_path=$0
case "$script_path" in
    */*) ;;
    *)
        script_path=$(command -v "$script_path") ||
            fail "could not locate the launcher script"
        ;;
esac
while [ -L "$script_path" ]; do
    link_dir=$(CDPATH= cd "$(dirname "$script_path")" && pwd)
    link_target=$(readlink "$script_path") ||
        fail "could not resolve $script_path"
    case "$link_target" in
        /*) script_path=$link_target ;;
        *) script_path=$link_dir/$link_target ;;
    esac
done

script_dir=$(CDPATH= cd "$(dirname "$script_path")" && pwd)
repo_dir=$(CDPATH= cd "$script_dir/.." && pwd)
build_dir=${NIXBENCH_BUILD_DIR:-$repo_dir/build}
case "$build_dir" in
    /*) ;;
    *) build_dir=$repo_dir/$build_dir ;;
esac
probe=${NIXBENCH_X11_PROBE:-$build_dir/nixbench_x11_probe}

if [ -z "${DISPLAY:-}" ]; then
    fail "DISPLAY is unset; enable NIXBENCH_XWAYLAND_ROOTLESS=1"
fi
if [ ! -x "$probe" ]; then
    fail "$probe is not available; build NixBench first"
fi

echo "NixBench rootless Xwayland probe: opening one managed X11 top-level on $DISPLAY" >&2
exec "$probe" \
    --title "NixBench Rootless X11 Probe" \
    --message "This X11 client is an independent NixBench window."
