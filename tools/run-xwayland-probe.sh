#!/bin/sh

set -eu

fail()
{
    echo "nixbench Xwayland probe: $*" >&2
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

xwayland=${NIXBENCH_XWAYLAND:-$(command -v Xwayland || true)}
probe=$build_dir/nixbench_x11_probe

if [ -z "$xwayland" ] || [ ! -x "$xwayland" ]; then
    fail "Xwayland is not installed or not executable"
fi
if [ ! -x "$probe" ]; then
    fail "$probe is not available; build NixBench first"
fi

display_number=${NIXBENCH_XWAYLAND_DISPLAY:-98}
case "$display_number" in
    ''|0|0*|*[!0-9]*)
        fail "NIXBENCH_XWAYLAND_DISPLAY must be a positive display number"
        ;;
esac
while [ "$display_number" -le 127 ]; do
    socket=/tmp/.X11-unix/X${display_number}
    lock=/tmp/.X${display_number}-lock
    if [ ! -e "$socket" ] && [ ! -e "$lock" ]; then
        break
    fi
    display_number=$((display_number + 1))
done
if [ "$display_number" -gt 127 ]; then
    fail "could not find a free Xwayland display number"
fi

cleanup()
{
    if [ -n "${xwayland_pid:-}" ] && kill -0 "$xwayland_pid" 2>/dev/null; then
        kill "$xwayland_pid" 2>/dev/null || true
        wait "$xwayland_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM HUP

echo "NixBench Xwayland probe: launching rootful Xwayland on :$display_number" >&2
echo "NixBench Xwayland probe: forcing the Wayland shared-memory backend" >&2
if [ "${NIXBENCH_TRACE_WAYLAND:-0}" = 1 ]; then
    WAYLAND_DEBUG=client
    export WAYLAND_DEBUG
    echo "NixBench Xwayland probe: Wayland trace enabled" >&2
fi

(
    exec "$xwayland" ":$display_number" \
        -ac \
        -terminate \
        -geometry 1024x640 \
        -shm
) &
xwayland_pid=$!

for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    if [ -S "/tmp/.X11-unix/X${display_number}" ]; then
        break
    fi
    if ! kill -0 "$xwayland_pid" 2>/dev/null; then
        wait "$xwayland_pid" || true
        fail "Xwayland exited before opening the display"
    fi
    sleep 0.1
done

if [ ! -S "/tmp/.X11-unix/X${display_number}" ]; then
    fail "Xwayland did not create /tmp/.X11-unix/X${display_number}"
fi

DISPLAY=:"$display_number"
export DISPLAY

echo "NixBench Xwayland probe: running native X11 client on $DISPLAY" >&2
if "$probe"; then
    status=0
else
    status=$?
fi

cleanup
trap - EXIT INT TERM HUP
exit "$status"
