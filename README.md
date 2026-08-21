# Linux Wallpaper Engine

Linux renderer for Wallpaper Engine projects.

## Requirements

- [xmake](https://xmake.io/)
- GCC or Clang with C++ support
- Vulkan development libraries and a working Vulkan GPU driver
- [Slang](https://github.com/shader-slang/slang)
- X11 development libraries (`libX11`, `libXcursor`, `libXi`)

On Arch / CachyOS:

```bash
sudo pacman -S xmake vulkan-icd-loader shader-slang libx11 libxcursor libxi
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

# Feature support

Runtime compatibility with Wallpaper Engine wallpapers.

- `[x]` Supported
- `[-]` Partial or incomplete
- `[ ]` Not supported yet

## Wallpaper types

- [-] Scene wallpapers
- [ ] Video wallpapers
- [ ] Web wallpapers
- [ ] Application wallpapers

## Scene layers and assets

- [x] Image layers
- [-] Particle system layers
- [ ] Text layers
- [ ] Sound layers
- [ ] Light layers
- [ ] 3D model layers
- [ ] Composition layers
- [ ] Fullscreen / effect layers
- [-] Layer hierarchy
- [x] Layer visibility, position, and scale
- [ ] Layer rotation and attachments
- [-] Parent transforms and layer blending

## Camera and parallax

- [x] Orthographic scenes
- [x] Camera parallax, mouse influence, amount, and delay
- [x] Per-layer `parallaxDepth`, horizontal / vertical depth, and parent propagation
- [x] `disablepropagation`, shader `g_ParallaxPosition`, and Depth Parallax integration
- [ ] Perspective scenes, camera assets, field of view, multiple cameras, paths, and shake

## Image effects

### Supported

- [x] Water Waves
- [x] Depth Parallax
- [x] Effect chains, multiple passes, masks, texture inputs, constant values, and shader combo defines

### Partial

- [-] Generic Wallpaper Engine shader effects
- [-] Wallpaper Engine material support

### Not supported yet

- [ ] Animation: Foliage Sway, Iris Movement, Pulse, Cloud Motion, Scroll, Shake, Spin, Swing, Twirl, Water Flow, Water Ripple
- [ ] Blur: Blur, Blur Precise, Motion Blur, Radial Blur
- [ ] Interactive: Cursor Ripple, Advanced Fluid Simulation, X-Ray
- [ ] Colorization: Blend, Blend Gradient, Chromatic Aberration, Clouds, Color Key, Film Grain, Glitter, Shimmer, Fire, Light Shafts, Nitro, Opacity, Reflection, Tint, VHS, Water Caustics
- [ ] Distortion: Fisheye, Perspective, Refraction, Skew, Transform
- [ ] Enhancement: Edge Detection, God Rays, Local Contrast, Shine
- [ ] Full built-in and custom effect compatibility

## Bloom and HDR

- [ ] Bloom, HDR rendering, Ultra HDR, threshold, iterations, and scatter

## Particle systems

### Supported

- [x] Maximum particle count and start-time warmup
- [x] Multiple emitters, sphere random, and box random
- [x] Initializers: lifetime, size, velocity, color, alpha, rotation, and angular velocity
- [x] Operators: movement, gravity, drag, alpha fade, alpha oscillation, size oscillation, position oscillation, and turbulence
- [x] Child particle systems

### Partial

- [-] Particle systems, sprite rendering, emitters, blend modes, and instance overrides

### Not supported yet

- [ ] World-space and perspective particles, sprite sheets, frame blending, material lighting, and refraction
- [ ] Full particle renderer, emitter, initializer, and operator sets
- [ ] Control points, mouse interaction, audio response, sprite-sheet animation, and instance modifiers

## Timeline animations

- [ ] Timeline animations, property animations, keyframes, Bézier interpolation, loop / mirror / single modes, playback rate, start paused, named animations, events, and SceneScript control

## Puppet Warp

- [ ] Puppet Warp, geometry, topology, skeletons, weights, animations, mixing, character sheets, texture channels, shape animations, constraints, spring / rigid simulation, gravity, kinematic chains, inverse kinematics, blend rules, attachment points, interaction, SceneScript control, and 3D perspective extrusion

## Lighting, reflections, and 3D

- [ ] 2D lighting, reflections, normal maps, light sources, light animations, and reflection maps
- [ ] 3D scenes, FBX / OBJ models, perspective rendering, model hierarchy, and multiple materials
- [ ] 3D materials: albedo, normal, metallic, roughness, reflection, emissive, lighting, rim lighting, toon shading, and tint masks
- [ ] Model animation, shadows, volumetric lighting, distance fog, height fog, and built-in Fur / Vegetation / Chroma shaders

## User properties, audio, and media

- [ ] User properties, property groups, display conditions, bindings, and SceneScript property access
- [ ] Audio playback, MP3 / WAV, playback controls, system capture, spectrum data, and audio-reactive content
- [ ] Currently-playing media integration, metadata, playback status, timeline, artwork, and artwork color extraction

## SceneScript and interaction

- [ ] SceneScript runtime, ECMAScript compatibility, property scripts, lifecycle methods, scene/layer/effect access, dynamic content, time/date, storage, shared state, and built-in modules
- [x] Mouse position and mouse-driven parallax
- [ ] Hit testing, cursor events, interactive effects, particles, and Puppet Warp

## RGB hardware

- [ ] RGB hardware integration, Corsair iCUE, Razer Chroma, RGB source layers, composition output, and the web wallpaper RGB API

## Shader programming

- [x] Runtime Slang compilation
- [-] Wallpaper Engine shader loading, GLSL compatibility, GLSL-to-SPIR-V translation, built-in uniforms, texture bindings, and shader combo system
- [ ] Full vertex attribute support, custom shaders, and full custom effect compatibility

## Textures and assets

- [x] Wallpaper Engine `.tex`, RGBA8, R8 grayscale, BC1 / DXT1, BC2 / DXT3, BC3 / DXT5, LZ4 data, embedded PNG textures, and multi-image `.tex` resources
- [-] Wallpaper Engine materials, shaders, and particle definitions
- [ ] Wallpaper Engine model formats and full `.tex` compatibility

## Wallpaper packages and projects

- [x] Wallpaper Engine project folders and standalone package input
- [x] `scene.pkg` extraction, `scene.json` parsing, authored and orthographic resolution, and clear color

## Web, video, and application wallpapers

- [ ] Web wallpapers: HTML, CSS, JavaScript, local assets, properties, audio visualization, media, RGB, FPS, and video playback
- [ ] Video wallpapers: playback, MP4, looping, audio, controls, hardware decoding, and user properties
- [ ] Application wallpapers: executable processes, lifecycle, and desktop placement

## Runtime and desktop integration

- [ ] Desktop placement, multi-monitor rendering, per-monitor / spanned / cloned wallpapers, pause / resume, mute, FPS limits, fullscreen detection, screensaver mode, and command-line controls
