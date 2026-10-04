# Feature support

What Linux Wallpaper Engine can and cannot do compared with Wallpaper Engine on Windows.

> **Keep this file up to date.** Any change that adds, fixes, removes or breaks behaviour must update the matching entry in the same commit. A partial feature is never left as a bare "partial": list what works and what is missing underneath it. If a feature is not listed, add it.

Legend:

- [x] Supported
- [-] Partial: the nested bullets say exactly what works and what does not
- [ ] Not supported

"Supported" means it is implemented and used; pixel-exact output against the Windows renderer is not guaranteed. Anything marked *unverified* was read from code or notes but not checked against a real wallpaper.

Related: [wallpaper-engine-assets.md](wallpaper-engine-assets.md) lists what the Wallpaper Engine install provides and how we use it.

## Contents

- [Wallpaper types](#wallpaper-types)
- [Projects and packages](#projects-and-packages)
- [Scene layers](#scene-layers)
- [Camera and parallax](#camera-and-parallax)
- [Image effects and shaders](#image-effects-and-shaders)
- [Bloom and HDR](#bloom-and-hdr)
- [Particles](#particles)
- [Animations](#animations)
- [Puppet warp](#puppet-warp)
- [Lighting and 3D](#lighting-and-3d)
- [User properties](#user-properties)
- [Audio](#audio)
- [Media integration](#media-integration)
- [SceneScript](#scenescript)
- [Interaction](#interaction)
- [Textures and asset formats](#textures-and-asset-formats)
- [Platform, command line and tooling](#platform-command-line-and-tooling)
- [RGB hardware](#rgb-hardware)

## Wallpaper types

- [-] Scene wallpapers
  - Works
    - Image, particle, text and sound layers, layer hierarchy, effects, bloom/HDR, camera parallax, camera shake and camera-path zoom/pan
    - Property scripts on image alpha and text
  - Missing
    - Light layers, 3D model layers, perspective cameras
    - User properties are not applied to scene values
    - Most SceneScript APIs (see [SceneScript](#scenescript))
- [-] Video wallpapers
  - Works
    - Container detection for mp4, webm, mkv, avi, mov and wmv, with looping
    - FFmpeg software decoding, VA-API hardware decoding, and an automatic software fallback; the chosen decode path (zero-copy, CPU copy or software) is logged at startup
    - GPU-native VA-API / DMA-BUF video textures, including on layer-shell surfaces, with a guarded fallback when the import fails
    - Playback rate, volume and fit (fit / fill) read from the `project.json` defaults
    - Embedded audio track, muted by the silent flags
    - Pauses while the window is iconified or suspended
    - Streams from disk by default; `--video-ram` keeps one shared copy in RAM
    - Videos embedded in `.tex` files, including ones inside a memory-mapped package
  - Missing
    - Live editing of video properties (defaults only)
    - Playback controls beyond the above
- [-] Web wallpapers (experimental, build with `xmake f --web=y`)
  - Works
    - HTML, CSS, JavaScript and local web assets, in a separate renderer process (Qt WebEngine) that talks to the app over a Unix socket
    - Sends the `project.json` user properties (defaults) to the page as JSON
  - Missing
    - Not built by default; without the flag a web wallpaper fails to load
    - File and directory properties, live property changes
    - Audio visualization API (silence only) and media integration API (no-op listeners)
    - RGB API, user-configurable FPS API, web video playback
- [ ] Application wallpapers (the project type is rejected)

## Projects and packages

- [x] Wallpaper project folders and standalone `.pkg` files
- [x] `scene.pkg` handling
  - Release builds read the package in place from a memory map
  - Debug builds, `--extract-only` and packages without a `scene.json` extract to `extracted/`
- [x] `scene.json` parsing, including the authored scene resolution (falls back to 1920x1080 when absent)
- [x] `project.json`: `title`, `type` (scene, video, web) and `file`; the type is inferred from the file extension when needed
- [-] `project.json` `general.properties`
  - Works
    - Video wallpapers read rate, volume and fit
    - Web wallpapers receive all properties
    - A resolver (`src/wallpaper/user_properties.*`) parses every declared property by type
  - Missing
    - Scene wallpapers do not apply the resolved values (see [User properties](#user-properties))

## Scene layers

- [-] Image layers
  - Works
    - `size`, `origin`, `scale`, `angles` (Z rotation), `alpha` (static, keyframed or script-driven), `color` tint, `visible`
    - `parallaxDepth` per layer
    - `colorBlendMode`, `copybackground`
    - Model/material references and puppet models
  - Missing
    - X and Y rotation (angles are applied around Z only)
    - The `perspective` flag is parsed but sprites are always orthographic
    - Origin, scale, angles and color keyframe animation and scripts (only alpha is animated)
- [-] Text layers
  - Works
    - Font loading (TrueType through stb_truetype), default font `NotoSans-Regular.ttf` for `systemfont`
    - `pointsize`, `color`, `alpha` (including the alpha animation curve), `size`, `maxwidth`, row and width limits
    - Horizontal and vertical alignment and screen-corner anchoring
    - Text driven by a SceneScript `update()` (clock and date wallpapers), re-evaluated about four times per second
  - Missing
    - `opaquebackground`, `backgroundcolor` and `padding` are not parsed
    - Scripts on text color, alpha, point size or visibility
- [x] Sound layers
  - Playback modes single, loop and random with optional min/max delay
  - Volume, mute and start-silent
- [-] Composition, fullscreen and solid layers
  - Works
    - Composition layers (`composelayer.json`), fullscreen layers (`fullscreenlayer.json`) and solid layers (`solidlayer.json`) are recognised by name
    - Effects on these layers render to an off-screen target
    - Solid layers draw a plain white texture tinted by the layer color
  - Missing
    - `projectlayer.json` and the depth-test variants are not special-cased
    - The `solid` interaction flag (click/hover target) is not parsed
- [-] Layer hierarchy and parent transforms
  - Works
    - Parent/child links with accumulated translation, rotation and scale
    - `disablepropagation` for parallax
    - Named attachment points on puppet models that child layers follow
  - Missing
    - X/Y rotation is not applied when positioning 2D sprites
- [x] Layer blending: `colorBlendMode` 0 (normal), 1-30 through the install's `common_blending.h`, 31 (skip)
- [-] Texture animation (TEXS sprite sheets)
  - Works
    - Frame rectangles and durations, multiple atlas pages (only the current page stays resident)
  - Missing
    - Scripted playback control (`getTextureAnimation()`)
- [ ] Light layers
- [ ] 3D model layers
  - Objects of any other kind (lights, cameras, unknown) become hierarchy nodes only; no layer is drawn

## Camera and parallax

- [x] Orthographic scenes with `fit` and `fill` scaling (`stretch` is accepted but behaves like `fit`)
- [x] Camera parallax: enabled flag, amount, delay and mouse influence, plus the `g_ParallaxPosition` shader uniform and the Depth Parallax effect
- [x] Camera shake: enabled, amplitude, speed and roughness
- [-] Camera paths (entry animations)
  - Works
    - Zoom curve and the X/Y origin curves of a visible camera object, in single, loop and mirror modes
    - Applied as zoom about the view centre plus a relative pan
  - Missing
    - Origin Z, `eye`/`center`/`up` changes and multiple paths (`queuemode`, the `paths` file)
    - Camera objects bound to a hidden user property are only honoured through their default
- [ ] Perspective cameras and field of view (`fov`, `nearz` and `farz` are parsed but unused)
- [ ] Camera fade (parsed, not applied)
- [ ] Multiple cameras

## Image effects and shaders

Effects load from the install (see [wallpaper-engine-assets.md](wallpaper-engine-assets.md)). "Works" means the shader compiles and the pass executes.

- [-] Effect runtime
  - Works
    - `effect.json` parsing, multiple passes per effect and effect chains
    - Render targets (`fbos`) with scale factors, `_rt_FullFrameBuffer` and layer composite targets
    - Effect masks and texture inputs, constants, shader combos
    - Keyframed (animated) constants and the effect `visible` flag
  - Missing
    - Other `_rt_*` target names are untested
    - Material depth test, depth write and cull mode are not applied
    - Custom Workshop effects whose GLSL Slang rejects
- [-] Effects known to work (from testing)
  - Water Waves, Water Flow, Water Ripple
  - Shake, Foliage Sway, Iris Movement, Scroll
  - Opacity, Tint, Color Key, Blend, Blend Gradient
  - Blur, Blur Precise, Motion Blur
  - Light Shafts, God Rays, Shine
  - Depth Parallax
  - VHS, Skew, Film Grain, Perspective, Reflection, Chromatic Aberration, Twirl, Spin, Clouds, Fisheye, Local Contrast, Refraction, Swing, Shimmer, Nitro, Edge Detection, Transform
  - Known failing: some Pulse variants, Cloud Motion, Cursor Ripple, Water Caustics, and Workshop effects that use GLSL constructs Slang rejects (audio bars, hue shift, auto sway, clipping mask, pixelate and others)
  - Untested: Radial Blur, X-Ray, Glitter, Fire, Advanced Fluid Simulation
- [-] Shader pipeline
  - Works
    - Wallpaper Engine GLSL preprocessing with `#include` from the install's `shaders/`
    - Rewrites for Slang: scalar/vector mismatches, out-of-range swizzles, narrowing conversions, vector width mismatches, `mix` differences, HLSL-style initializers
    - Runtime Slang compilation to SPIR-V with a persistent on-disk cache (under `$XDG_CACHE_HOME`)
    - Built-in uniforms: time, texture resolutions, `g_ParallaxPosition`, effect texture projection matrices, pointer position, ambient and skylight colors, screen size, texel size, model-view-projection, audio spectrum (16/32/64 bands)
    - Texture bindings `g_Texture0..N`
  - Missing
    - Full vertex attribute support (2D `a_Position` / `a_TexCoord` and the particle layout only)
    - Some GLSL constructs the rewrites do not cover
- [ ] Per-layer lighting and reflections in effects (see [Lighting and 3D](#lighting-and-3d))

## Bloom and HDR

- [-] Bloom
  - Works
    - LDR chain: quarter-resolution extract, blur and combine, with strength and threshold
    - Multi-level HDR chain with 1-8 iterations, scatter, strength, threshold and feather
    - The `general.hdr` flag, ambient, skylight and clear colors
  - Missing
    - Bloom tint is fixed to white
    - "Ultra HDR" output

## Particles

- [-] Particle systems
  - Works
    - Emitters: sphere random, box random, point (default); multiple emitters per system
    - Initializers: lifetime, size, velocity, color, alpha, rotation, angular velocity, turbulent velocity
    - Operators: movement (gravity and drag), alpha fade, alpha/size/position oscillation, turbulence
    - Renderers: sprite, sprite trail, trail (beam aspect ratio, camera-depth trails)
    - Blend modes: translucent and additive
    - Sprite sheets (grids and TEXS frame data) with sequence, random-frame and once modes, and frame blending through the `SPRITESHEETBLEND` combo
    - Refraction through the `REFRACT` combo with a normal map, reading the scene color
    - Child systems: `static`, `eventfollow`, `eventspawn`, `eventdeath` with origin, angles, scale, `maxcount` and probability
    - Maximum particle count, start-time warm-up, perspective particles
    - Instance overrides: alpha, rate (time scale), size, count (emission scale), speed, lifetime, color, plus the per-override disable flags
  - Missing
    - Other emitters and operators (anything not listed above is ignored)
    - Control points (only `controlpointstartindex` for child systems is read)
    - Mouse-interactive and audio-responsive particles
    - World-space particles, material lighting
    - Script control of particle systems

## Animations

- [-] Property animations
  - Works
    - Keyframed `alpha` on image layers and keyframed effect constants, in single, loop and mirror modes with linear interpolation
    - Camera-path zoom and origin curves
  - Missing
    - Keyframed origin, scale, angles and color on objects
    - Bézier interpolation (keyframes are interpolated linearly)
    - `startpaused`, playback rate, named animations and animation events
    - Timeline control from scripts

## Puppet warp

- [-] Puppet warp
  - Works
    - `.mdl` (`MDLV`) parsing with the four known vertex layouts, with or without skinning
    - Skeleton, four bone weights per vertex, linear blend skinning
    - Animation clips (single, mirror and loop modes), multiple animation layers with rate, blend, additive and visibility
    - Attachment points that child layers follow
    - Effects on puppet layers run on the source texture before the mesh is assembled
  - Missing
    - Blend shapes (morph targets), bone constraints, inverse kinematics, spring and rigid simulation, interactive bones
    - Character sheets and texture channels
    - Script control (`getAnimationLayer`, bone and blend-shape APIs)

## Lighting and 3D

- [ ] 2D lighting, normal maps, reflections, light sources (point, spot, directional)
  - Ambient and skylight colors are passed to shaders only; nothing computes lighting
- [ ] 3D scenes, FBX/OBJ models, model materials (PBR maps), skeletal 3D animation, shadows, volumetric lighting, fog
- [ ] Built-in 3D shaders (fur, vegetation, chroma)

## User properties

- [-] User properties
  - Works
    - Resolution of every `project.json` property by type (bool, slider, combo, color in 0..1 or 0..255, text)
    - Overrides from the desktop GUI's saved values and from repeatable `--set-property key=value`
  - Missing
    - Resolved values are not applied: `{ "user": ..., "value": ... }` entries in `scene.json` keep their default
    - Property-changed events, display conditions, groups
    - `engine.userProperties` is empty and `applyUserProperties` is never called
    - Texture replacement, user shortcut and file properties

## Audio

- [x] Audio playback (WAV, MP3, OGG, FLAC through miniaudio), loop playback
- [x] Sound layers (see [Scene layers](#scene-layers)) and video audio
- [x] Silent mode: `--no-audio`, `-s`/`--silent`, `--mute` or `LWE_NO_AUDIO=1`; diagnostic runs are always silent
- [-] System audio capture and spectrum
  - Works
    - Captures the PulseAudio monitor of the default sink at 48 kHz stereo
    - Smoothed 16, 32 and 64-band spectra per channel
    - Spectrum uniforms for shaders and `engine.registerAudioBuffers` for scripts
  - Missing
    - Zeros when no monitor device exists
    - No volume control UI
- [-] Audio-responsive content
  - Works
    - Shader effects that read the spectrum uniforms (subject to the shader compiling)
    - Scripts that read the audio buffers
  - Missing
    - Audio-responsive particles

## Media integration

- [-] Media session (MPRIS)
  - Works
    - A source module (`src/shared/media/`) reads track title, artist, album, playback state and timeline from MPRIS players over sd-bus, and decodes album art from `file://` URLs
    - Median-cut color extraction (primary, secondary, tertiary, text, high-contrast)
    - Built only when libsystemd is available (`mpris` option); otherwise it is a no-op
  - Missing
    - Not connected to SceneScript (`media*Changed` events never fire) or to the `$mediaThumbnail` texture
    - `http(s)` album art URLs
    - Not exercised against a live player yet

## SceneScript

Reference: [SceneScript documentation](https://docs.wallpaperengine.io/en/scene/scenescript/reference.html). Scripts run on one shared QuickJS runtime as ES modules, using the install's own `baseclasses.js` and `jsmodules`. `utils/script_corpus.cpp` loads every script of a Workshop folder and reports what fails.

- [-] Script runtime
  - Works
    - Shared runtime, per-script module scope, `import ... from 'WEMath' | 'WEColor' | 'WEVector'`
    - 64 MB memory limit, per-call time budgets, and a script is disabled after three consecutive errors
    - `scriptproperties` overrides applied the way the real engine does
  - Missing
    - Hooks other than `init` and `update` (see Events)
- [-] Properties that can host a script
  - Works
    - Image `alpha` (`init(value)`, then `update(value)` every frame; a keyframed alpha is passed in as the value)
    - Text content (`update(string)`, about four times per second)
  - Missing
    - `origin`, `scale`, `angles`, `visible`, `color`, `size`, effect constants, sound volume, particle fields, camera and scene settings
- [-] Globals
  - [-] `engine`
    - Works: `frametime`, `runtime`, `timeOfDay`, `canvasSize`, `screenResolution`, `AUDIO_RESOLUTION_16/32/64`, `registerAudioBuffers`, `setTimeout`, `setInterval` (returning cancel functions), device/orientation queries
    - Missing: `userProperties` is empty, `openUserShortcut` and `registerAsset` are stubs
  - [x] `console` (`log`, `info`, `debug`, `warn`, `error`; rate-limited)
  - [x] `shared`, `localStorage` (global and per-screen areas, 100 KB each, persisted per wallpaper under `~/.local/share/linux-wallpaperengine/localstorage/`)
  - [x] `Vec2`, `Vec3`, `Vec4`, `Mat3`, `Mat4`, `WEMath`, `WEColor`, `WEVector`, `MediaPlaybackEvent` (from the install)
  - [ ] `thisLayer` and `thisObject` (undefined; layer and animation handles do not exist)
  - [-] `thisScene`
    - Works: the object exists
    - Missing: every method is a stub that returns null/false/empty (`getLayer`, `createLayer`, `destroyLayer`, `sortLayer`, camera and scene properties...)
  - [-] `input`
    - Works: the object exists
    - Missing: always zero/false; pointer events and hit testing exist as a tested module (`src/wallpaper/2d/input/`) but are not connected
- [-] Events
  - Works: `init(value)` and `update(value)` for property scripts
  - Missing: `destroy`, `resizeScreen`, `applyUserProperties`, `applyGeneralSettings`, `cursorEnter/Leave/Move/Down/Up/Click`, `mediaStatusChanged`, `mediaPlaybackChanged`, `mediaPropertiesChanged` (only a title-only stub), `mediaThumbnailChanged`, `mediaTimelineChanged`
- [ ] Layer API (`ILayer`, `IImageLayer`, `ITextLayer`, `IEffectLayer`, `ISoundLayer`): transforms, parenting, attachments
- [ ] Animation handles (`IAnimation`, `IAnimationLayer`, `ITextureAnimation`, `IVideoTexture`)
- [ ] Effect and material handles (`IEffect`, `IMaterial`)
- [ ] Particle handles (`IParticleSystem`, `IParticleSystemInstance`)
- [ ] Dynamic layers (`createLayer`, `destroyLayer`, `sortLayer`) and model data (`IModelData`)
- [ ] Bone, blend-shape and physics APIs

## Interaction

- [x] Mouse position and mouse-driven parallax
- [-] Pointer buttons
  - Works
    - Wayland layer-shell surfaces forward `wl_pointer` button and enter/leave events; the frame loop tracks the pressed-button mask and the cursor's scene-world position
    - A tested hit-test module finds the topmost visible solid layer under the cursor (rotation, non-uniform and negative scale, parents) and a tracker produces enter, leave, move, down, up and click
  - Missing
    - Layers are not registered as hit targets and no events reach scripts
    - Live delivery to a background layer-shell surface has not been confirmed
- [ ] Keyboard, touch and gamepad input
- [ ] Interactive effects, particles and puppet bones

## Textures and asset formats

- [x] Wallpaper Engine `.tex`: RGBA8, RG8, R8, BC1/DXT1, BC2/DXT3, BC3/DXT5, LZ4-compressed payloads, multiple images and mip levels
- [x] Embedded PNG/JPEG payloads, GIF containers and MP4 video payloads inside `.tex`
- [-] Texture flags
  - Missing: clamp/wrap and interpolation flags from the header are not applied
- [-] Materials, shaders and particle definitions
  - Works: loaded from wallpapers and from the install (see [wallpaper-engine-assets.md](wallpaper-engine-assets.md))
  - Missing: formats and fields not listed in the sections above
- [ ] Other `.mdl` variants and 3D model formats

## Platform, command line and tooling

- [x] Vulkan renderer (sokol) with GPU selection (`--list-gpus`, `--gpu`), frame cap (`-f`/`--fps`) and scaling (`--scaling default|fit|fill`)
- [-] Wayland wlr-layer-shell backend (`-r`/`--screen-root`, `--layer`; debug builds also `--layer-size`, `--layer-anchor`)
  - Works: background, bottom, top and overlay layers, anchoring, output selection, pointer motion and buttons, parallax
  - Missing: `--scaling stretch` and `--clamp` are accepted but ignored
- [x] Debug-build diagnostics: `--diagnose` family (frame capture with pass images, scene stages, render graph, shaders, uniforms), `--disable-effects`, `--disable-particles`, `--disable-bloom`, `--sandbox`, `--no-ui`, and an ImGui inspector
- [-] Desktop integration
  - Works
    - Wallpaper surface through wlr-layer-shell (see above), GPU enumeration and selection (also on the layer surface), frame cap
    - Package extraction CLI (`--extract-only`)
    - Debug sandbox, scene tree keyboard navigation, wallpaper path copy and a sound-layer mute toggle (cursor calls are skipped on layer surfaces)
  - Missing
    - Multi-monitor rendering from one process, per-monitor, spanned and cloned wallpapers (run one process per output)
    - Pause/resume and mute/unmute controls while running
    - Fullscreen-application detection and screensaver mode
    - A runtime control interface beyond the command line
- [x] Unit tests in `tests/` (`xmake test`) and the script corpus runner in `utils/` (`xmake build script_corpus`)
- [x] CI runs formatting and static analysis (`xmake check`)

## RGB hardware

- [ ] RGB hardware integration (Corsair iCUE, Razer Chroma), RGB source layers, web wallpaper RGB API
