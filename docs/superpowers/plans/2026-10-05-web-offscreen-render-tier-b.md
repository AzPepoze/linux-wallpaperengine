# Web Offscreen Renderer (Tier B: zero-copy DMA-BUF) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stream the web helper's rendered frame to the engine as a DMA-BUF sampled directly by the engine's Vulkan device, with tier A's CPU readback as the automatic fallback.

**Architecture:** The helper renders Qt Quick into a small ring of externally-allocated BGRA8 `VkImage`s and exports each as a DMA-BUF fd. The engine imports the fds as `VkImage`s on its own device and blits them into an `sg_image` using the same scaffolding as the video zero-copy path (`s_zc` pipeline, `_sg_vk_staging_copy_begin/end`). A shared-memory counter pair coordinates the ring. A runtime capability check gates the path; any failure falls back to tier A.

**Tech Stack:** C++17, Qt 6.11 Vulkan RHI, Vulkan external memory (`VK_KHR_external_memory_fd`, `VK_EXT_external_memory_dma_buf`), sokol_gfx Vulkan internals, POSIX shared memory.

**Spec:** `docs/superpowers/specs/2026-10-05-web-offscreen-render-design.md`

## Global Constraints

- The core engine must not gain a Qt dependency; all Qt code stays in the helper target.
- The tier-A shm path remains the floor. Tier B is additive and must not regress it.
- Reuse the video zero-copy scaffolding (`s_zc`, `_sg_vk_staging_copy_begin/end`, the fullscreen-triangle shader) rather than writing a parallel blit.
- Both processes must use the same `VkPhysicalDevice`; detect a mismatch and fall back.
- `K = 3` ring buffers. The helper must never write a buffer the engine is still sampling.
- No buffer may be freed while the engine might still be reading it.
- `xmake test` stays green; new Qt-free logic is unit-tested.

## Review Focus

- **Cross-GPU (helper on iGPU, engine on dGPU):** import must fail cleanly and the engine must fall back to tier A, not render garbage.
- **Driver without `VK_EXT_external_memory_dma_buf`:** capability check must reject tier B before allocating anything.
- **Engine frame slower than the helper:** the ring must not overwrite an in-flight buffer; the helper blocks rather than tears.
- **Teardown with a buffer in flight:** no use-after-free when the engine exits or switches wallpapers.
- **Surface resize:** dimensions and exported fds must be refreshed, not stale.

---

### Task 1: DMA-BUF descriptor and sync in the shared header

**Files:**
- Modify: `src/wallpaper/web/web_ipc.h`
- Test: `tests/web_renderer_shared_test.cpp` (extend)

**Interfaces:**
- Produces:
  - `enum { WEB_DMABUF_MAX_BUFFERS = 3 };`
  - `struct WebDmaBufBuffer { int32_t fd; uint32_t fourcc; uint64_t modifier; uint32_t stride, offset, width, height; };`
  - New fields on `WebFrameBuffer`: `uint32_t transport; uint32_t buffer_count; WebDmaBufBuffer buffers[WEB_DMABUF_MAX_BUFFERS]; std::atomic<uint64_t> published_frame; std::atomic<uint32_t> published_index; std::atomic<uint64_t> consumed_frame;` (`transport`: `0` = shm, `1` = dmabuf).
  - `bool web_renderer::publishDmaBuf(WebFrameBuffer* frame, uint32_t index)` — publishes buffer `index` with a release store and bumps `published_frame`.
  - `int web_renderer::acquireDmaBuf(const WebFrameBuffer* frame)` — returns the buffer index the helper may write next, blocking (`sched_yield`) while `published_frame - consumed_frame >= K`.

- [ ] **Step 1: Write the failing test**

