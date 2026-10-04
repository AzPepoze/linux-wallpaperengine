# Wallpaper Engine assets that can be used

Linux Wallpaper Engine does not re-create the content that ships with the Windows application. It reads it from the user's own Wallpaper Engine installation (`<install>` below; pass it with `--assets-dir`, or set `engine_path` in `config.json`). Nothing from the install is copied into this repository.

Before implementing something, check this page: if the install already ships it (shaders, effects, materials, presets, fonts, JavaScript classes...), load it instead of rewriting it.

Counts below are from Wallpaper Engine 2.8.x and are only meant to show scale.

## Installation layout

| Path | What it is | Used | Notes |
| --- | --- | --- | --- |
| `<install>/assets/` | Content shared by every wallpaper: shaders, effects, materials, particles, presets, fonts, models, scripts, scenes, compatibility data | Yes | Detailed in the next table |
| `<install>/bin/` | Windows executables and DLLs (renderer, `scenescript64.dll`, FreeImage, assimp, CEF...) | No | Closed-source and Windows-only. `scenescript64.dll` is the real SceneScript runtime; the JavaScript it exposes is documented, so the engine hosts QuickJS itself |
| `<install>/projects/` | `defaultprojects` (sample wallpapers), `templates`, `myprojects` | No | The default projects are handy extra test content |
| `<install>/plugins/` | Hardware (LED) plugins | No | RGB integration is not supported |
| `<install>/ui/` | Editor and launcher web UI | No | Contains editor typings for the JavaScript standard library only; there are no SceneScript typings to reuse |
| `<install>/locale/` | UI translations | No | |
| `<install>/config.json`, `version.json`, `log.txt` | Settings, version, log | No | |

## `assets/` folders

| Folder | Files | Content | Used | How it is used |
| --- | --- | --- | --- | --- |
| `effects/` | ~1040 | One folder per effect: `effect.json`, material JSON, GLSL (`.vert` / `.frag`), masks and lookup textures | Yes | Effect definitions and their shaders and textures are resolved through the asset provider when a scene references `effects/<name>/effect.json` |
| `shaders/` | ~140 | Engine GLSL: per-material `.vert` / `.frag` and the shared headers (`common.h`, `common_blending.h`, `common_blur.h`, `common_composite.h`, `common_fog.h`, `common_foliage.h`, `common_fragment.h`, `common_particles.h`, `common_pbr_2.h`...) | Yes | Passes load `shaders/<name>.vert` / `.frag`; `#include` lines resolve against this folder. Layer blend modes (`colorBlendMode` 1-30) come from `common_blending.h`, so they are never re-implemented in C++ |
| `materials/` | ~590 | Material JSON plus `.tex` textures (masks, noise, gradients, particle sprites), GIFs | Yes | Looked up as `assets/<path>` then `assets/materials/<path>` |
| `presets/` | ~880 | Particle and material presets grouped by theme (fire, rain, snow, clock, ...) | Yes | `materials/presets/<name>...` is redirected to `assets/presets/<name>/...` |
| `particles/` | 6 | Example particle definitions | Partly | Resolved through the asset provider when a wallpaper references them |
| `fonts/` | ~20 | TrueType/OpenType fonts, including `NotoSans-Regular.ttf` | Yes | Default font for text layers (`systemfont`), plus fonts a scene names |
| `models/` | 7 | `util/` layer definitions (`composelayer`, `fullscreenlayer`, `solidlayer`, `projectlayer`, depth-test variants) and an editor camera | Partly | Composition, fullscreen and solid layers are recognised by file name; the JSON itself is only read through the asset provider |
| `scripts/` | 4 | The JavaScript that SceneScript scripts run on top of: `jsclasses/baseclasses.js` and `jsmodules/{wemath,wecolor,wevector}.js` | Yes | Loaded by the script engine at startup and on `import`; see below |
| `scenes/` | ~250 | Editor sample and template scenes (model editor, particle editor, video player, GIF previews) | No | Editor content, not needed at runtime |
| `zcompat/` | 11 | Compatibility data for specific Workshop items: `scene/shaders/<id>/` (replacement shaders such as pixelate and audio bars) and `web/<id>.json` | No | Candidate for fixing Workshop effects whose shaders do not translate; nothing reads it yet |

## Asset lookup order

For a relative path such as `materials/foo.tex` the asset manager tries, in order:

1. The wallpaper itself (`<wallpaper>/<path>`, then `<wallpaper>/materials/<path>`; a `scene.pkg` is memory-mapped in release builds or extracted in debug builds).
2. The install: `<install>/assets/<path>`, then `<install>/assets/materials/<path>`.
3. For `materials/presets/<name>...`: `<install>/assets/presets/<name>/<path>`.
4. `<install>/<path>`.
5. An absolute path, when the reference is already absolute.

## SceneScript files in the install

| File | Provides | Status |
| --- | --- | --- |
| `scripts/jsclasses/baseclasses.js` | `Vec2`, `Vec3`, `Vec4`, `Mat3`, `Mat4`, `MediaPlaybackEvent`, `IModelData`, `createScriptProperties` (`addSlider`, `addCheckbox`, `addText`, `addCombo`, `addColor`), `shared`, and the `_Internal` helpers the real engine uses to apply a scene's `scriptproperties` overrides and to convert user properties | Evaluated at startup |
| `scripts/jsmodules/wemath.js` | `deg2rad`, `rad2deg`, `smoothStep`, `mix` | Loaded by `import ... from 'WEMath'` |
| `scripts/jsmodules/wecolor.js` | `rgb2hsv`, `hsv2rgb`, `normalizeColor`, `expandColor` | Loaded by `import ... from 'WEColor'` |
| `scripts/jsmodules/wevector.js` | `angleVector2`, `vectorAngle2` | Loaded by `import ... from 'WEVector'` |

Without the install, the script engine still starts with a minimal fallback (`shared`, `createScriptProperties`) and logs that the Vec/Mat classes and modules are unavailable.

## What the install does not provide

These live inside the closed `scenescript64.dll` / renderer and must be implemented here, following the [official SceneScript reference](https://docs.wallpaperengine.io/en/scene/scenescript/reference.html): the `engine`, `input`, `thisScene`, `thisLayer`, `thisObject`, `console` and `localStorage` objects, the timers, audio buffers, layer/animation/effect/particle handles, the event dispatch, and media and user-property plumbing. [features.md](features.md) tracks which of these exist.
