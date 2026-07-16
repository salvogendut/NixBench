#!/bin/sh

set -eu

sakura=/usr/pkg/bin/sakura

if [ "$#" -ne 0 ]; then
    echo "usage: $0" >&2
    exit 2
fi
if [ ! -f "$sakura" ] || [ ! -x "$sakura" ]; then
    echo "nixbench Sakura probe: $sakura is not an executable regular file" >&2
    exit 127
fi

profile_root=$(mktemp -d "${TMPDIR:-/tmp}/nixbench-sakura-profile.XXXXXX")
cleanup() {
    rm -rf "$profile_root"
}
trap cleanup EXIT INT TERM HUP

GTK_CSD=0
export GTK_CSD
XDG_CONFIG_HOME=$profile_root/config
XDG_CACHE_HOME=$profile_root/cache
XDG_DATA_HOME=$profile_root/data
XDG_STATE_HOME=$profile_root/state
mkdir -p "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$XDG_DATA_HOME" "$XDG_STATE_HOME"
export XDG_CONFIG_HOME XDG_CACHE_HOME XDG_DATA_HOME XDG_STATE_HOME

if [ "${NIXBENCH_TRACE_WAYLAND:-0}" = 1 ]; then
    WAYLAND_DEBUG=client
    export WAYLAND_DEBUG
    echo "NixBench Sakura probe: Wayland trace enabled" >&2
fi

echo "NixBench Sakura probe: GTK terminal, no extra arguments" >&2
if "$sakura"; then
    status=0
else
    status=$?
fi

cleanup
trap - EXIT INT TERM HUP
exit "$status"
