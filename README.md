# Linux Wallpaper Engine

Linux renderer for Wallpaper Engine projects.

## Requirements

- [xmake](https://xmake.io/)
- GCC or Clang with C++ support
- Vulkan development libraries and a working Vulkan GPU driver
- [Slang](https://github.com/shader-slang/slang)
- A local Wallpaper Engine installation with its original `assets/` data (required at runtime; set `WALLPAPER_ENGINE_PATH` or `engine_path` in `config.json`)
- X11 development libraries (`libX11`, `libXcursor`, `libXi`)
- FFmpeg development libraries (`libavformat`, `libavcodec`, `libavutil`, `libswscale`)

On Arch / CachyOS:

```bash
sudo pacman -S xmake vulkan-icd-loader shader-slang libx11 libxcursor libxi ffmpeg
```

## Build and run

| Goal                                                 | Command                                                     |
| ---------------------------------------------------- | ----------------------------------------------------------- |
| Build the default configuration                      | `xmake`                                                     |
| Run a wallpaper project directory or `.pkg` file     | `xmake run linux-wallpaperengine "/path/to/wallpaper"`      |
| Configure and run a debug build                      | `xmake f -m debug` then `xmake run linux-wallpaperengine`   |
| Configure and run a release build                    | `xmake f -m release` then `xmake run linux-wallpaperengine` |
| Clean build outputs                                  | `xmake clean`                                               |
| Validate formatting and static analysis              | `xmake check`                                               |
| Format source files                                  | `xmake format`                                              |
| Validate, build, and launch the debug effect sandbox | `xmake sandbox`                                             |

Build outputs are written to `bin/<mode>/`.

### Render Diagnostics (Debug Mode)

When running in debug mode (`xmake f -m debug`), render diagnostics automatically run at frame 100 to capture the render pipeline state:
- Captures pass images, scene stages, render graphs, shader code, and uniforms.
- Output is written to `./diagnostics/<wallpaper_name>/` (named after the active wallpaper or project title).
- If a diagnostics folder for that wallpaper already exists, it is automatically replaced with fresh capture data.

## FEATURE SUPPORT

> Runtime compatibility with Wallpaper Engine wallpapers.
>
> - [x] Supported
> - [-] Partial / Incomplete
> - [ ] Not supported yet

### Wallpaper Types

- [-] Scene wallpapers
- [ ] Video wallpapers
- [ ] Web wallpapers
- [ ] Application wallpapers

### Scene Layers & Assets

- [x] Image layers
- [-] Particle system layers
- [ ] Text layers
- [ ] Sound layers
- [ ] Light layers
- [ ] 3D model layers
- [ ] Composition layers
- [ ] Fullscreen / effect layers
- [-] Layer hierarchy
- [x] Layer visibility
- [x] Layer position
- [x] Layer scale
- [ ] Layer rotation
- [-] Parent transforms
- [-] Layer blending
- [ ] Layer attachments

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

#### Animation Effects

- [ ] Foliage Sway
- [ ] Iris Movement
- [ ] Pulse
- [ ] Cloud Motion
- [ ] Scroll
- [ ] Shake
- [ ] Spin
- [ ] Swing
- [ ] Twirl
- [ ] Water Flow
- [ ] Water Ripple
- [x] Water Waves

#### Blur Effects

- [ ] Blur
- [ ] Blur Precise
- [ ] Motion Blur
- [ ] Radial Blur

#### Interactive Effects

- [ ] Cursor Ripple
- [ ] Advanced Fluid Simulation
- [x] Depth Parallax
- [ ] X-Ray

#### Colorization Effects

- [ ] Blend
- [ ] Blend Gradient
- [ ] Chromatic Aberration
- [ ] Clouds
- [ ] Color Key
- [ ] Film Grain
- [ ] Glitter
- [ ] Shimmer
- [ ] Fire
- [ ] Light Shafts
- [ ] Nitro
- [ ] Opacity
- [ ] Reflection
- [ ] Tint
- [ ] VHS
- [ ] Water Caustics

#### Distortion Effects

- [ ] Fisheye
- [ ] Perspective
- [ ] Refraction
- [ ] Skew
- [ ] Transform

#### Enhancement Effects

- [ ] Edge Detection
- [ ] God Rays
- [ ] Local Contrast
- [ ] Shine

#### Effect Runtime

- [-] Generic Wallpaper Engine shader effects
- [x] Effect chains
- [x] Multiple effect passes
- [x] Effect masks
- [x] Effect texture inputs
- [x] Constant shader values
- [x] Shader combo defines
- [-] Wallpaper Engine material support
- [ ] Full built-in effect compatibility
- [ ] Custom effect compatibility

### Bloom & HDR

- [ ] Bloom
- [ ] HDR rendering
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
- [ ] Perspective particles
- [ ] Sprite sheets
- [ ] Frame blending
- [ ] Material lighting
- [ ] Refraction
- [-] Blend modes

#### Renderers

- [-] Particle sprite rendering
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

- [x] Child particle systems
- [ ] Control points
- [ ] Mouse-interactive particles
- [ ] Audio-responsive particles
- [ ] Particle sprite-sheet animations
- [ ] Particle instance modifiers
- [-] Particle instance overrides

### Timeline Animations

- [ ] Timeline animations
- [ ] Property animations
- [ ] Keyframes
- [ ] Bézier interpolation
- [ ] Loop mode
- [ ] Mirror mode
- [ ] Single mode
- [ ] Animation playback rate
- [ ] Start paused
- [ ] Named animations
- [ ] Animation events
- [ ] SceneScript timeline control

### Puppet Warp

- [ ] Puppet Warp
- [ ] Puppet geometry / mesh
- [ ] Custom topology / vertex editing
- [ ] Skeleton / bones
- [ ] Bone weights
- [ ] Puppet animations
- [ ] Multiple puppet animations
- [ ] Animation mixing
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
- [ ] Attachment points
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

- [ ] Sound layers
- [ ] Audio playback
- [ ] MP3 playback
- [ ] WAV playback
- [ ] Loop playback
- [ ] Single playback
- [ ] Volume control
- [ ] Play / pause / stop
- [ ] System audio capture
- [ ] Audio spectrum data
- [ ] Audio-responsive properties
- [ ] Audio-responsive effects
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

- [ ] SceneScript runtime
- [ ] ECMAScript-compatible scripting
- [ ] Property scripts
- [ ] `init()`
- [ ] `update()`
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
- [ ] User properties
- [ ] Time / date APIs
- [ ] Media integration
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
- [x] Standalone package input
- [x] `scene.json` parsing
- [x] Authored scene resolution
- [x] Orthographic projection resolution
- [x] Clear color

### Web Wallpapers

- [ ] HTML
- [ ] CSS
- [ ] JavaScript
- [ ] Local web assets
- [ ] Web user properties
- [ ] File properties
- [ ] Directory properties
- [ ] Audio visualization API
- [ ] Media integration API
- [ ] RGB API
- [ ] User-configurable FPS API
- [ ] Web video playback

### Video Wallpapers

- [ ] Video wallpaper playback
- [ ] MP4 wallpapers
- [ ] Looping
- [ ] Audio
- [ ] Playback controls
- [ ] Hardware video decoding
- [ ] Video user properties

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
- [ ] Runtime command-line controls
