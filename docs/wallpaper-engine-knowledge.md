# Wallpaper Engine knowledge

Back to the [README](../README.md).

This page collects the facts about Wallpaper Engine that this project depends on: file formats, asset lookup, and behaviour that the code copies on purpose. Read it before you change a parser or a renderer. For what the install ships, see [Wallpaper Engine assets](wallpaper-engine-assets.md).

> [!NOTE]
> Facts here come from the code and from the comments next to it. Wallpaper Engine is closed source, so a few entries are observed behaviour, not documented behaviour.

## Terms

| Term | Meaning |
| --- | --- |
| Install | The Wallpaper Engine folder that holds `assets/`. Set it with `--assets-dir` or `engine_path` in `config.json`. |
| Project | A wallpaper folder with `project.json` and `scene.json`, or a single media file. |
| PKG | A package file that bundles many assets into one file. |
| TEX | A texture file. It can hold images or video. |
| MDL | A puppet model file: a mesh with an optional skeleton and animation clips. |
| Combo | A shader define chosen per material. |

## Asset lookup

Assets are resolved relative to a base path. The first match wins.

### Install (`EngineAssetProvider`)

| Try in order | Path |
| --- | --- |
| 1 | `<install>/assets/<rel>` |
| 2 | `<install>/assets/materials/<rel>` |
| 3 | `<install>/assets/presets/<preset>/<rel>` (only for `materials/presets/<preset>.json`) |
| 4 | `<install>/<rel>` |

Source: [`src/shared/assets/providers/asset_provider.cpp`](../src/shared/assets/providers/asset_provider.cpp).

### Wallpaper (`WallpaperAssetProvider`)

| Try in order | Path |
| --- | --- |
| 1 | `<wallpaper>/<rel>` |
| 2 | `<wallpaper>/materials/<rel>` |

The wallpaper folder is checked first, so a project can override an install asset with the same relative path.

## Package files (PKG)

- The magic is `PKGV`. Some packages have 4 unknown bytes before it, so the reader tries offset 0 and then offset 4.
- Strings are stored as a `u32` length followed by that many bytes, with no terminator.

Source: [`src/shared/assets/unpack.cpp`](../src/shared/assets/unpack.cpp), [`src/shared/core/vfs.cpp`](../src/shared/core/vfs.cpp).

## Textures (TEX)

A TEX file has three parts: a `TEXV` header, a `TEXI` info block, and a `TEXB` container with one or more mip levels.

### Header

| Field | Notes |
| --- | --- |
| `TEXV` | Version magic, then a null delimiter. |
| `TEXI0001` | Info magic. The reader skips 8 bytes. |
| `format_id` | Pixel format. See the table below. |
| `flags` | Bit `0x40` adds one extra `u32` (depth or 3D slice count) to the header and to each mip. |
| `image_width`, `image_height` | Size of the top mip. |
| `TEXB000x` | Container magic. `TEXB0003` adds one `u32`. `TEXB0004` adds two, and the second is a video marker. |

Source: [`src/shared/assets/tex_header.cpp`](../src/shared/assets/tex_header.cpp).

### Pixel formats

| `format_id` | Format | Pixel format used |
| --- | --- | --- |
| 0 | RGBA8 | RGBA8 |
| 1, 7 | DXT1 (BC1) | BC1 |
| 2, 6 | DXT3 (BC2) | BC2 |
| 4 | DXT5 (BC3) | BC3 |
| 8 | RG8 | RG8 |
| 9 | R8 (grayscale) | R8 |

Source: [`src/shared/assets/tex_decoder_internal.h`](../src/shared/assets/tex_decoder_internal.h), [`src/shared/assets/tex_format.cpp`](../src/shared/assets/tex_format.cpp).

### Video textures

A `TEXB0004` container with the video marker holds a video stream instead of a still image. Use the video path to decode it; see [features](features.md) for what works.

## Puppet models (MDL)

A puppet model is a mesh with optional bones and animation.

### Layout

```text
"MDLV00XX" | u8 pad | u32 type | u16 subversion | u16 flags | u32 reserved
| material path (null-terminated) | 28 zero bytes
| tag 0x0180000F | u32 vertexBytes | vertices | u32 indexBytes | u16 indices
| trailer | optional MDLS skeleton | optional MDAT | optional MDLA | optional MDLE
```

Layout confirmed against real Workshop assets. The reader cross-checks `vertexBytes % 80` and `indexBytes % 6`.

### Vertex stride

The stride and the bone and weight offsets depend on the model version and on whether the model is skinned. Weights sum to 1.

| Stride | Model | Offsets (bytes) |
| --- | --- | --- |
| 80 | v0017 and later, skinned | normal +12, tangent +24/+36, 4 `u32` bone indices +40, 4 `f32` weights +56, uv +72 |
| 84 | some v0023, skinned | as stride 80 with one extra word: bones +44, weights +60, uv +76 |
| 52 | v0013, skinned | 4 `u32` bone indices +12, 4 `f32` weights +28, uv +44 |
| 48 | unskinned | normal +12, tangent +24/+36, uv +40, no bones |

Every vertex starts with its position (3 `f32` at +0).

### Skeleton and clips

| Block | Contents |
| --- | --- |
| MDLS | One entry per bone: `u32` type, `u32` parent (`0xFFFFFFFF` = root), `u32` payload bytes (64 = bind matrix), row-major 4x4 bind matrix, info JSON string, name string. |
| MDLA | `fps`, `frameCount`, then one track per bone. Each track has `frameCount + 1` keyframes of 36 bytes: translation xyz, rotation xyz in radians, scale xyz. |

