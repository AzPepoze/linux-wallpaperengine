# Linux Wallpaper Engine

Linux renderer for Wallpaper Engine projects.

## Requirements

### System requirements

- Linux
- GCC or Clang with C++20 support
- [xmake](https://xmake.io/)
- Vulkan development libraries and a working Vulkan GPU driver
- [Slang](https://github.com/shader-slang/slang) compiler/runtime libraries
- X11 development libraries (`libX11`, `libXcursor`, `libXi`)
- FFmpeg development libraries (`libavformat`, `libavcodec`, `libavutil`, `libswscale`, `libswresample`)
- VA-API development libraries (`libva`, `libva-drm`)
- `libdrm`
- Optional, for web wallpapers (`xmake f --web=y`): Qt6 WebEngine (`qt6-webengine`)
- A local Wallpaper Engine installation with its original `assets/` data (required at runtime; set `WALLPAPER_ENGINE_PATH` or `engine_path` in `config.json`)

### Dependencies managed by xmake

The following dependencies are fetched by xmake and normally do not need to be installed manually:

- Sokol
- linmath.h
- Vulkan-Headers
- LZ4
- miniaudio
- cJSON
- stb
- QuickJS (SceneScript text objects)
- Dear ImGui (debug builds only)
- libwayland-client, `wayland-scanner` (optional, for the desktop layer backend)

### Arch Linux / CachyOS

```bash
sudo pacman -S --needed \
    xmake \
    gcc \
    vulkan-icd-loader \
    shader-slang \
    libx11 \
    libxcursor \
    libxi \
    ffmpeg \
    libva \
    libdrm
```

A Vulkan driver for your GPU is also required, for example `vulkan-radeon`, `vulkan-intel`, or the appropriate NVIDIA driver.

Optional, for rendering on the desktop layer under Wayland (`xmake f --layer_shell=y`, enabled automatically when found):

```bash
sudo pacman -S --needed wayland wayland-protocols
```

Optional, for web wallpapers:

```bash
sudo pacman -S --needed qt6-webengine
```

For development checks:

```bash
sudo pacman -S --needed clang cppcheck
```

## Build and run

| Goal                                                 | Command                                                               |
| ---------------------------------------------------- | --------------------------------------------------------------------- |
| Build the default configuration                      | `xmake`                                                               |
| Run a wallpaper project directory or `.pkg` file     | `xmake run linux-wallpaperengine "/path/to/wallpaper"`                |
| Run a video wallpaper                                | `xmake run linux-wallpaperengine "/path/to/video.mp4"`                |
| Configure and run a debug build                      | `xmake f -m debug` then `xmake run linux-wallpaperengine`             |
| Configure and run a release build                    | `xmake f -m release` then `xmake run linux-wallpaperengine`           |
| List available GPUs                                  | `bin/<mode>/linux-wallpaperengine --list-gpus`                         |
| Select a GPU                                         | `bin/<mode>/linux-wallpaperengine --gpu <index-or-name> "/path"`      |
| Extract a package                                    | `bin/<mode>/linux-wallpaperengine --extract-only "/path/to/scene.pkg"` |
| Keep a video file in RAM instead of streaming it     | `bin/<mode>/linux-wallpaperengine --video-ram "/path/to/video.mp4"`   |
| Clean build outputs                                  | `xmake clean`                                                         |
| Validate formatting and static analysis              | `xmake check`                                                         |
| Build and run all unit checks                        | `xmake test`                                                          |
| Format source files                                  | `xmake format`                                                        |
| Validate, build, and launch the debug effect sandbox | `xmake sandbox`                                                       |

Launcher-compatible options (the wallpaper path may come first or last):

| Option | Effect |
| --- | --- |
| `--assets-dir <path>` | Wallpaper Engine install root or its `assets/` directory |
| `-f, --fps <n>` | Cap the frame rate (default 60; `0` renders at the display rate) |
| `-s, --silent` | Disable audio |
| `--video-ram` | Load video files into RAM (one shared copy). By default video is streamed from disk, so large 4K files stay out of memory. `LWE_VIDEO_RAM=1` does the same |
| `--scaling <default\|fit\|fill\|stretch>` | `fill` crops to cover, `fit` letterboxes (`stretch` currently behaves like `fit`) |
| `--clamp <mode>` | Accepted and ignored |
| `-r, --screen-root <output>`, `--layer <background\|bottom\|top\|overlay>` | Draw as a `wlr-layer-shell` wallpaper surface on the named output (default layer `background`), anchored to all edges with pointer parallax. Needs a build with `--layer_shell=y` and `WAYLAND_DISPLAY`; otherwise, or if the output is not found, the app logs the reason and runs in a window |
| `--layer-size <WxH>`, `--layer-anchor <edges>` | Debug builds only: use a small anchored rectangle (for example `320x180` and `top-left`) instead of the full output |

Build outputs are written to `bin/<mode>/`.

Release builds read `scene.pkg` in place from a memory map, so nothing is extracted and startup does not wait on disk writes. Debug builds, `--extract-only` and packages without a `scene.json` extract to `extracted/`. Compiled shaders are cached in `$XDG_CACHE_HOME/linux-wallpaperengine/` (default `~/.cache/linux-wallpaperengine/`) and reused across launches.

### Render Diagnostics (Debug Mode)

Render diagnostics are available in debug builds and are opt-in. Enable them with `--diagnose` or `--diagnostics`:

```bash
bin/debug/linux-wallpaperengine --diagnose "/path/to/wallpaper"
```

Diagnostics can capture render-pipeline state including pass images, scene stages, render graphs, shader code, and uniforms. Add `--diagnose-deterministic` to use a fixed 1/60 s step and a seeded random generator, so the same frame renders identically on every run (the window size and cursor position still affect the result).

## FEATURE SUPPORT

> Runtime compatibility with Wallpaper Engine wallpapers.
>
> - [x] Supported
> - [-] Partial / Incomplete
> - [ ] Not supported yet

### Wallpaper Types

- [-] Scene wallpapers
- [x] Video wallpapers
- [-] Web wallpapers (experimental, build with `xmake f --web=y`)
- [ ] Application wallpapers

### Scene Layers & Assets

- [x] Image layers
- [-] Particle system layers
- [-] Text layers (alignment-corner anchoring and SceneScript-driven clock / date text)
- [x] Sound layers
- [ ] Light layers
- [ ] 3D model layers
- [-] Composition layers
- [-] Fullscreen / effect layers
- [-] Layer hierarchy
- [x] Layer visibility
- [x] Layer position
- [x] Layer scale
- [x] Layer rotation
- [-] Parent transforms
- [-] Layer blending
- [-] Layer attachments (named puppet sockets)
- [-] TEXS texture animations (frame rectangles, durations, and multiple atlas pages; script playback control is not implemented)

### Camera & Parallax

- [x] Orthographic scenes
- [ ] Perspective scenes
- [x] Camera parallax
- [x] Mouse influence
- [x] Parallax amount
- [x] Parallax delay
- [x] Per-layer `parallaxDepth`
- [x] Horizontal / vertical parallax depth
- [x] Parent parallax propagation
- [x] `disablepropagation`
- [x] Shader `g_ParallaxPosition`
- [x] Depth Parallax effect integration
- [ ] Camera assets
- [ ] Camera field of view
- [ ] Multiple cameras
- [ ] Camera paths
- [ ] Camera shake

### Image Effects

> "Works" means the shader compiles and the pass executes; pixel-exact output is not guaranteed.

#### Working

- [x] Water Waves, Water Flow, Water Ripple
- [x] Shake, Foliage Sway, Iris Movement, Scroll
- [x] Opacity, Tint, Color Key, Blend, Blend Gradient
- [x] Blur, Blur Precise, Motion Blur
- [x] Light Shafts, God Rays, Shine
- [x] Depth Parallax
- [x] VHS, Skew, Film Grain, Perspective, Reflection, Chromatic Aberration, Twirl, Spin, Clouds, Fisheye, Local Contrast, Refraction, Swing, Shimmer, Nitro, Edge Detection, Transform

#### Known failing

- [ ] Pulse (some shader variants)
- [ ] Cloud Motion
- [ ] Cursor Ripple
- [ ] Water Caustics
- [ ] Workshop effects with GLSL constructs Slang rejects (audio bars, hue shift, auto sway, clipping mask, pixelate, and others)

#### Untested

- [ ] Radial Blur, X-Ray, Glitter, Fire, Advanced Fluid Simulation

#### Effect Runtime

- [-] Generic Wallpaper Engine shader effects (GLSL to Slang translation)
- [x] Effect chains
- [x] Multiple effect passes
- [x] Effect masks
- [x] Effect texture inputs
- [x] Constant shader values
- [x] Shader combo defines
- [-] Wallpaper Engine material support
- [ ] Named render targets (`_rt_FullFrameBuffer`, `_rt_*FrameBuffer`, `_rt_imageLayerComposite_*`)
- [ ] Custom effect compatibility

### Bloom & HDR

- [ ] Bloom
- [-] HDR rendering (float scene target with a soft highlight roll-off at present)
- [ ] Ultra HDR
- [ ] HDR bloom / threshold
- [ ] Bloom iterations
- [ ] Bloom scatter

### Particle Systems

- [-] Particle systems

#### General

- [x] Maximum particle count
- [x] Start-time warmup
- [ ] World-space particles
- [x] Perspective particles
- [-] Sprite sheets (grids and TEXS frame metadata read from the texture header)
- [ ] Frame blending
- [ ] Material lighting
- [-] Refraction
- [-] Blend modes

#### Renderers

- [-] Particle sprite rendering
- [-] Particle trail rendering (beam aspect ratio and camera-depth trails)
- [ ] Full particle renderer set

#### Emitters

- [-] Emitters
- [x] Multiple emitters
- [x] Sphere random
- [x] Box random
- [ ] Full emitter set

#### Initializers

- [x] Lifetime
- [x] Size
- [x] Velocity
- [x] Color
- [x] Alpha
- [x] Rotation
- [x] Angular velocity
- [ ] Full initializer set

#### Operators

- [x] Movement
- [x] Gravity
- [x] Drag
- [x] Alpha fade
- [x] Alpha oscillation
- [x] Size oscillation
- [x] Position oscillation
- [x] Turbulence
- [ ] Full operator set

#### Advanced Particle Features

- [x] Child particle systems (`static`, `eventfollow`, `eventspawn`, `eventdeath`, with origin / angles / scale, `maxcount` and `probability`)
- [ ] Control points
- [ ] Mouse-interactive particles
- [ ] Audio-responsive particles
- [-] Particle sprite-sheet animations
- [ ] Particle instance modifiers
- [-] Particle instance overrides (alpha, size, color, lifetime and speed per particle; `count` scales emission and `rate` is a system time scale; the per-override disable flags are honoured)

### Timeline Animations

- [-] Timeline animations
- [-] Property animations (alpha and effect constants)
- [x] Keyframes
- [ ] Bézier interpolation
- [x] Loop mode
- [x] Mirror mode
- [x] Single mode
- [ ] Animation playback rate
- [ ] Start paused
- [ ] Named animations
- [ ] Animation events
- [ ] SceneScript timeline control

### Puppet Warp

- [-] Puppet Warp
- [x] Puppet model (`.mdl` / MDLV) parsing
- [x] Puppet geometry / mesh
- [ ] Custom topology / vertex editing
- [x] Skeleton / bones
- [x] Bone weights (four influences per vertex)
- [x] Puppet animations (keyframed bone clips)
- [x] Multiple puppet animations (animation layers with rate and visibility)
- [x] Animation mixing (blend and additive layers)
- [x] Effects on puppet layers (run on the source texture before the mesh is assembled)
- [ ] Character sheets
- [ ] Texture channels
- [ ] Shape animations / blend shapes
- [ ] Bone constraints
- [ ] Spring simulation
- [ ] Rigid simulation
- [ ] Gravity
- [ ] Kinematic chains
- [ ] Inverse kinematics
- [ ] Bone blend rules
- [x] Attachment points (named sockets that child layers follow)
- [ ] Interactive bones
- [ ] SceneScript puppet control
- [ ] 3D perspective extrusion

### Lighting & Reflections

- [ ] 2D real-time lighting
- [ ] 2D reflections
- [ ] Normal maps
- [ ] Light sources
- [ ] Point lights
- [ ] Spot lights
- [ ] Directional lights
- [ ] Light animations
- [ ] Reflection maps

### 3D Scenes & Models

- [ ] 3D scenes
- [ ] FBX models
- [ ] OBJ models
- [ ] Perspective rendering
- [ ] Model hierarchy
- [ ] Multiple model materials

#### 3D Materials

- [ ] Albedo maps
- [ ] Normal maps
- [ ] Metallic maps
- [ ] Roughness maps
- [ ] Reflection maps
- [ ] Emissive maps
- [ ] Lighting
- [ ] Reflections
- [ ] Rim lighting
- [ ] Toon / cel shading
- [ ] Tint masks

#### Model Animation

- [ ] Skeletal animation
- [ ] Animation clips
- [ ] Multiple animation layers
- [ ] Animation blending
- [ ] Root motion
- [ ] Additional animation files
- [ ] Bone physics simulations
- [ ] Model attachments

#### 3D Rendering

- [ ] Shadows
- [ ] Point-light shadows
- [ ] Spot-light shadows
- [ ] Directional-light shadows
- [ ] Volumetric lighting
- [ ] Distance fog
- [ ] Height fog

#### Built-in 3D Shaders

- [ ] Fur shader
- [ ] Vegetation shader
- [ ] Chroma shader

### User Properties

- [ ] User properties
- [ ] Color properties
- [ ] Slider properties
- [ ] Checkbox properties
- [ ] Combo properties
- [ ] Text input properties
- [ ] Texture replacement properties
- [ ] User Shortcut properties
- [ ] Property groups
- [ ] Property display conditions
- [ ] Property bindings
- [ ] SceneScript property access

### Audio

- [x] Sound layers
- [x] Audio playback (MP3 / WAV / OGG via miniaudio)
- [x] Loop playback
- [x] Video wallpaper audio
- [x] Silent mode (`--no-audio` or `LWE_NO_AUDIO=1`; debug runs with `--no-ui` or `--diagnose` are always silent)
- [-] System audio capture and spectrum data (PulseAudio monitor; zeros when unavailable)
- [-] Audio-responsive effects (spectrum uniforms bound; depends on effect compatibility)
- [ ] Single playback mode / random playback mode
- [ ] Volume control UI
- [ ] Audio-responsive properties
- [ ] Audio-responsive particles

### Media Integration

- [ ] Currently-playing media integration
- [ ] Track title
- [ ] Artist
- [ ] Album metadata
- [ ] Media playback status
- [ ] Playback timeline
- [ ] Album artwork
- [ ] Album artwork color extraction

### SceneScript

- [-] SceneScript runtime (embedded QuickJS, text objects only)
- [-] ECMAScript-compatible scripting
- [-] Property scripts (text objects)
- [ ] `init()`
- [x] `update()`
- [ ] Scene access
- [ ] Layer access
- [ ] Effect access
- [ ] Dynamic layer creation
- [ ] Dynamic asset registration
- [ ] Timeline control
- [ ] Puppet animation control
- [ ] 3D model animation control
- [ ] Particle control
- [ ] Sound control
- [ ] Cursor events
- [ ] Audio data
- [-] User properties (`scriptproperties` overrides)
- [x] Time / date APIs
- [-] Media integration (`mediaPropertiesChanged` hook; no live media data)
- [ ] Local persistent storage
- [ ] Shared script state
- [ ] Dynamic 2D / 3D model creation
- [ ] WEColor module
- [ ] WEMath module
- [ ] WEVector module

### Interaction

- [x] Mouse position
- [x] Mouse-driven parallax
- [ ] Solid layer hit testing
- [ ] Cursor enter
- [ ] Cursor leave
- [ ] Cursor move events
- [ ] Cursor down
- [ ] Cursor up
- [ ] Cursor click
- [ ] Interactive effects
- [ ] Interactive particles
- [ ] Interactive Puppet Warp

### RGB Hardware

- [ ] RGB hardware integration
- [ ] Corsair iCUE
- [ ] Razer Chroma
- [ ] RGB source layers
- [ ] Composition-layer RGB output
- [ ] Web wallpaper RGB API

### Shader Programming

- [-] Wallpaper Engine shader loading
- [-] Wallpaper Engine GLSL compatibility
- [-] GLSL to SPIR-V translation
- [x] Runtime Slang compilation
- [x] Persistent SPIR-V cache (shared across launches, written atomically)
- [-] Built-in shader uniforms
- [-] Built-in texture bindings
- [-] Shader combo system
- [ ] Full vertex attribute support
- [ ] Custom shaders
- [ ] Full custom effect compatibility

### Texture & Asset Formats

- [x] Wallpaper Engine `.tex`
- [x] RGBA8
- [x] R8 grayscale
- [x] BC1 / DXT1
- [x] BC2 / DXT3
- [x] BC3 / DXT5
- [x] LZ4-compressed texture data
- [x] Embedded PNG textures
- [x] Multi-image `.tex` resources
- [-] Wallpaper Engine materials
- [-] Wallpaper Engine shaders
- [-] Wallpaper Engine particle definitions
- [ ] Wallpaper Engine model formats
- [ ] Full `.tex` format compatibility

### Wallpaper Packages & Projects

- [x] Wallpaper Engine project folders
- [x] `scene.pkg` extraction
- [x] Read `scene.pkg` in place from a memory map (release builds)
- [x] Standalone package input
- [x] `scene.json` parsing
- [x] `project.json` video target detection
- [x] Authored scene resolution
- [x] Orthographic projection resolution
- [x] Clear color

### Web Wallpapers

- [x] HTML
- [x] CSS
- [x] JavaScript
- [x] Local web assets
- [-] Web user properties (project defaults only)
- [ ] File properties
- [ ] Directory properties
- [-] Audio visualization API (silence only)
- [-] Media integration API (no-op listeners)
- [ ] RGB API
- [ ] User-configurable FPS API
- [ ] Web video playback

### Video Wallpapers

- [x] Video wallpaper playback
- [x] MP4 wallpapers
- [x] WebM wallpapers
- [x] MKV wallpapers
- [x] AVI wallpapers
- [x] MOV wallpapers
- [x] WMV wallpapers
- [x] Looping
- [x] Audio (`--no-audio` or `LWE_NO_AUDIO=1` to silence)
- [x] Playback rate, volume and fit (fit / fill) from `project.json` properties
- [x] Pause while the window is iconified or suspended
- [x] Decode path logged at startup (zero-copy, CPU copy or software)
- [x] Streams from disk by default; `--video-ram` keeps one shared copy in RAM
- [x] Videos embedded in `.tex` files, including ones inside a mapped package
- [-] Playback controls
- [x] FFmpeg software decoding
- [x] VA-API hardware decoding
- [-] GPU-native VA-API / DMA-BUF video textures (including layer-shell surfaces, with a guarded fallback when import fails)
- [x] Automatic software decode fallback
- [-] Video user properties (rate, volume and fit defaults only; no live editing)

### Application Wallpapers

- [ ] Application wallpapers
- [ ] Native executable wallpaper processes
- [ ] Wallpaper process lifecycle
- [ ] Embedded / desktop window placement

### Runtime / Desktop Integration

- [ ] Desktop wallpaper placement
- [ ] Multi-monitor rendering
- [ ] Per-monitor wallpapers
- [ ] Spanned wallpapers
- [ ] Cloned wallpapers
- [ ] Pause / resume runtime
- [ ] Mute / unmute
- [ ] FPS limits
- [ ] Fullscreen application detection
- [ ] Screensaver mode
- [-] Runtime command-line controls
- [x] GPU enumeration
- [x] GPU selection (also honoured on the Wayland layer surface)
- [x] Package extraction CLI
- [x] Debug sandbox
- [x] Debug scene tree keyboard navigation, wallpaper path copy and sound-layer mute toggle (cursor calls are skipped on Wayland layer surfaces)
- [x] Debug render diagnostics
