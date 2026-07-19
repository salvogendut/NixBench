# Building and hosted development

[Back to the main README](../README.md)

Required development dependencies:

- A C11 compiler
- CMake 3.16 or newer and a supported build tool
- SDL 3.2.0 or newer, including its development files
- libpng (PNG wallpaper decoding) and zlib (PNG screenshot encoding)
- A video backend supported by SDL3; Xorg is the initial NetBSD host

The compositor path additionally uses the Wayland server library and scanner,
libxkbcommon, and the stable `xdg-shell.xml` from `wayland-protocols`. NixClock
and the separate demo also require the Wayland client development library.
Configuration defaults to `-DNIXBENCH_WAYLAND=AUTO`: it enables the feature
when all server components are found and otherwise builds the SDL-only desktop.
Use `-DNIXBENCH_WAYLAND=ON` to require it, or `OFF` to omit it explicitly.
`NIXBENCH_BUILD_EXAMPLES` defaults to `ON`; use
`-DNIXBENCH_BUILD_EXAMPLES=OFF` to omit experimental clients. NetBSD's pkgsrc
`wayland`, `libxkbcommon`, and `wayland-protocols` packages provide these
components; no `pkg-config` executable is required by this build.
`NIXBENCH_BUILD_APPLICATIONS` also defaults to `ON`; set it to `OFF` to omit
NixClock. Application targets are built only when the Wayland client and
compositor dependencies are available.

The experimental HTML decoration renderer is controlled by
`-DNIXBENCH_HTML_THEMES=AUTO|ON|OFF`. `AUTO`, the default, enables its targets
only when Wayland client support, `pkg-config`, `gdk-wayland-3.0`, and either
`webkit2gtk-4.1` or `webkit2gtk-4.0` are present. `ON` makes missing renderer
dependencies a configuration error; `OFF` excludes the browser renderer
explicitly. The core theme catalog and native Classic fallback do not depend
on WebKitGTK. See [HTML desktop themes](html-themes.md) for the process and
safety boundaries.

The optional rootless Xwayland XWM is enabled when the Wayland compositor and
the XCB, XCB Composite, and XCB XFixes development libraries are available.
NetBSD normally supplies these under `/usr/X11R7`; CMake searches that base-X11
layout directly. XFixes is used to observe `CLIPBOARD` and `PRIMARY` owner
changes without polling.

Detailed NetBSD DRM/KMS inventory is an optional libdrm feature controlled by
`-DNIXBENCH_LIBDRM=AUTO`, the default. `AUTO` enables it when `xf86drm.h`,
`xf86drmMode.h`, `libdrm/drm.h`, and the libdrm library are found, and otherwise
keeps the path-only probe available. `ON` makes any missing libdrm component a
configuration error; `OFF` omits all libdrm calls explicitly. Discovery checks
normal CMake search locations and NetBSD's base-system `/usr/X11R7/include`,
`/usr/X11R7/include/libdrm`, and `/usr/X11R7/lib` layout directly, so it does
not require `pkg-config`.

The privilege-separated NetBSD session is also excluded by default. Enabling
`-DNIXBENCH_BUILD_WSDISPLAY_SESSION=ON` requires Wayland server support,
Wayland client development files, and `NIXBENCH_BUILD_APPLICATIONS=ON`. It
builds:

- `nixbench-wsdisplay-session`, the root recovery supervisor and device-helper
  launcher;
- `nixbench-session-core`, its internal ordinary-user desktop process; and
- `nixclock`, a native application available from the desktop launcher.

`nixbench-wsdisplay-smoke` is the older, explicitly opt-in root hardware
harness and is not built by default. Enable it separately with
`-DNIXBENCH_BUILD_WSDISPLAY_SMOKE=ON`. Neither option makes `wsdisplay` a
supported production runtime.

SDL3 is available from NetBSD pkgsrc as `devel/SDL3`; pkgsrc's `png` and
`zlib` packages provide the image dependencies. Configure, build, and test with:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Inspect the backends available on the current machine with:

```sh
./build/nixbench-backend-probe
```

The probe lists SDL video drivers and, on NetBSD, checks the default
`wsdisplay`, wscons input, and DRM device paths. It opens `wsdisplay` read-only
only long enough to query its type, current mode, and framebuffer layout. When
libdrm support was built, it also tries each DRM primary node read-write and
then read-only, and reports the live driver version, dumb-buffer capabilities,
KMS resources, CRTC state, cached connectors and modes, and legacy-visible
planes. It uses the cached connector query so the diagnostic does not request a
fresh display probe, and it does not enable additional DRM client capabilities.

