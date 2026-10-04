# Web wallpaper offscreen rendering — design

**Status:** design validated by spike (2026-10-05); tier A not yet implemented.
**Related:** `docs/superpowers/plans/2026-10-05-web-offscreen-render-tier-a.md` (tier A implementation), `docs/features.md` (web wallpaper entry).

## Problem

The web wallpaper helper (`src/wallpaper/web/web_renderer_main.cpp`) renders the page with a
`QWebEngineView` widget under the `offscreen` QPA plugin and calls `view_.grab()` every frame,
copying the result into a shared-memory BGRA buffer the engine uploads. Measured on the
reference machine (RX 6600 / RADV, 1920x1080, 60 fps, a real workshop wallpaper):

- helper process: **~81% of one core, ~13.5 ms/frame**.

`grab()` synchronously re-renders the whole widget and reads it back on the GUI thread, so it
also blocks input handling. This is the lag the offscreen rewrite targets.

## Spike findings (2026-10-05)

These were measured with a throwaway probe (not kept); they correct several assumptions:

1. **`QQuickRenderControl` + QML `WebEngineView` does render offscreen on Qt 6.11.2.** The
   `WebEngineView` documentation says it "is not rendered correctly" under a render control, but
   the real wallpaper rendered correctly (player widget, clock, date). The initial blank frames
   were a timing artifact: the test loop finished in milliseconds, before the page had loaded.
   Pages need real wall-clock time and the Wallpaper Engine shim before they paint.
2. **The `offscreen` QPA plugin cannot be used.** It forces Qt Quick's *software* adaptation, so
   `QQuickRenderControl::initialize()` fails with "QRhi is only compatible with default
   adaptation". The helper must run on the session platform (Wayland or X11); render-control
   windows are never shown, so nothing appears on the desktop.
3. **Render control + per-frame CPU readback is ~3.6x cheaper than `grab()`:** ~3.7 ms/frame
   (~19–22% of a core at 60 fps) versus ~13.5 ms/frame. This is most of the win, with no engine
   changes.
4. **EGL DMA-BUF export is blocked on this Mesa.** `eglCreateImageKHR(EGL_GL_TEXTURE_2D_KHR)`
   segfaults in Mesa 26.2.3, reproduced with a standalone 40-line EGL test with no Qt involved.
   The zero-copy transport therefore cannot use EGL today.
5. **The Vulkan external-memory transport works on all three devices.** A BGRA8 linear `VkImage`
   allocated with `VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT` exported with `vkGetMemoryFdKHR`
   and re-imported on a second `VkDevice` of the same physical device succeeds on **AMD RADV
   (RX 6600)**, **Intel ANV (UHD 730)** and **llvmpipe**. This supersedes the EGL path.
6. **Qt's Vulkan RHI can render WebEngine into that external image.** With
   `QSGRendererInterface::Vulkan`, a `QQuickRenderControl` + QML `WebEngineView` rendering into an
   externally-allocated BGRA8 linear `VkImage` (set via `QQuickRenderTarget::fromVulkanImage`)
   painted the real wallpaper, and the image exported as a DMA-BUF. Verified on AMD, Intel and
   llvmpipe. The device extensions are requested through
   `QQuickGraphicsConfiguration::setDeviceExtensions({VK_KHR_external_memory_fd,
   VK_EXT_external_memory_dma_buf})`; the `VkDevice` is taken from the RHI via
   `QSGRendererInterface::getResource(DeviceResource)`.

## Approach ladder

The three transports are shipped together and selected at runtime, best first, each falling back
to the next when its prerequisites are missing. This is what keeps the helper working across the
wide range of GPUs, drivers and sessions an open-source project meets.

| Order | Tier | Transport | Cost | Requires |
|---|---|---|---|---|
| 1 | B (follow-up) | GPU texture shared as a DMA-BUF into the engine's Vulkan device | 0 CPU copies | Same-GPU DMA-BUF export/import on both processes |
| 2 | A (this plan) | `QQuickRenderControl` offscreen render, CPU readback into the existing shm BGRA frame | one readback | A working GL/EGL context on the session platform |
| 3 | fallback (current) | `QWebEngineView` widget + `grab()` under `offscreen` QPA | full widget render + readback | none (always works) |

Tier A keeps the current IPC (`WebFrameBuffer` + control socket) exactly as it is; only the
helper's rendering changes. Tier B adds a second transport alongside shm and does not replace it.

## Portability

The ladder exists because offscreen rendering is the part of this project most sensitive to the
platform and GPU.

- **Session platform.** Prefer the platform Qt already selected from the environment (Wayland,
  then X11). Never force `offscreen`: it has no GL adaptation. If neither a GL context nor
  `QQuickRenderControl::initialize()` succeeds, fall to the widget backend.
