#!/bin/sh

set -eu

midori=/usr/pkg/bin/midori
page='data:text/html,%3Chtml%3E%3Chead%3E'
page="${page}"'%3Ctitle%3ENixBench%20WebKit%20probe%3C%2Ftitle%3E'
page="${page}"'%3C%2Fhead%3E%3Cbody%20style%3D%22background%3A%23fff%3B'
page="${page}"'color%3A%23111%3Bfont-family%3Asans-serif%3Bfont-size%3A28px%3B'
page="${page}"'padding%3A24px%22%3E%3Ch1%3ENixBench%20WebKit%20probe%3C%2Fh1%3E'
page="${page}"'%3Cp%3EExisting%20GTK3%20and%20WebKit%20page%20content%20'
page="${page}"'rendered%20without%20X.org.%3C%2Fp%3E%3C%2Fbody%3E%3C%2Fhtml%3E'

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

echo "NixBench Midori content probe: fixed offline page, software compositing" >&2
exec "$midori" "$page"
