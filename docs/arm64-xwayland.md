# ARM64 NetBSD Xwayland build notes

[Back to the main README](../README.md)

This note records the ARM64 NetBSD 10.1 Xwayland build used with NixBench. The
native-X11 Present protocol mismatch must be corrected first. On this host the
reliable result is then a software-only Xwayland build: loading a newer modular
libdrm beside NetBSD's base graphics stack produced an unusable accelerated
server.

## Why X11 applications report `can't open display`

NixBench starts rootless Xwayland only when it can discover an executable named
`Xwayland`. If neither `/usr/pkg/bin/Xwayland` nor another configured path is
available, a standalone session correctly leaves `NIXBENCH_XWAYLAND` empty.
Native Wayland applications still work, but X11 clients such as `xclock` have
no `DISPLAY` and report that they cannot open one.

Check the executable before testing an X11 client:

```sh
command -v Xwayland
test -x /usr/pkg/bin/Xwayland && /usr/pkg/bin/Xwayland -version
```

After Xwayland is installed, restart the NixBench session. Its rootless bridge
creates and exports the private `DISPLAY` to applications launched from the
desktop, including terminals such as Sakura.

## Prefer binary dependencies

On the ARM64 host, `pkgin` supplied all substantial Xwayland build
dependencies. Installing them first avoids rebuilding Meson, gettext, Wayland,
and related libraries from source:

```sh
sudo pkgin install meson gettext-tools freetype2 libei libxcvt \
    wayland wayland-protocols xkbcomp
```

Use `sudo pkgin search NAME` when a pkgsrc directory name is rejected by
`pkgin`; pkgsrc package names and repository package names are not always the
same. In this validation, `input-headers` was not available as a binary
package. It is a small header-only package and can be installed from pkgsrc:

```sh
cd /usr/pkgsrc/devel/input-headers
sudo nice -n 19 make MAKE_JOBS=1 install
```

`xorgproto-2025.1` is similarly a small header package. It supplies
`presentproto` 1.4, required by Xwayland 24.1.12:

```sh
cd /usr/pkgsrc/x11/xorgproto
sudo nice -n 19 make MAKE_JOBS=1 install
```

## Thermal-safe source build

The ARM64 machine used for this work can overheat during sustained builds. Keep
the Xwayland build deliberately serial and low priority:

```sh
cd /usr/pkgsrc/wayland/xwayland
sudo nice -n 19 make MAKE_JOBS=1 install
```

Do not run NixBench, browsers, emulators, or other CPU-heavy work on that host
at the same time. A power loss or forced shutdown can leave Meson's build
directory locked. Once no Meson, Ninja, or pkgsrc Xwayland process remains,
remove only generated pkgsrc state and retry:

```sh
cd /usr/pkgsrc/wayland/xwayland
sudo make clean
sudo nice -n 19 make MAKE_JOBS=1 install
```

`make clean` affects the port's generated `work/` directory, not the NixBench
source checkout or a user's applications.

## Native-X11 Present protocol buildlink

NetBSD 10.1's base X11 installation supplies `presentproto` 1.2. Xwayland
24.1.12 requires version 1.4 or newer. Although installing pkgsrc
`xorgproto-2025.1` provides `/usr/pkg/lib/pkgconfig/presentproto.pc` at version
1.4, the current Xwayland pkgsrc buildlink directory can still select
`/usr/X11R7` through `x11-links` and report version 1.2 during Meson
configuration.

Useful diagnostics are:

```sh
/usr/pkg/bin/pkg-config --modversion presentproto
grep -n . /usr/pkg/lib/pkgconfig/presentproto.pc
readlink /usr/pkgsrc/wayland/xwayland/work/.buildlink/lib/pkgconfig/presentproto.pc
```

The first two commands should report 1.4 after `xorgproto` is installed. If the
last command resolves to `/usr/pkg/share/x11-links/...`, Meson may still see
the base-system 1.2 metadata. On this host, the local Xwayland port needed a
stricter buildlink requirement before its existing `xorgproto` include:

```make
BUILDLINK_API_DEPENDS.xorgproto+= xorgproto>=2025.1
```

After a `sudo make clean`, the generated link then resolved to
`/usr/pkg/lib/pkgconfig/presentproto.pc` and Meson accepted Present 1.4. This
is a local pkgsrc-port adjustment that should be proposed upstream separately;
it is not a NixBench source change.

## Why the accelerated build did not work

