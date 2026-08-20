# Runtime Architecture

This project is organized around runtime compatibility with Wallpaper Engine content.
The format layer must understand Wallpaper Engine files without depending on GPU/runtime objects,
and the renderer must not understand Wallpaper Engine JSON.

## Dependency direction

```text
app
 ↓
runtime
 ↓
wallpaper runtime
 ↓
scene systems ─────→ renderer ─────→ Sokol/Vulkan
 ↓                    ↑
assets ────────────────┘
 ↑
Wallpaper Engine formats
```

## Module responsibilities

- `src/app/`: process entry point and application lifecycle only.
- `src/runtime/`: runtime state, input, clock, profiling, and wallpaper lifecycle coordination.
- `src/wallpaper/`: wallpaper runtimes (`scene`, later `video`, `web`, and application wallpapers).
- `src/formats/wallpaper_engine/`: parsers/decoders for Wallpaper Engine file formats. No Sokol/Vulkan types.
- `src/assets/`: asset discovery, caching, and conversion between decoded assets and runtime resources.
- `src/render/`: renderer, render resources, shader runtime, render passes, and backend adapters.
- `src/render/backend/sokol/`: Sokol implementation/backend integration. `sokol_implementation.cpp` only instantiates the vendored Sokol headers.
- `src/platform/`: Linux/window-system/desktop integration.
- `src/debug/`: optional debug UI and runtime inspection.
- `src/common/`: small cross-cutting utilities with no wallpaper/render ownership.

## Format boundary

Wallpaper Engine JSON and binary formats are parsed into typed documents first:

```text
scene.json → SceneParser → SceneDocument → SceneRuntime → SceneTree / typed layers → Renderer
```

`cJSON` must eventually stop at the format boundary. Runtime layers must not own raw Wallpaper Engine JSON.

Texture decoding follows the same rule:

```text
.tex → TexDecoder → DecodedImage → AssetManager → renderer texture
```

The decoder uses project-owned pixel-format enums; the Sokol backend performs the final mapping to `sg_pixel_format`.

## Migration policy

The architecture is being migrated incrementally. Each migration stage should keep behavior unchanged until a dedicated feature commit explicitly changes runtime behavior.
