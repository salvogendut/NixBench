# pkgsrc patches used by NixBench

These patches keep local NetBSD package builds reproducible while their
changes are evaluated for submission upstream.

## Thunar without XFCE panel plugins

`xfce4-thunar-optional-panel-plugins.patch` adds a `panel-plugins` pkgsrc
option. The option is enabled by default, so the package's existing behavior
does not change. A standalone NixBench host can disable it together with GVfs:

```sh
cd /usr/pkgsrc
sudo patch -p1 < /path/to/NixBench/patches/pkgsrc/xfce4-thunar-optional-panel-plugins.patch
cd sysutils/xfce4-thunar
sudo make clean
sudo env MAKE_JOBS=2 make \
  'PKG_OPTIONS.xfce4-thunar=-gvfs -panel-plugins' package-install
```

This builds the GTK3/Wayland-capable Thunar file manager while omitting its
unneeded XFCE trash-panel and wallpaper plugins. It avoids pulling the entire
XFCE panel, Vala, Graphviz, and Rust toolchain into a NixBench-only system.
Disabling GVfs removes network-location and enhanced volume-management
support; normal local file operations, menus, context actions, and
drag-and-drop remain available.
