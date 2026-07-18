# Xwayland 24.1.12 NetBSD portability patches

These patches were developed while bringing up NixBench rootless Xwayland on
NetBSD 10.1/arm64. They are kept separate from the NixBench workaround so they
can be reviewed and submitted to Xwayland/Xserver upstream.

They have been validated together against Xwayland 24.1.12, configured with
`-Dglamor=false -Ddri3=false`, using NixBench's shared-memory Wayland renderer.
They are candidate upstream fixes; this repository does not claim that they
have been accepted upstream.

## Patches

1. `0001-composite-mark-parent-before-validating-late-redirect.patch`
   prevents a null `parent->valdata` dereference in `miValidateTree()` when a
   rootless window manager redirects an already-mapped top-level window.
2. `0002-xwayland-shm-fallback-when-posix-fallocate-is-unsupported.patch`
   lets Xwayland allocate its shared-memory backing file on a filesystem whose
   `posix_fallocate()` returns `EOPNOTSUPP`, by falling back to `ftruncate()`.
3. `0003-xwayland-register-damage-for-late-surface.patch` restores the
   XDamage tracker when per-window Composite redirection creates a rootless
   Wayland surface after the X window has already been realized. Without it,
   pre-painted GTK2 windows such as PCManFM remain mapped but bufferless.

Apply them to an unpacked Xwayland 24.1.12 tree:

```sh
patch -p1 < 0001-composite-mark-parent-before-validating-late-redirect.patch
patch -p1 < 0002-xwayland-shm-fallback-when-posix-fallocate-is-unsupported.patch
patch -p1 < 0003-xwayland-register-damage-for-late-surface.patch
```

## Reproduction evidence

NixBench first maps an X11 top-level and then requests manual Composite
redirection for that window. Without patch 1, Xwayland aborts in this path:

```text
miValidateTree
compHandleMarkedWindows
compRedirectWindow
ProcCompositeRedirectWindow
Dispatch
dix_main
```

The faulting `miValidateTree()` instruction dereferenced the parent window's
null `valdata` pointer. After patch 1, the Composite request returned X11
`BadAlloc` instead of crashing. A direct probe then showed the allocation
failure behind it:

```text
posix_fallocate=45 (Operation not supported)
```

Xwayland's `os_create_anonymous_file()` treated that result as fatal. Patch 2
uses the same `ftruncate()` allocation already used when
`HAVE_POSIX_FALLOCATE` is absent.

With both patches, Xwayland created an `xwayland_shell_v1` surface and NixBench
completed the association:

```text
Rootless XWM accepted map request for X window 0x40000c (xclock)
Rootless XWM received WL_SURFACE_SERIAL 1 for X window 0x40000c
Rootless XWM associated X window 0x40000c with Wayland surface serial 1
```

The `xclock` process remained running and visible. A clean shutdown restored
the wsdisplay console and removed the NixBench recovery record.

PCManFM exposed a second late-redirection failure. Its 640x480 X top-level was
viewable and associated with an `xwayland_shell_v1` surface, and Xwayland
allocated matching SHM buffers, but no `wl_surface.attach` followed. PCManFM
paints before mapping, unlike xclock. The late `SetWindowPixmap` surface path
had no Damage object, so neither the existing frame nor later explicit X11
drawing could reach `xwl_window_post_damage()`. Patch 3 registers that missing
tracker on the next block-handler pass, outside the re-entrant pixmap callback,
and seeds full-window damage. Normal realize-time surface creation is
unchanged.
