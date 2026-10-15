# Third-party code

Code from other projects, kept here so the build needs no network for it.

| Folder | What | License | Source and pin | Local changes |
| --- | --- | --- | --- | --- |
| `smaa/` | SMAA 1x shaders and lookup tables | MIT, see `smaa/LICENSE.txt` | [iryoku/smaa](https://github.com/iryoku/smaa) at `71c806a838bdd7d517df19192a20f0c61b3ca29d` | Patched at build time, see below |
| `fxaa/` | NVIDIA FXAA 3.11 header | BSD 3-clause, see `fxaa/LICENSE` | Fork [lyntel/GraphicsSamples](https://github.com/lyntel/GraphicsSamples) at `3d30817ebeeade64fe6a4fc3aa1fe4265c29b6fd`, `samples/es3-kepler/FXAA/FXAA3_11.h` | Patched at build time, see below |
| `wlr-protocols/` | Wayland protocol XML | MIT-style permission notice inside each XML file (no separate LICENSE file) | Git submodule, [wlroots/wlr-protocols](https://gitlab.freedesktop.org/wlroots/wlr-protocols) | None. Clone with `--recurse-submodules` |

## Build-time patches

The engine reads these files at build time (`xmake.lua`, `on_load`) and embeds them as C strings. Each patch checks that its target text exists, and the build stops if the upstream file changed.

| File | Change | Reason |
| --- | --- | --- |
| `smaa/SMAA.hlsl` | `SMAATexture2D` takes `LweCombinedSampler2D` | The engine passes samplers as this type |
| `smaa/SMAA.hlsl` | `SMAASampleOffset` and `SMAASampleLevelZeroOffset` apply the offset in UV space | The shader compiler needs constant texture offsets; SMAA's offsets are runtime values |
| `fxaa/FXAA3_11.h` | `FxaaTex` is `LweCombinedSampler2D` | Same sampler type as above |

Files are copied byte for byte from the pinned upstream commits, so the patched text is always the upstream text plus these edits.
