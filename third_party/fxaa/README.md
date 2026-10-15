# FXAA 3.11

Post-process anti-aliasing by Timothy Lottes, as shipped by NVIDIA.

| Item | Value |
| --- | --- |
| File | `FXAA3_11.h`, unmodified |
| Author | Timothy Lottes; NVIDIA CORPORATION (copyright 2014-2015) |
| License | BSD 3-clause, see `LICENSE` |
| Source | Fork `lyntel/GraphicsSamples` at commit `3d30817ebeeade64fe6a4fc3aa1fe4265c29b6fd`, path `samples/es3-kepler/FXAA/FXAA3_11.h` |

The engine sets the configuration macros in its own shader prefix (`src/shared/graphics/render_init.cpp`): `FXAA_PC`, `FXAA_GLSL_130`, `FXAA_QUALITY__PRESET 12`, `FXAA_GREEN_AS_LUMA 1` (luma read from the green channel, so the RGB scene needs no luma in alpha).

One build-time patch (`xmake.lua`): `#define FxaaTex sampler2D` becomes `#define FxaaTex LweCombinedSampler2D`, because the engine passes samplers as that type. The file on disk is unchanged.

The original NVIDIA repository is archived, so this copy comes from a fork that keeps the same file. Its header matches the original's license text.
