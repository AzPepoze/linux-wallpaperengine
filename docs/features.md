# Features

Back to the [README](../README.md).

This page lists the features of Wallpaper Engine, and which ones this project supports. For each item, see [Feature details](features-detail.md).

- [x] = supported
- [x] Name (Partial) = supported for many cases, with some parts missing
- [ ] = not yet

## Wallpaper Engine features

- [x] Wallpaper types (Partial)
  - [x] Scene wallpapers (Partial)
    - [x] Image, text, particle and sound layers
    - [x] Effects and camera parallax
    - [ ] Light layers and 3D models
  - [x] Video wallpapers (Partial)
    - [x] mp4, webm, mkv and more
    - [x] Hardware video decoding
    - [ ] Live video property edits
  - [x] Web wallpapers (Partial)
    - [x] Needs the web feature (Qt6 WebEngine)
  - [ ] Application wallpapers
- [x] Layers (Partial)
  - [x] Image layers (Partial)
    - [x] Size, position, scale, rotation around one axis, alpha, tint
    - [ ] Rotation around the other two axes
  - [x] Text layers (Partial)
    - [x] Fonts, size, color, alignment
    - [ ] Anchoring to screen edges
  - [x] Sound layers
    - [x] Single, loop and random playback
- [x] Effects and shaders (Partial)
  - [x] Most common effects work
  - [ ] Some effects fail to compile
- [x] Bloom and HDR (Partial)
  - [x] Bloom
  - [ ] Some HDR variants
- [x] Camera (Partial)
  - [x] Parallax
  - [x] Shake
  - [x] Zoom
  - [ ] Perspective cameras
- [x] Particles (Partial)
  - [x] Common emitters, movement and trails
  - [ ] Some operators
- [x] Animation (Partial)
  - [x] Opacity animation
  - [ ] Position, scale and color animation
- [x] Puppet warp (Partial)
  - [x] Skinned models and clips
  - [ ] Blend shapes and physics
- [ ] Lighting and 3D
  - [ ] 2D lighting and normal maps
  - [ ] 3D models
- [x] Scripts, SceneScript (Partial)
  - [x] Most common scripts
  - [ ] Some APIs are stubs
- [x] User properties (Partial)
  - [x] Sliders, colors, checkboxes and text
  - [x] Most changes apply live
  - [ ] Editor groups and conditions
- [x] Audio
  - [x] Sound playback
  - [x] Audio visualizer (Partial)
- [x] Media info, MPRIS (Partial)
  - [x] Song title, artist and album
  - [ ] Web album art links
- [x] Transitions between wallpapers
  - [x] 27 transition effects
  - [x] Crossfade with audio
- [x] Mouse (Partial)
  - [x] Follows the mouse
  - [x] Click and hover on layers
- [ ] Keyboard, touch and gamepad
- [x] Package files and textures
  - [x] `.pkg` files
  - [x] `.tex` textures
- [x] Command line
- [x] GPU selection
- [ ] Multi-monitor in one process
- [ ] RGB hardware

## Platforms

- [x] Desktop background on Wayland (Partial)
  - [x] Tested on Hyprland only
- [x] Desktop window on X11 (Partial)
  - [x] Tested on a test display only
- [x] Normal window on any desktop
- [x] Arch Linux (tested)
- [ ] Other distros (not tested)

Note: a checked box means the feature runs and is used. It does not mean the output matches Wallpaper Engine pixel for pixel.

## More

- [Feature details](features-detail.md): the item-by-item list
- [Wallpaper Engine assets](wallpaper-engine-assets.md): the assets we load from Wallpaper Engine

When a feature changes, update both this page and the details page in the same commit.
