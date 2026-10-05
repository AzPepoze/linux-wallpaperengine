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
- [Wallpaper switching and transitions](#wallpaper-switching-and-transitions)
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
- [-] Web wallpapers (experimental; enabled automatically when Qt6 WebEngine is installed, disable with `xmake f --web=n`)
  - Works
    - HTML, CSS, JavaScript and local web assets, in a separate renderer process (Qt WebEngine) that talks to the app over a Unix socket
    - Frames stream to the engine with automatic fallback: a zero-copy DMA-BUF the engine samples directly, else an offscreen render-control target with asynchronous readback, else an in-process widget grab
    - `--web-transport auto|dma-buf|off-screen|snapshot` (or the `web_transport` key in `config.json`) selects the transport; the CLI flag overrides the config
    - Sends the `project.json` user properties (defaults) to the page as JSON
  - Missing
    - Not built by default; without the flag a web wallpaper fails to load
    - File and directory properties, live property changes
    - Audio visualization API (silence only) and media integration API (no-op listeners)
    - RGB API, user-configurable FPS API, web video playback
- [ ] Application wallpapers (the project type is rejected)

## Wallpaper switching and transitions

- [x] Runtime switching: a running instance loads a new wallpaper in place, in the same window or layer surface
- [x] Wallpaper Engine transition shaders, loaded from the install (`assets/shaders/HLSL/dx11playlisttransition.*`), covering the full `FADEEFFECT` set 0-26: fade, mosaic, diffuse, horizontal/vertical slide, horizontal/vertical fade, clouds, burnt paper, circular, zipper, door, lines, zoom, drip, pixelate, bricks, paint, fade to black, twister, black hole, crt, radial wipe, glass shatter, bullets, ice and boilover
- [x] `none` (hard cut) and `random` (one effect picked per switch), with a configurable duration (`--transition`, `--transition-duration`, default `fade` / 1000 ms)
- [x] Audio crossfade: the outgoing wallpaper's audio fades out while the incoming wallpaper's fades in, driven by the transition progress. Scene sound layers crossfade; video/web audio is assigned to the same group but still cuts at the swap (fixed when per-instance retention lands in P2)
- [x] Transition mode (`--transition-mode`, config `transition_mode`, default `freeze`)
  - `freeze` holds the outgoing wallpaper's last frame for the fade; `continue` steps and renders the outgoing wallpaper through the fade (scene, video and web), then destroys it
  - Both modes crossfade the outgoing audio into the incoming audio
- [x] The outgoing frame is captured and held while the new wallpaper loads, so a slow load does not show a black gap; a failed load keeps the frozen frame
- [x] Type-agnostic: scene, video and web wallpapers all switch through the same compositor, and 3D inherits it when 3D rendering lands
- [-] Single-instance control
  - Works: launching with a wallpaper for a display that already runs an instance hands the switch over a Unix socket in `$XDG_RUNTIME_DIR/linux-wallpaperengine/` and exits; `--no-control` opts out
  - Missing: no explicit switch command separate from launching with a path, and no per-output targeting when a path is not the first argument
- [-] Bricks (#16) and glass shatter (#23)
  - Works: geometry generated on the CPU (falling brick quads, a tessellated shard plane) since this renderer has no geometry-shader stage
  - Missing: not yet verified pixel-for-pixel against the Windows renderer

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
  - Known failing: some Pulse variants, Cloud Motion, Water Caustics, and Workshop effects that use GLSL constructs Slang rejects (audio bars, hue shift, auto sway, clipping mask, pixelate and others)
  - Untested: Radial Blur, X-Ray, Glitter, Fire, Advanced Fluid Simulation
- [-] Shader pipeline
  - Works
    - Wallpaper Engine GLSL preprocessing with `#include` from the install's `shaders/`
    - Rewrites for Slang: scalar/vector mismatches, out-of-range swizzles, narrowing conversions, vector width mismatches, `mix` differences, HLSL-style initializers
    - Runtime Slang compilation to SPIR-V with a persistent on-disk cache (under `$XDG_CACHE_HOME`)
    - Pointer uniforms `g_PointerPosition`, `g_PointerPositionLast` and `g_PointerState` (normalized to the output surface), which Cursor Ripple needs; its projection back into layer space assumes the layer fills the screen
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
    - Script control of particle systems (control points)

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
    - Script control of blend shapes

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
- [x] Silent mode: `--no-audio`, `-s`/`--silent` or `--mute`; diagnostic runs are always silent
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
    - Events reach scripts (`media*Changed`), and a material that lists `$mediaThumbnail` under `usertextures` samples the album art as a 256x256 texture
  - Missing
    - `http(s)` album art URLs

## SceneScript

Reference: [SceneScript documentation](https://docs.wallpaperengine.io/en/scene/scenescript/reference.html). Scripts run on one shared QuickJS runtime as ES modules, using the install's own `baseclasses.js` and `jsmodules`. `utils/script_corpus.cpp` loads every script of a Workshop folder and reports what fails.

- [x] Script runtime
  - Shared runtime, per-script module scope, `import ... from 'WEMath' | 'WEColor' | 'WEVector'`
  - 64 MB memory limit, per-call time budgets, and a script is disabled after three consecutive errors
  - `scriptproperties` overrides applied the way the real engine does
  - `--script-profile` logs the five scripts that spent the most time every 10 seconds
  - Scripts are scoped per scene: each wallpaper has its own `shared`, `localStorage` and layer ids, and events only reach the scripts of the active scene (needed while two wallpapers are alive in a transition)
- [-] Properties that can host a script
  - Works
    - `origin`, `scale`, `angles`, `visible`, `color` and image `size` of a scene object: `init(value)` runs once on the first frame (after the whole scene exists), then `update(value)` every frame with the property's current value; the result is written to the scene tree node or the layer. Values arrive as real `Vec2`/`Vec3` objects, and a number returned for a vector broadcasts to every component
    - Image `alpha` (`init(value)`, then `update(value)` every frame; a keyframed alpha is passed in as the value)
    - Text content (`update(string)`, about four times per second)
    - `visible` of a group (an object without a layer) hides everything beneath it
    - Effect `visible` and effect constants (number, `Vec2`, `Vec3`); `thisObject` is the effect, and its `getAnimation()` controls the constant's keyframe animation (`startpaused`, `play()`, `rate`...)
  - Missing
    - Particle fields and camera properties
    - Scripts on text color, point size and other text properties
    - Four-component effect constants
- [-] Globals
  - [-] `engine`
    - Works: `frametime`, `runtime`, `timeOfDay`, `canvasSize`, `screenResolution`, `AUDIO_RESOLUTION_16/32/64`, `registerAudioBuffers`, `setTimeout`, `setInterval` (returning cancel functions), device/orientation queries
    - `userProperties` holds every project property (colors as `Vec3`), resolved from `project.json`, the GUI config and `--set-property`
    - Missing: `openUserShortcut` and `registerAsset` are stubs
  - [x] `console` (`log`, `info`, `debug`, `warn`, `error`; rate-limited)
  - [x] `shared`, `localStorage` (global and per-screen areas, 100 KB each, persisted per wallpaper under `~/.local/share/linux-wallpaperengine/localstorage/`)
  - [x] `Vec2`, `Vec3`, `Vec4`, `Mat3`, `Mat4`, `WEMath`, `WEColor`, `WEVector`, `MediaPlaybackEvent` (from the install)
  - [-] `thisLayer`
    - Works: `origin`, `scale`, `angles` (degrees), `parallaxDepth`, `visible` (read/write), `size` and `name` (read), `color`, `alpha`, `getParent()`, `getChildren()`, `getTransformMatrix()` (`Mat4`), `getAnimation`, `getAnimationLayer`, `getAnimationLayerCount`, `getTextureAnimation`
    - Works: text layers (`text`, `color`, `alpha`, `font`, `pointsize`, `padding`, `horizontalalign`, `verticalalign`, `limitwidth`, `maxwidth`, `limitrows`, `maxrows`, `opaquebackground`, `backgroundcolor`; `anchor` is kept but the layout does not follow screen edges) and sound layers (`volume`, `play`, `stop`, `pause`, `isPlaying`)
    - Works: `solid` (the layer receives cursor events)
    - Works: `getEffect(name | index)` and `getEffectCount()`; an effect has `visible`, `name`, `getMaterialCount()`, `getMaterial(index)` (every shader constant of that pass is a property), `setMaterialProperty(name, value)`, `getMaterialProperty(name)` and `executeMaterialFunction(name)` (functions an effect defines to clear its buffers); a value set by a script replaces the constant's keyframes
    - Works: `transformAttachmentToTexture(layer, attachment)` (a `Mat3` into this layer's texture space)
    - Works: `setParent(parent, adjustTransforms?)` and `setParent(parent, attachment, adjustTransform?)` (with adjust, the layer stays where it is in the world), `rotateObjectSpace`, `getAttachmentIndex`, `getAttachmentMatrix`, `getAttachmentOrigin`, `getAttachmentAngles` (puppet attachments, world space)
    - Works: model layers (puppet): `createAnimationLayer`, `destroyAnimationLayer` (later layers shift down one index), `playSingleAnimation` (plays once and removes itself), `rootmotion` (off ignores the root bone's animated translation); `perspective` is kept but the 2D renderer ignores it
    - Not available for scripts on camera or scene-level properties (no owning layer)
  - [-] `thisObject`
    - Works: `visible`, `name`, `getAnimation()`; the effect itself for scripts on an effect property
  - [-] `thisScene`
    - Works: `getLayer(name | index)`, `getLayerCount()`, `enumerateLayers()`, `getLayerIndex()`
    - Works: `createLayer(config)` (an asset path, or an object shaped like a scene.json object: image, particle, text or sound; its own scripts and animations are bound), `destroyLayer`, `sortLayer`
    - Works: scene settings `bloom`, `bloomstrength`, `bloomthreshold`, `clearcolor`, `ambientcolor`, `skylightcolor`, `cameraparallax*` and `camerashake*` (read/write; bloom values apply live in HDR scenes only, other scenes bake them when the bloom passes are created)
    - Works: `getInitialLayerConfig(layer)` returns the object as authored in scene.json
    - Works: `getCameraTransforms()` and `setCameraTransforms({eye, center, up, zoom})`; `zoom` zooms the 2D view, while `eye`, `center` and `up` are stored but the orthographic renderer does not use them
    - Works: `clearenabled`, `camerafade`, `fov`, `nearz`, `farz` (read/write; `fov`, `nearz` and `farz` are kept but the orthographic renderer does not use them)
  - [x] `input`: `cursorWorldPosition`, `cursorScreenPosition` and `cursorLeftDown` follow the pointer
- [x] Animation handles: timeline, sprite-sheet and puppet animation layers (`rate`, `fps`, `frameCount`, `duration`, `frame`, `play`, `stop`, `pause`, `blend`, `visible`, `addEndedCallback`, `join`); `getAnimation` / `getTextureAnimation` return an object even when the layer has no such animation, so unguarded calls do not throw
- [-] Events
  - Works: `init`, `update`, `applyUserProperties` (once after the first init), `cursorEnter/Leave/Move/Down/Up/Click` on solid image layers, and `mediaStatusChanged`, `mediaPlaybackChanged`, `mediaPropertiesChanged`, `mediaThumbnailChanged`, `mediaTimelineChanged` from MPRIS
  - Works: `destroy` (when the scene is unloaded), `resizeScreen` (receives the new size as `x`, `y`) and `applyGeneralSettings` (once at start, with `language` taken from the locale)
- [x] Video texture handle (`thisLayer.getVideoTexture()`)
  - Works: `play()`, `pause()`, `stop()` (back to the first frame), `isPlaying()`, `duration`, `rate`, `loop` (off stops at the end), `getCurrentTime()`, `setCurrentTime(t)` (seeks the video and its audio track), `addEndedCallback()`
- [-] Particle handles (`thisLayer.getParticleSystem()`)
  - Works
    - System: `play()`, `pause()`, `stop()` (stops emission and clears the live particles), `isPlaying()`, `emitParticles(n)`
    - `instance`: `alpha`, `size`, `count`, `speed`, `lifetime`, `rate`, `colorn`
  - Missing
    - `instance.controlpoint0` to `controlpoint7` are stored, but nothing in the simulation reads them
    - Child systems are not separate instances
- [-] Bones, blend shapes and bone physics (`thisLayer`, puppet models)
  - Works
    - `getBoneCount`, `getBoneIndex`, `getBoneParentIndex`, `getBoneTransform` / `setBoneTransform` (world), `getLocalBoneTransform` / `setLocalBoneTransform`, `getLocalBoneOrigin` / `Angles` and their setters; a set replaces the animated pose of that bone
    - `getBlendShapeIndex`, `getBlendShapeWeight`, `setBlendShapeWeight`, `applyBonePhysicsImpulse` and `resetBonePhysicsSimulation` answer for a model without blend shapes or physics bones (index -1, weight 0, no effect), which is every model in the supported `.mdl` formats
  - Missing
    - Models with morph targets or physics bones: the `.mdl` parser reads neither
- [-] Model data (`IModelData`)
  - Works
    - `thisScene.createModelData({ shapes })`, `applyData`, `replaceData`, `destroyModelData`, the `IModelData.POSITION / NORMAL / UV / TANGENT_SIGNED / COLOR` constants, and `thisScene.createLayer({ model })`; asset handles from `engine.registerAsset()` work as materials and as `createLayer` arguments
    - The mesh is drawn flat into the layer's picture: each shape is textured with its material's first texture (white when it has none)
    - Confirmed on screen: a script-made triangle renders as a solid white shape of the expected size and position on the layer origin (deterministic `--diagnose` capture of a test scene)
  - Missing
    - Depth, lighting, normals, tangents, vertex colors and the material's own shader; `perspective`
    - Shapes with more than 65535 vertices

## Interaction

- [x] Mouse position and mouse-driven parallax
- [-] Pointer buttons
  - Works
    - Wayland layer-shell surfaces forward `wl_pointer` button and enter/leave events; the frame loop tracks the pressed-button mask and the cursor's scene-world position
    - A tested hit-test module finds the topmost visible solid layer under the cursor (rotation, non-uniform and negative scale, parents) and a tracker produces enter, leave, move, down, up and click
    - Solid image layers with cursor hooks are hit targets and their scripts receive the events
  - Missing
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
    - A runtime control interface beyond the command line and the liquid-wallpaper switch socket
- [x] Unit tests in `tests/` (`xmake test`) and the script corpus runner in `utils/` (`xmake build script_corpus`)
- [x] CI runs formatting and static analysis (`xmake check`)

## RGB hardware

- [ ] RGB hardware integration (Corsair iCUE, Razer Chroma), RGB source layers, web wallpaper RGB API
