#!/bin/sh

set -eu

if [ "$#" -eq 0 ]; then
    echo "usage: $0 command [args...]" >&2
    exit 2
fi

exec env -u NIXBENCH_TRACE_WAYLAND \
    -u NIXBENCH_TRACE_WAYLAND_LOG \
    -u NIXBENCH_APPLICATION \
    -u NIXBENCH_EXPECT_SUPERVISOR_TERM \
    -u NIXBENCH_EXPECT_CORE_FAILURE \
    -u NIXBENCH_VT_AWAY \
    -u NIXBENCH_GTK_MENU_BRIDGE \
    -u NIXBENCH_GTK_MENU_BRIDGE_DEBUG \
    -u NIXBENCH_BUILD_DIR \
    "$@"