Add to `tests/web_renderer_shared_test.cpp`:
```cpp
    // DMA-BUF publish/acquire ring.
    std::vector<uint8_t> storage2(sizeof(WebFrameBuffer));
    auto* fb2 = reinterpret_cast<WebFrameBuffer*>(storage2.data());
    std::memset(storage2.data(), 0, storage2.size());
    fb2->buffer_count = WEB_DMABUF_MAX_BUFFERS;
    pthread_mutex_init(&fb2->mutex, nullptr);
    const int a0 = web_renderer::acquireDmaBuf(fb2);
    CHECK(a0 == 0);
    web_renderer::publishDmaBuf(fb2, a0);
    CHECK(fb2->published_frame.load() == 1);
    CHECK(fb2->published_index.load() == 0);
    pthread_mutex_destroy(&fb2->mutex);
```
(Note: `acquireDmaBuf` must not block here because `published_frame - consumed_frame == 0 < K`.)

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build web_renderer_tests && ./bin/debug/web_renderer_tests`
Expected: build fails — `publishDmaBuf`/`acquireDmaBuf` not declared.

- [ ] **Step 3: Implement the header fields and the two functions**

`publishDmaBuf`: `frame->published_index.store(index, std::memory_order_relaxed); frame->published_frame.fetch_add(1, std::memory_order_release);` `acquireDmaBuf`: compute `next = published_index + 1 (mod K)`; while `published_frame - consumed_frame >= K` call `sched_yield()`; return `next`.

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build web_renderer_tests && ./bin/debug/web_renderer_tests`
Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/wallpaper/web/web_ipc.h tests/web_renderer_shared_test.cpp
git commit -m "feat(web): add DMA-BUF ring protocol to web IPC"
```

---

### Task 2: Helper Vulkan backend (render into external images, export fds)

**Files:**
- Create: `src/wallpaper/web/web_vulkan_backend.h`
- Create: `src/wallpaper/web/web_vulkan_backend.cpp`
- Modify: `src/wallpaper/web/web_renderer_main.cpp` (try it before the render-control backend)
- Modify: `xmake.lua` (add the sources; the helper needs the Vulkan headers — already available via Qt6)

**Interfaces:**
- Consumes: `FrameRenderer` (tier-A Task 2), the shared helpers, `WebDmaBufBuffer`.
- Produces: `class web_renderer::VulkanBackend : public FrameRenderer`, constructed like `RenderControlBackend`. `start()` returns `false` when Vulkan external memory is unavailable.

- [ ] **Step 1: Implement the backend**

Mirror the spike (`vk_qt_probe.cpp`): `QVulkanInstance` with `QQuickGraphicsConfiguration::preferredInstanceExtensions()`; a `QQuickGraphicsConfiguration` with `setDeviceExtensions({VK_KHR_external_memory_fd, VK_EXT_external_memory_dma_buf})`; `QSGRendererInterface::Vulkan` + render control; `initialize()`; take the `VkDevice`/`VkPhysicalDevice` from `QSGRendererInterface::getResource(...)`. Allocate `K` linear BGRA8 images with `VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT`, bind, and export one fd each via `vkGetMemoryFdKHR`; fill `WebFrameBuffer::buffers[i]` (`fourcc = DRM_FORMAT_ARGB8888`, `modifier = 0`). Set `transport = 1`.

- [ ] **Step 2: Render and publish**

Each tick: `index = acquireDmaBuf(frame_)`; `window.setRenderTarget(QQuickRenderTarget::fromVulkanImage(images[index], VK_IMAGE_LAYOUT_UNDEFINED, VK_FORMAT_B8G8R8A8_UNORM, size))`; run the shared render loop; `publishDmaBuf(frame_, index)`.

- [ ] **Step 3: Build**

Run: `xmake build linux-wallpaperengine-webrender`
Expected: build succeeds.

- [ ] **Step 4: Verify export on all three GPUs**

Add a temporary `--dump-dmabuf` that prints the first exported fd/fourcc/stride, run it under the default device, `VK_ICD_FILENAMES=intel_icd.json`, and `lvp_icd.json`. Expected: three fds, `fourcc=DRM_FORMAT_ARGB8888`, non-zero stride. Remove the temporary flag.

- [ ] **Step 5: Commit**

```bash
git add src/wallpaper/web/web_vulkan_backend.h src/wallpaper/web/web_vulkan_backend.cpp src/wallpaper/web/web_renderer_main.cpp xmake.lua
git commit -m "feat(web): helper renders into exported DMA-BUF images"
```

---

### Task 3: Engine imports the DMA-BUF and blits it into an sg_image

**Files:**
- Create: `src/shared/graphics/backend/gpu_zero_copy_bgra.{h,cpp}` (or extend `gpu_zero_copy.*`)
- Modify: `src/shared/graphics/backend/sokol/sokol_implementation.cpp` (init a plain-sampler pipeline alongside the YCbCr one)
- Modify: `src/wallpaper/web/web_wallpaper.cpp` (`pollChild` uses the dmabuf path when `transport == 1`)

**Interfaces:**
- Consumes: `WebDmaBufBuffer`, the sokol `_sg.vk` device/queue, `s_zc`.
- Produces:
  - `bool gpu_init_zero_copy_bgra();`
  - `struct ImportedBgraSurface { VkImage image; VkDeviceMemory memory; VkImageView view; VkDescriptorSet descriptor_set; uint32_t width, height; };`
  - `ImportedBgraSurface* gpu_import_bgra_dmabuf(int fd, uint32_t fourcc, uint64_t modifier, uint32_t stride, uint32_t offset, uint32_t width, uint32_t height);`
  - `bool gpu_blit_zero_copy_bgra(const ImportedBgraSurface& surface, sg_image dst, int width, int height);`

- [ ] **Step 1: Implement import**

`vkCreateImage` with `VkExternalMemoryImageCreateInfo{handleTypes=DMA_BUF}`, `VK_FORMAT_B8G8R8A8_UNORM`, `VK_IMAGE_TILING_LINEAR`, `VK_IMAGE_USAGE_SAMPLED_BIT | TRANSFER_SRC`; `vkAllocateMemory` with `VkImportMemoryFdInfoKHR`; bind; create the view; allocate a descriptor set from the existing pool with a plain (non-YCbCr) sampler.

- [ ] **Step 2: Implement the blit**

Clone `gpu_blit_zero_copy_surface`'s structure (barriers + fullscreen triangle into the `sg_image`'s framebuffer) but bind the BGRA descriptor set and use a pipeline built with a plain-sampler descriptor layout. Factor the shared barrier/renderpass code so the two paths do not diverge.

- [ ] **Step 3: Build and unit-check**

Run: `xmake build linux-wallpaperengine`
Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/shared/graphics/backend/gpu_zero_copy_bgra.* src/shared/graphics/backend/sokol/sokol_implementation.cpp src/wallpaper/web/web_wallpaper.cpp
git commit -m "feat(gfx): import and blit a BGRA DMA-BUF into an sg_image"
```

