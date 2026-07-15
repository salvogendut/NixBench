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

WEBKIT_DISABLE_COMPOSITING_MODE=1
export WEBKIT_DISABLE_COMPOSITING_MODE

if [ "${NIXBENCH_TRACE_WAYLAND:-0}" = 1 ]; then
    WAYLAND_DEBUG=client
    export WAYLAND_DEBUG
fi

echo "NixBench Midori diagnostic: software compositing under ordinary-user GDB" >&2
exec "$gdb" --quiet --nx --batch \
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
    --args "$midori"
