# HTML desktop themes

[Back to the main README](../README.md)

## Selected architecture

NixBench uses the "HTML only for decoration" model. Applications continue to
submit ordinary native Wayland, Xwayland, or SDL content. A browser engine is
used only to paint the desktop shell around that content:

- the desktop and wallpaper layer;
- window borders, title bars, controls, shadows, and resize affordances;
- the global bar and application menus; and
- optional transition and state animations.

Theme files are standard HTML5, CSS, and SVG. NixBench does not translate HTML
elements into native widgets, and applications do not become web applications.
The first renderer target is WebKitGTK because that engine is already exercised
by GTK applications on the NetBSD standalone compositor.

```text
ordinary-user processes

  native application surface       HTML theme renderer
  (Wayland, Xwayland, SDL)          (WebKitGTK)
             |                            |
             +------------+---------------+
                          |
                 NixBench desktop core
          policy, geometry, focus, stacking, input,
          clipping, damage, and final composition
                          |
             privileged wsdisplay helper
          device access and frame presentation only
```

The browser and its content processes always run with the ordinary user's
credentials. The privileged console helper never parses HTML, CSS, SVG, theme
manifests, or theme assets.

## Renderer atlas

One browser document maintains a packed, transparent render atlas. Each
desktop component has a named rectangular tile in that atlas:

```text
+----------------------+----------------------+
| window:7 decoration  | window:12 decoration|
+----------------------+----------------------+
| global bar and menus | desktop layer        |
+----------------------+----------------------+
```

The compositor copies or samples tiles independently. It can therefore draw,
for every window from bottom to top, that window's decoration followed by its
native client content. A single full-screen HTML overlay would not work: the
decoration of a lower window could incorrectly cover the client content of a
higher window.

The renderer surface is marked as a private chrome-atlas surface and is never
managed as an application window. The core sends immutable window state and
geometry to the renderer. The renderer returns a committed atlas generation
and hit regions associated with stable action identifiers.

The first state vocabulary is:

- window identifier, title, size, scale, and active state;
- maximized, minimized, urgent, and fullscreen state;
- visibility and placement of minimize, maximize, and close controls;
- global-menu labels, item states, and open-menu state; and
- pointer hover and pressed action identifiers.

The compositor remains authoritative. HTML cannot move, resize, focus, close,
minimize, or maximize a window directly. Elements identify intent with
attributes such as `data-nixbench-action="close"`; the trusted renderer bridge
reports the element's bounds and action identifier, and the core applies the
normal window policy after its own hit test.

## Bundle format

Installed themes live below `${CMAKE_INSTALL_DATADIR}/nixbench/themes`.
Per-user themes will be discovered below
`~/.local/share/nixbench/themes`. A bundle contains:

```text
Fantasy/
|-- theme.conf
|-- desktop.html
|-- window.html
|-- menubar.html
|-- theme.css
`-- assets/
    `-- original-theme-assets.svg
```

`theme.conf` is a small, non-executable manifest. Format 1 declares the stable
theme identifier, display name, entry documents, stylesheet, and requested
capabilities. Entry documents are HTML fragments loaded into the renderer's
trusted atlas document rather than independent browsing contexts.

The repository begins with three personalities:

- **Fantasy**, an original expressive theme intended to exercise SVG,
  gradients, irregular decoration silhouettes, and animation;
- **Motif**, a restrained original recreation of the visual conventions of
  1990s Unix workstations; and
- **BeOS-inspired**, an original theme with compact offset title tabs and warm
  yellow controls.

Names describe the inspiration. Bundles must use original or compatibly
licensed artwork and may not copy proprietary system assets.

## Safety policy

Downloadable themes are untrusted presentation data. The initial renderer
enforces all of the following:

- theme-supplied JavaScript is disabled;
- HTTP, HTTPS, WebSocket, and other network requests are denied;
- cookies, persistent web storage, service workers, forms, navigation, media
  capture, downloads, and browser plug-ins are disabled;
- only files inside the selected, canonicalized theme directory may be read;
- document, decoded-image, SVG, atlas-dimension, memory, and animation-rate
  limits are enforced; and
- renderer failure or timeout preserves the last good pixels briefly and then
  falls back to the built-in native Classic renderer.

NixBench may inject its own fixed bridge script to update state, collect
`data-nixbench-action` rectangles, and observe animation damage. That script is
part of NixBench, not the theme, and exposes no general system API.

## Performance rules

Static tiles are cached and rendered only when their state, size, assets, or
CSS animation changes. Animations are paused for minimized and fully obscured
windows and can be rate-limited independently of pointer and client-surface
updates. The existing desktop damage path remains responsible for copying only
changed output regions to `wsdisplay`.

The browser renderer is asynchronous. Window movement, input routing, VT
switching, recovery, and application presentation must never wait for an HTML
frame. A late renderer frame is skipped rather than stalling the desktop.

## Delivery milestones

1. Install and discover validated bundles while continuing to draw Classic
   native decorations.
2. Add the private renderer-atlas state and buffer protocol.
3. Render one title-bar tile through WebKitGTK behind an explicit experimental
   option, with native hit testing and Classic fallback.
4. Render complete per-window frames and controls, then the global bar and
   menus.
5. Add HTML desktop/backdrop rendering and Settings theme selection.
6. Enable bounded CSS transitions and SVG animation after idle and animated
   CPU measurements on amd64 and arm64 NetBSD hardware.

The first hardware gate requires unchanged behavior for native NixClock,
GTK/Wayland applications, SDL/Wayland applications, and Xwayland windows. A
theme-renderer crash must not terminate the desktop or compromise console
restoration.

## Renderer preview

When WebKitGTK support is detected, the build includes the experimental
`nixbench-html-theme-renderer`. Before atlas integration is enabled it can
render any validated component in a normal preview window or save a transparent
PNG through WebKitGTK's snapshot API:

```sh
./build/nixbench-html-theme-renderer \
  --theme ./themes/Fantasy \
  --component window \
  --size 640x96

./build/nixbench-html-theme-renderer \
  --theme ./themes/Motif \
  --component window \
  --size 640x96 \
  --snapshot /tmp/nixbench-motif-title.png
```

This preview is a renderer bring-up tool, not an application window or the
final compositor integration path.
