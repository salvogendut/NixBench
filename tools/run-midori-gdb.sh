#!/bin/sh

set -eu

gdb=/usr/bin/gdb
midori=/usr/pkg/bin/midori

if [ "$#" -ne 0 ]; then
    echo "usage: $0" >&2
    exit 2
fi
if [ ! -x "$gdb" ]; then
    echo "nixbench Midori diagnostic: $gdb is not executable" >&2
    exit 127
fi
if [ ! -f "$midori" ] || [ ! -x "$midori" ]; then
    echo "nixbench Midori diagnostic: $midori is not an executable regular file" >&2
    exit 127
fi

profile_root=$(mktemp -d "${TMPDIR:-/tmp}/nixbench-midori-profile.XXXXXX")
cleanup() {
    rm -rf "$profile_root"
}
trap cleanup EXIT INT TERM HUP

script_path=$0
case "$script_path" in
    */*) ;;
    *)
        script_path=$(command -v "$script_path") ||
            {
                echo "nixbench Midori diagnostic: could not locate the probe script" >&2
                exit 127
            }
        ;;
esac
while [ -L "$script_path" ]; do
    link_dir=$(CDPATH= cd "$(dirname "$script_path")" && pwd)
    link_target=$(readlink "$script_path") ||
        {
            echo "nixbench Midori diagnostic: could not resolve $script_path" >&2
            exit 127
        }
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
gtk_menu_bridge_module=$build_dir/gtk-modules/libnixbench_gtk_menu_bridge.so

WEBKIT_DISABLE_COMPOSITING_MODE=1
export WEBKIT_DISABLE_COMPOSITING_MODE
GTK_CSD=0
export GTK_CSD
if [ "${NIXBENCH_GTK_MENU_BRIDGE:-0}" = 1 ] &&
   [ -r "$gtk_menu_bridge_module" ]; then
    GTK_MODULES=$gtk_menu_bridge_module${GTK_MODULES:+:$GTK_MODULES}
    export GTK_MODULES
fi
XDG_CONFIG_HOME=$profile_root/config
XDG_CACHE_HOME=$profile_root/cache
XDG_DATA_HOME=$profile_root/data
XDG_STATE_HOME=$profile_root/state
mkdir -p "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$XDG_DATA_HOME" "$XDG_STATE_HOME"
export XDG_CONFIG_HOME XDG_CACHE_HOME XDG_DATA_HOME XDG_STATE_HOME

if [ "${NIXBENCH_TRACE_WAYLAND:-0}" = 1 ]; then
    WAYLAND_DEBUG=client
    export WAYLAND_DEBUG
    echo "NixBench Midori diagnostic: Wayland trace enabled" >&2
fi

echo "NixBench Midori diagnostic: software compositing under ordinary-user GDB" >&2
if "$gdb" --quiet --nx --batch \
    -ex 'set pagination off' \
    -ex 'set confirm off' \
    -ex 'set startup-with-shell off' \
    -ex 'set follow-fork-mode parent' \
    -ex 'set detach-on-fork on' \
    -ex 'set print thread-events off' \
    -ex 'handle SIGPIPE nostop noprint pass' \
    -ex run \
    -ex 'echo \n===== NixBench Midori GDB backtrace =====\n' \
    -ex 'info program' \
    -ex 'bt 64' \
    -ex 'thread apply all bt 12' \
    -ex 'echo \n===== Loaded shared libraries =====\n' \
    -ex 'info sharedlibrary' \
    --args "$midori"; then
    status=0
else
    status=$?
fi
cleanup
trap - EXIT INT TERM HUP
exit "$status"