With Glamor enabled, Xwayland 24.1.12 requires `libdrm >= 2.4.116`. NetBSD 10.1
base provides 2.4.109. Building pkgsrc `x11/libdrm` in modular mode satisfies
Meson's version check, but it does not make the rest of NetBSD's base graphics
stack modular.

The resulting Xwayland process on the ARM64 host loaded both ABIs: pkgsrc
`/usr/pkg/lib/libdrm.so.2` directly and base `/usr/X11R7/lib/libdrm.so.3`
through GBM/EGL. Xwayland accepted X11 windows, but never created or associated
their Wayland surfaces, so clients such as `xclock` remained invisible without
reporting an X11 error. Check for this mixture with:

```sh
ldd /usr/pkg/bin/Xwayland | grep -E 'libdrm|libgbm|libEGL'
```

Do not continue using an Xwayland binary that loads both libdrm ABIs.

## Software-only Xwayland fallback

NixBench currently presents rootless X11 windows through shared-memory Wayland
buffers, so the framebuffer session does not require Xwayland's Glamor or DRI3
paths. For the validated ARM64 fallback, add these Meson arguments to the
NetBSD section of `/usr/pkgsrc/wayland/xwayland/Makefile`:

```make
MESON_ARGS+=	-Dglamor=false -Ddri3=false
```

For this software-only build, do not expose the newer modular libdrm through
the port's buildlink section. Locally comment out both its API requirement and
buildlink include:

```make
# BUILDLINK_API_DEPENDS.libdrm+=	libdrm>=2.4.116
# .include "../../x11/libdrm/buildlink3.mk"
```

Keep the `xorgproto>=2025.1` override described above. Then remove the old
generated build state and replace the installed Xwayland package:

```sh
cd /usr/pkgsrc/wayland/xwayland
sudo make clean
sudo nice -n 19 make MAKE_JOBS=1 replace
```

Meson's summary must report both `glamor: false` and `dri3: false`. Its log may
say that base libdrm 2.4.109 is too old and mark the dependency as not found;
that is expected for this fallback. After installation, `ldd` must not show
the modular `/usr/pkg/lib/libdrm.so.2`.

## Required Xwayland 24.1.12 portability fixes

The software-only build exposed two independent Xwayland/Xserver portability
problems on this host. The exact upstream-ready diffs and reproduction record
are preserved under
[`patches/xwayland-24.1.12`](../patches/xwayland-24.1.12/README.md):

- late manual Composite redirection could enter `miValidateTree()` with the
  parent window's `valdata` unset and crash Xwayland;
- the filesystem backing `XDG_RUNTIME_DIR` returned `EOPNOTSUPP` from
  `posix_fallocate()`, which Xwayland converted into an X11 `BadAlloc` instead
  of falling back to `ftruncate()`.

Apply both patches to Xwayland 24.1.12 before building. These are candidate
upstream fixes and are deliberately kept outside NixBench's source. For a
local pkgsrc port, place equivalent pkgsrc-format patches under
`/usr/pkgsrc/wayland/xwayland/patches`, update `distinfo` with
`sudo make makepatchsum`, and rebuild the package.

## NixBench rootless redirection workaround

The ARM64 Xwayland server did not apply a root-wide manual Composite redirect
to top-levels created later. NixBench therefore redirects each rootless X11
top-level individually, immediately after mapping it, and releases the
redirect when the window unmaps. The ordering matters: redirecting the window
before it is mapped did not cause Xwayland to create a Wayland surface.

This behavior lives in NixBench and is separate from the two Xwayland patches.
It remains compatible with the existing x86_64 rootless path while avoiding a
server-specific assumption about redirecting future root children.

## Validation

Start a fresh NixBench session after installation because the rootless
Xwayland service and `DISPLAY` belong to one desktop session. Launch an X11
client from Sakura:

```sh
xclock &
```

For a diagnostic run with `NIXBENCH_TRACE_WAYLAND=1`, a successful association
contains all three messages below:

```text
Rootless XWM accepted map request for X window 0x40000c (xclock)
Rootless XWM received WL_SURFACE_SERIAL 1 for X window 0x40000c
Rootless XWM associated X window 0x40000c with Wayland surface serial 1
```

The ARM64 validation kept `xclock` running and visible, then terminated the
NixBench supervisor normally. Console mode, video state, VT mode and active VT
were restored, and `/var/run/nixbench-wsdisplay-session.state` was removed.