---

### Task 4: Engine consumption signalling and lifecycle

**Files:**
- Modify: `src/wallpaper/web/web_wallpaper.cpp` (`pollChild`, `clear`)
- Modify: `src/wallpaper/web/web_wallpaper.h`

**Interfaces:**
- Consumes: `published_frame`/`published_index`/`consumed_frame`, `ImportedBgraSurface`.
- Produces: no new public interface.

- [ ] **Step 1: Consume frames**

In `pollChild`, when `transport == 1`: read `published_frame` (acquire); if it advanced, blit `buffers[published_index]`'s imported surface into `image_`; then `consumed_frame.store(published_frame, release)`. Import each buffer lazily and cache it by index; re-import only on size change.

- [ ] **Step 2: Release on teardown**

In `clear()`, destroy imported surfaces and close the duplicated fds. Ensure `consumed_frame` is advanced so a still-running helper does not block forever (the helper is stopped first).

- [ ] **Step 3: Build and manual verification**

Run the engine with the wallpaper and confirm it renders with no CPU readback (helper CPU should drop well below tier A's ~3.7 ms/frame). Then force `transport = 0` and confirm tier A still works.

- [ ] **Step 4: Commit**

```bash
git add src/wallpaper/web/web_wallpaper.cpp src/wallpaper/web/web_wallpaper.h
git commit -m "feat(web): consume DMA-BUF frames in the engine with fallback"
```

---

### Task 5: Capability gate, selection and documentation

**Files:**
- Modify: `src/wallpaper/web/web_renderer_main.cpp` (order: Vulkan → render-control → widget)
- Modify: `src/wallpaper/web/web_wallpaper.cpp` (engine accepts only if it can import)
- Modify: `docs/features.md`

- [ ] **Step 1: Gate and select**

Helper: try `VulkanBackend`, then `RenderControlBackend`, then `WidgetBackend`. Engine: if the helper published `transport == 1` but the import fails, treat the frame as unavailable and log once; do not crash.

- [ ] **Step 2: Verify the ladder**

Force each failure in turn (no external memory, no GL, no display) and confirm the helper/engine fall through to the next tier and the wallpaper still renders.

- [ ] **Step 3: Document**

Update the web wallpaper entry in `docs/features.md` to state the three transports and their fallback order.

- [ ] **Step 4: Run the full suite**

Run: `xmake test`
Expected: all suites pass.

- [ ] **Step 5: Commit**

```bash
git add src/wallpaper/web/web_renderer_main.cpp src/wallpaper/web/web_wallpaper.cpp docs/features.md
git commit -m "feat(web): select DMA-BUF transport with render-control and widget fallbacks"
```