Source: [`src/wallpaper/2d/puppet/mdl_parser.cpp`](../src/wallpaper/2d/puppet/mdl_parser.cpp), [`src/wallpaper/2d/puppet/mdl_parser.h`](../src/wallpaper/2d/puppet/mdl_parser.h).

## Scene transforms

- A scene node's local transform is applied as `T * Rz * Ry * Rx * S`: translate, then rotate Z, Y, X, then scale.
- Angles are in degrees.

Source: [`src/wallpaper/2d/tree/scene_tree.cpp`](../src/wallpaper/2d/tree/scene_tree.cpp).

## Particles

| Behaviour | Detail |
| --- | --- |
| Initializer defaults | A bound left out of an initializer takes Wallpaper Engine's default, not zero: `lifetimerandom` max 1, `sizerandom` max 20, `alpharandom` min 0.05 and max 1. |
| Colour at spawn | Override colours are converted to linear space at spawn: each channel is squared after dividing by 255 when the value is legacy. |
| Follow children | When the parent ends, the follow child's particles are cleared. Spawn and death children expire on their own. |
| Random draws | Initializer draws are `pow(random, exponent)` between the bounds. Exponent 1 is a plain uniform draw. |

Sources: [`src/wallpaper/2d/layers/particle/particle_parser.cpp`](../src/wallpaper/2d/layers/particle/particle_parser.cpp), [`src/wallpaper/2d/layers/particle/particle_spawner.cpp`](../src/wallpaper/2d/layers/particle/particle_spawner.cpp), [`src/wallpaper/2d/layers/particle/particle_simulation.cpp`](../src/wallpaper/2d/layers/particle/particle_simulation.cpp).

## Text

- Point sizes are authored in design units at a fixed ratio of 4 design units per point.
- Without a width limit, lines break only at explicit newlines.
- Glyphs are rendered at up to 2x supersampling, with a 4096 px cap per texture.
- Characters missing from the authored font fall back to system fonts.

Source: [`src/wallpaper/2d/layers/text/text_raster.cpp`](../src/wallpaper/2d/layers/text/text_raster.cpp).

## Transitions

Playlist transitions use the `FADEEFFECT` combo values 0 to 26:

| Value | Effect | Value | Effect |
| --- | --- | --- | --- |
| 0 | Fade | 14 | Drip |
| 1 | Mosaic | 15 | Pixelate |
| 2 | Diffuse | 16 | Bricks |
| 3 | Horizontal slide | 17 | Paint |
| 4 | Vertical slide | 18 | Fade to black |
| 5 | Horizontal fade | 19 | Twister |
| 6 | Vertical fade | 20 | Black hole |
| 7 | Clouds | 21 | CRT |
| 8 | Burnt paper | 22 | Radial wipe |
| 9 | Circular | 23 | Glass shatter |
| 10 | Zipper | 24 | Bullets |
| 11 | Door | 25 | Ice |
| 12 | Lines | 26 | Boilover |
| 13 | Zoom | | |

Source: [`src/wallpaper/transition/transition_catalog.h`](../src/wallpaper/transition/transition_catalog.h). The random pick for a seed always returns the same effect.

## Shaders

- Some materials use DX11 HLSL as a fallback. The HLSL path compiles it to SPIR-V and needs no source rewriting.

Source: [`src/shared/graphics/shader/shader_slang_compile_hlsl.cpp`](../src/shared/graphics/shader/shader_slang_compile_hlsl.cpp).

## SceneScript

- SceneScript runs as an ES module on the shared script engine.
- A number assigned to a vector property is copied to every component. This matches Wallpaper Engine.
- An empty object handle (id 0) reads as `undefined` and does nothing when called. This matches the animation object of a layer that has none.
- Bindings added by a running script take effect after the update loop finishes.
- The UI language reports as `en-us` style tags, taken from the locale.

Sources: [`src/wallpaper/2d/script/scene_script.h`](../src/wallpaper/2d/script/scene_script.h), [`src/wallpaper/2d/script/script_value_js.cpp`](../src/wallpaper/2d/script/script_value_js.cpp), [`src/wallpaper/2d/script/script_engine_prelude.cpp`](../src/wallpaper/2d/script/script_engine_prelude.cpp), [`src/wallpaper/2d/script/script_bindings.cpp`](../src/wallpaper/2d/script/script_bindings.cpp).

## User properties

- `--list-properties` output follows the upstream layout: properties are sorted by key.
- A label property has no value of its own. Upstream shows its text.
- Scene settings that a script can read are named as SceneScript sees them, such as `bloomstrength` and `clearcolor`. Booleans are 0 or 1.

Sources: [`src/wallpaper/property_listing.cpp`](../src/wallpaper/property_listing.cpp), [`src/wallpaper/2d/script/script_scene_backend.h`](../src/wallpaper/2d/script/script_scene_backend.h).

## Web wallpapers

- The Qt WebEngine view is created with the Wallpaper Engine shim installed.
- A web frame can arrive as a DMA-BUF through the helper process. The metadata goes over the control socket and the file descriptors go with `SCM_RIGHTS`. See the [IPC header](../src/wallpaper/web/web_ipc.h).

Source: [`src/wallpaper/web/web_quick_view.h`](../src/wallpaper/web/web_quick_view.h).
