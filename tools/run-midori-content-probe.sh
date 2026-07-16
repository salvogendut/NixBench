#!/bin/sh

set -eu

midori=/usr/pkg/bin/midori
page='data:text/html,%3Chtml%3E%3Chead%3E'
page="${page}"'%3Ctitle%3ENixBench%20WebKit%20probe%3C%2Ftitle%3E'
page="${page}"'%3C%2Fhead%3E%3Cbody%20style%3D%22background%3A%23fff%3B'
page="${page}"'color%3A%23111%3Bfont-family%3Asans-serif%3Bfont-size%3A28px%3B'
page="${page}"'padding%3A24px%22%3E%3Ch1%3ENixBench%20WebKit%20probe%3C%2Fh1%3E'
page="${page}"'%3Cp%3EExisting%20GTK3%20and%20WebKit%20page%20content%20'
page="${page}"'rendered%20without%20X.org.%3C%2Fp%3E'
page="${page}"'%3Cp%3EKeyboard%20trial%3A%20press%20Ctrl-L%2C%20enter%20'
page="${page}"'%3Ccode%3Edata%3Atext%2Fplain%2Cnixbench-keyboard%3C%2Fcode%3E%2C%20'
page="${page}"'then%20press%20Return.%3C%2Fp%3E%3C%2Fbody%3E%3C%2Fhtml%3E'

if [ "$#" -ne 0 ]; then
    echo "usage: $0" >&2
    exit 2
fi
if [ ! -f "$midori" ] || [ ! -x "$midori" ]; then
    echo "nixbench Midori content probe: $midori is not an executable regular file" >&2
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
                echo "nixbench Midori content probe: could not locate the probe script" >&2
                exit 127
            }
        ;;
esac
while [ -L "$script_path" ]; do
    link_dir=$(CDPATH= cd "$(dirname "$script_path")" && pwd)
    link_target=$(readlink "$script_path") ||
        {
            echo "nixbench Midori content probe: could not resolve $script_path" >&2
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
WEBKIT_DISABLE_COMPOSITING_MODE=1
export WEBKIT_DISABLE_COMPOSITING_MODE
GTK_CSD=0
export GTK_CSD
. "$repo_dir/tools/gtk-menu-bridge-env.sh"
nixbench_enable_gtk_menu_bridge "$build_dir" "nixbench Midori probe"
XDG_CONFIG_HOME=$profile_root/config
XDG_CACHE_HOME=$profile_root/cache
XDG_DATA_HOME=$profile_root/data
XDG_STATE_HOME=$profile_root/state
mkdir -p "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$XDG_DATA_HOME" "$XDG_STATE_HOME"
export XDG_CONFIG_HOME XDG_CACHE_HOME XDG_DATA_HOME XDG_STATE_HOME

if [ "${NIXBENCH_TRACE_WAYLAND:-0}" = 1 ]; then
    WAYLAND_DEBUG=client
    export WAYLAND_DEBUG
    echo "NixBench Midori content probe: Wayland trace enabled" >&2
fi

echo "NixBench Midori content probe: fixed offline page, software compositing" >&2
if "$midori" "$page"; then
    status=0
else
    status=$?
fi
cleanup
trap - EXIT INT TERM HUP
exit "$status"