The command never explicitly requests DRM master, changes a display mode, maps
device memory, allocates a DRM buffer, issues a page flip, or consumes input
events. Opening an idle primary DRM node may nevertheless grant DRM master
implicitly. The probe detects that state and drops it before collecting the
driver and KMS inventory; it aborts all further queries for that card if it
cannot establish the master state or drop an implicitly granted master.
On current NetBSD releases, dropping DRM master is privileged, so an
unprivileged probe of an otherwise idle, working primary node will abort safely
at this point. Use a controlled privileged diagnostic session when a full
idle-console inventory is needed; the probe never treats the failed partial
result as usable.

A card is reported as a direct KMS candidate only when the detailed query is
available, the card opens read-write, the master-safety check succeeds, any
implicit grant is dropped, a live driver version and KMS resources can be read,
at least one CRTC and encoder exist, dumb buffers are supported, and at least
one connected connector has a cached mode. This is a conservative preflight,
not proof that modesetting or ordinary page flips will work. Alternate device
paths can be supplied with `--wsdisplay`, `--keyboard`, `--mouse`, and
`--drm-directory`; use `--help` for details.

## Hosted desktop

Open the desktop in a development window:

```sh
./build/nixbench
```

Windowed operation is the development default. NixInfo opens at startup. Use
Project > New Window to create another application window, Refresh to update
the focused window's snapshot, View to show or hide its full kernel version,
and Window to close that window or its siblings. About NixInfo is a singleton,
and Quit NixInfo exits the application without exiting the desktop. The
NixBench desktop menu can start it again.

Click an internal window to focus it and bring it to the front, drag it by its
title bar, and use its close gadget to close it. Resize it using the gadget
at the right end of its bottom decorator rail. The global top bar switches
between the focused application's menus and the NixBench desktop menu. Click a
menu and then an item, or press and drag directly to an item. F10 opens the
keyboard menu path when the desktop or an internal NixBench window owns
keyboard focus; a focused Wayland or Xwayland client receives F10 itself. Use
the arrow keys, Enter, and Escape to navigate or dismiss an open menu. Escape
exits NixBench when no menu is open and no Wayland client owns keyboard focus.
The right end of the bar shows local time. Clicking the desktop clears the
active window. Pass `--fullscreen` only for a hosted full-display preview.
Close the outer host window to exit NixBench. Use `--help` to list all current
options.

The CMake configuration deliberately uses the system SDL3 package instead of
downloading dependencies during the build. Direct X11 dependencies are not
needed for the hosted-window phase.

When Wayland support is enabled, NixBench publishes a nested display beneath
`XDG_RUNTIME_DIR` and logs its `WAYLAND_DISPLAY` name. A development shell that
does not set a runtime directory can prepare one before starting NixBench:

```sh
install -d -m 700 "$HOME/.nixbench-runtime"
XDG_RUNTIME_DIR="$HOME/.nixbench-runtime" ./build/nixbench
```

The initial keymap follows libxkbcommon's `XKB_DEFAULT_*` environment. For
example, start NixBench with `XKB_DEFAULT_LAYOUT=it` to publish an Italian
layout to clients. NixBench does not yet import the active host Xorg layout
automatically; a desktop keyboard setting is planned.

In another terminal, use the display name logged by NixBench (normally
`wayland-0`) to start NixClock:

```sh
XDG_RUNTIME_DIR="$HOME/.nixbench-runtime" \
WAYLAND_DISPLAY=wayland-0 \
./build/nixclock
```

Focus the clock to place its **NixClock** and **Settings** menus in the global
bar. Choose **Settings > Show seconds** to toggle the colored seconds hand; the
item's check mark follows the setting. Choose **NixClock > Quit** to terminate
only the clock application. Pass `--show-seconds` to enable that hand at
startup. NixClock currently connects only to NixBench because its required
global-menu extension is deliberately still private and experimental.

The earlier separate protocol probe can be started in the same way:

```sh
XDG_RUNTIME_DIR="$HOME/.nixbench-runtime" \
WAYLAND_DISPLAY=wayland-0 \
./build/nixbench-wayland-demo
```

Clicking the centered control, or pressing Space or Enter while it is focused,
toggles its color and indicator. Escape closes the demo. The active client
window also supplies the temporary Application > Close Application menu in
NixBench's global bar. Pass `--exit-after-first-frame` to the demo for a
noninteractive rendering smoke run. Toolkit compatibility still requires the
additional protocol layers listed above.