- **GPU vendor (tier A).** Tier A is vendor-neutral: it renders through Qt's own GL context and
  performs one CPU readback, so AMD (RADV), Intel (ANV/i915), NVIDIA (proprietary/nouveau) and
  software rasterizers all behave the same.
- **GPU vendor (tier B).** This is where vendor support differs and where the fallback matters:
  AMD and Intel export/import DMA-BUFs with DRM format modifiers; NVIDIA proprietary supports
  export but with a much smaller modifier set (linear is the safe choice). The engine's existing
  DMA-BUF import (`VideoImportCache`) is NV12/YCbCr-specific and cannot be reused as-is for a
  BGRA web frame.
- **Multi-GPU.** Tier B requires both processes to use the *same* Vulkan device; a helper on the
  iGPU and an engine on the dGPU cannot share the buffer, and the engine already logs and bails
  on cross-adapter DMA-BUF imports. Detect same-device and fall back to tier A. The helper
  inherits the engine's selection environment (`MESA_VK_DEVICE_SELECT`, `DRI_PRIME`), so both
  processes start on the same device by construction.
- **Helper must not hard-fail.** A failed tier falls through, never aborts the wallpaper.

## Tier A design

**Rendering.** A `QQuickRenderControl` drives an invisible `QQuickWindow` whose root item is a QML
`WebEngineView`. Each frame: `polishItems()`, `beginFrame()`, `sync()`, `render()`, `endFrame()`
(Qt 6 requires the begin/end pair). The window renders into a `QOpenGLFramebufferObject` set via
`QQuickRenderTarget::fromOpenGLTexture`. The FBO is read back with `toImage()` and copied into the
shm `WebFrameBuffer` (ARGB32 on little-endian is BGRA in memory, matching `pixel_format = 1`).

**Shim and properties.** The Wallpaper Engine API shim is injected into the default profile at
`DocumentCreation` so page scripts see it. Because the QML `WebEngineView` exposes no `page`
property to C++, the shim source embeds the user-properties JSON and the general properties
(`{fps}`) and applies them itself on `DOMContentLoaded` and `load`, instead of the helper calling
`runJavaScript` after `loadFinished`.

**Input.** Control-socket messages are decoded to a small Qt-free `RenderEvent` and forwarded as
`QMouseEvent`/`QWheelEvent` to the `QQuickWindow` (not a widget). The root item gets active focus
so the page receives keyboard input.

**Backend selection.** `main()` prepares the shm and ctrl socket, then tries to construct the
render-control backend; on any failure it constructs the widget backend. Both satisfy the same
small interface, so the loop and IPC code are shared.

**File split.** `web_renderer_main.cpp` keeps argument parsing, shm setup and backend selection;
`web_render_control.{h,cpp}` holds the render-control backend; `web_widget_backend.{h,cpp}` holds
the extracted current backend; `web_renderer_shared.{h,cpp}` holds the Qt-free, unit-testable
pieces (shim builder, input decode, frame publish).

## Tier B design (follow-up)

Render offscreen on the GPU, then hand the frame to the engine without a CPU copy:

- Create the Qt Quick render target on a Vulkan image we own, allocated with
  `VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT`, and export its fd with `vkGetMemoryFdKHR`.
  This bypasses Mesa's broken EGL export path entirely.
- Pass the fd + format/modifier/stride/offset over the control socket.
- In the engine, import it as a sampled `VK_FORMAT_B8G8R8A8_UNORM` image (linear tiling for the
  widest driver support) and sample it in the existing image pipeline. This is a new import path
  parallel to the NV12 video path, sharing its scaffolding.
- Synchronise producer/consumer with a small ring of buffers plus a fence or `VK_EXTERNAL` semaphore
  exposed through the shared header; never sample a buffer while it is being written.
- Fall back to tier A when: export is unavailable, the engine's device differs from the helper's,
  or the import fails.

Tier B is gated behind its own runtime capability check, mirroring `gpu_set_zero_copy_video_supported`.
The helper side (external image + RHI render + export) is **validated** on AMD, Intel and llvmpipe; the
remaining work is the engine-side import/sampling path and the cross-process synchronisation, which is
the subject of a separate plan.

## Testing

- **Unit (no Qt):** shim/properties builder and control-message decoding live in
  `web_renderer_shared` and are covered by `tests/web_renderer_shared_test.cpp`.
- **Integration (manual, documented):** render one frame from a fixture page and confirm it is
  non-blank; re-measure helper CPU against the `grab()` baseline.
- **Fallback:** force `initialize()` to fail and confirm the widget backend is selected and still
  publishes frames.

## Out of scope

- Audio, media integration and input beyond mouse move/down/up/scroll.
- Replacing the shm contract; tier B adds a transport, shm stays the floor.
- The EGL DMA-BUF export path (blocked by Mesa; the Vulkan path supersedes it).
