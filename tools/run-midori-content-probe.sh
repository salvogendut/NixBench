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

WEBKIT_DISABLE_COMPOSITING_MODE=1
export WEBKIT_DISABLE_COMPOSITING_MODE

if [ "${NIXBENCH_TRACE_WAYLAND:-0}" = 1 ]; then
    WAYLAND_DEBUG=client
    export WAYLAND_DEBUG
    trace_log=${NIXBENCH_TRACE_WAYLAND_LOG:-${TMPDIR:-/tmp}/nixbench-midori-wayland-$$.log}
    echo "NixBench Midori content probe: writing Wayland trace to $trace_log" >&2
    exec "$midori" "$page" 2>"$trace_log"
fi

echo "NixBench Midori content probe: fixed offline page, software compositing" >&2
exec "$midori" "$page"
