# This file is sourced by the GTK application probes.

nixbench_enable_gtk_menu_bridge()
{
    nixbench_bridge_build_dir=$1
    nixbench_bridge_caller=$2

    if [ "${NIXBENCH_GTK_MENU_BRIDGE:-0}" != 1 ]; then
        return 0
    fi

    nixbench_bridge_module=$nixbench_bridge_build_dir/gtk-modules/libnixbench_gtk_menu_bridge.so
    if [ ! -r "$nixbench_bridge_module" ]; then
        echo "$nixbench_bridge_caller: requested GTK menu bridge is missing: $nixbench_bridge_module" >&2
        return 1
    fi

    GTK3_MODULES=$nixbench_bridge_module${GTK3_MODULES:+:$GTK3_MODULES}
    export GTK3_MODULES
}
