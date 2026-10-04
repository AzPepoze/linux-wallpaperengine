# Web Offscreen Renderer (Tier A) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the web helper's `QWebEngineView::grab()` capture with a `QQuickRenderControl` offscreen render, keeping the existing shared-memory IPC, and keep the old widget capture as an automatic fallback.

**Architecture:** The helper (`linux-wallpaperengine-webrender`, an out-of-process QtWebEngine program) renders the page with an invisible `QQuickWindow` driven by `QQuickRenderControl` into an OpenGL FBO, reads the FBO back, and copies it into the same `WebFrameBuffer` the engine already consumes. The Qt-free pieces (shim builder, control-message decode, frame publish) are extracted so they can be unit-tested. If the render control cannot initialise, the helper falls back to today's widget `grab()` capture.

**Tech Stack:** C++17, Qt 6.11 (QtQuick, QtQml, QtWebEngineQuick, QtWebEngineWidgets for the fallback), OpenGL/EGL, POSIX shared memory, xmake.

**Spec:** `docs/superpowers/specs/2026-10-05-web-offscreen-render-design.md`

## Global Constraints

- The helper is only built when the `web` option is enabled (`has_config("web")`); the core engine must keep **no Qt dependency**.
- Keep the CLI contract and the `WebFrameBuffer`/control-socket contract in `src/wallpaper/web/web_ipc.h` unchanged.
- Do not force `QT_QPA_PLATFORM=offscreen` for the render-control path (it forces the software Qt Quick adaptation). Use the session platform; force `offscreen` only for the no-display widget fallback.
- Tier A performs exactly one CPU readback per frame; it must not add any engine-side changes.
- A failed backend must fall through to the next, never abort the wallpaper.
- `xmake test` must stay green; add unit tests only for Qt-free code.

## Review Focus

- **No display / headless session:** `QQuickRenderControl` cannot initialise. Expected: the helper selects the widget backend and still publishes frames.
- **Software / llvmpipe GL only:** render control may initialise but be very slow. Expected: it still produces correct frames; no crash.
- **Slow-loading or JS-heavy page:** frames before `DOMContentLoaded` are blank. Expected: the shim self-applies properties and the page paints within the normal capture cadence.
- **Page that reads `wallpaperPropertyListener`:** the shim must be present at `DocumentCreation` and properties applied after the listener is registered.
- **Zero-size / resized surface:** readback dimensions must match the shm buffer or the frame is dropped, not corrupted.

---

### Task 1: Qt-free shared helpers

**Files:**
- Create: `src/wallpaper/web/web_renderer_shared.h`
- Create: `src/wallpaper/web/web_renderer_shared.cpp`
- Test: `tests/web_renderer_shared_test.cpp`
- Modify: `xmake.lua` (add `web_renderer_tests` next to the other `add_test(...)` calls)

**Interfaces:**
- Consumes: `WebFrameBuffer`, `WebInputMessage`, `WebInputType` from `src/wallpaper/web/web_ipc.h` (unchanged).
- Produces:
  - `std::string web_renderer::buildShimScript(const std::string& user_properties_json, int fps)`
  - `web_renderer::RenderEvent web_renderer::decodeInput(const WebInputMessage& msg)` where `RenderEvent { EventKind kind; float x, y, scroll_x, scroll_y; uint32_t button, modifiers; }` and `enum class EventKind { None, MouseMove, MouseDown, MouseUp, Scroll, Shutdown }`
  - `bool web_renderer::publishFrame(WebFrameBuffer* frame, const uint8_t* bgra, uint32_t width, uint32_t height)`

- [ ] **Step 1: Write the failing test**

```cpp
#include "wallpaper/web/web_ipc.h"
#include "wallpaper/web/web_renderer_shared.h"

#include <cstring>
#include <string>
#include <vector>

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

int main() {
    int failures = 0;

    // Shim embeds the properties and applies them itself.
    const std::string shim = web_renderer::buildShimScript("{\"a\":{\"value\":1}}", 30);
    CHECK(shim.find("wallpaperRegisterAudioListener") != std::string::npos);
    CHECK(shim.find("{\"a\":{\"value\":1}}") != std::string::npos);
    CHECK(shim.find("30") != std::string::npos);

    // Control-message decoding.
    WebInputMessage m = {};
    m.type = WEB_INPUT_MOUSE_MOVE;
    m.x = 0.25f;
    m.y = 0.5f;
    CHECK(web_renderer::decodeInput(m).kind == web_renderer::EventKind::MouseMove);
    CHECK(web_renderer::decodeInput(m).x == 0.25f);
    m.type = WEB_INPUT_SHUTDOWN;
    CHECK(web_renderer::decodeInput(m).kind == web_renderer::EventKind::Shutdown);
    m.type = 999;
    CHECK(web_renderer::decodeInput(m).kind == web_renderer::EventKind::None);

    // Frame publish copies pixels and bumps the counter.
    std::vector<uint8_t> storage(sizeof(WebFrameBuffer) + 4 * 4);
    auto* fb = reinterpret_cast<WebFrameBuffer*>(storage.data());
    fb->width = 2;
    fb->height = 2;
    fb->pixel_format = 1;
    fb->frame_counter = 0;
    pthread_mutex_init(&fb->mutex, nullptr);
    std::vector<uint8_t> pixels(16, 0xAB);
    CHECK(web_renderer::publishFrame(fb, pixels.data(), 2, 2));
    CHECK(fb->frame_counter == 1);
    CHECK(reinterpret_cast<uint8_t*>(fb)[sizeof(WebFrameBuffer)] == 0xAB);
    CHECK(!web_renderer::publishFrame(fb, pixels.data(), 3, 2));  // wrong size
    pthread_mutex_destroy(&fb->mutex);

    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `xmake build web_renderer_tests && ./bin/debug/web_renderer_tests`
Expected: build fails with `'wallpaper/web/web_renderer_shared.h' file not found`.

- [ ] **Step 3: Implement `web_renderer_shared.{h,cpp}`**

`buildShimScript` returns the shim from the spike (the `wallpaperPropertyListener`/`wallpaperRegisterAudioListener`/media no-ops) with two additions: a literal `window.__lweApplyGeneralProperties({"fps":<fps>})` and `window.__lweApplyUserProperties(<user_properties_json>)` invoked from both a `DOMContentLoaded` listener and a `window` `load` listener (wrapped in `setTimeout(..., 150)`). `decodeInput` is a switch over `msg.type`. `publishFrame` validates `frame && bgra && frame->width == width && frame->height == height`, locks the mutex, `memcpy`s `height` rows of `width * 4`, bumps `frame_counter`, unlocks, and returns `true`.

- [ ] **Step 4: Run test to verify it passes**

Run: `xmake build web_renderer_tests && ./bin/debug/web_renderer_tests`
Expected: exit 0, no `FAIL` lines.

- [ ] **Step 5: Commit**

```bash
git add src/wallpaper/web/web_renderer_shared.h src/wallpaper/web/web_renderer_shared.cpp tests/web_renderer_shared_test.cpp xmake.lua
git commit -m "feat(web): extract Qt-free web renderer helpers with tests"
```

---

### Task 2: Extract the widget backend behind a common interface

**Files:**
- Create: `src/wallpaper/web/frame_renderer.h`
- Create: `src/wallpaper/web/web_widget_backend.h`
- Create: `src/wallpaper/web/web_widget_backend.cpp`
- Modify: `src/wallpaper/web/web_renderer_main.cpp` (use the backend; no behaviour change)
- Modify: `xmake.lua` (add the new sources to the `linux-wallpaperengine-webrender` target)

**Interfaces:**
- Consumes: `web_renderer::buildShimScript`, `web_renderer::decodeInput`, `web_renderer::publishFrame` (Task 1); the shim and capture code currently inline in `web_renderer_main.cpp`.
- Produces:
  - `class web_renderer::FrameRenderer` in `frame_renderer.h` (Qt-free): `virtual ~FrameRenderer() = default;` and `virtual bool start(const std::string& html_path, const std::string& user_properties_json, int ctrl_fd) = 0;`
  - `class web_renderer::WidgetBackend : public FrameRenderer` constructed as `WidgetBackend(WebFrameBuffer* frame, uint32_t width, uint32_t height, uint32_t fps)`; `start` returns `false` only if the view cannot be created.

- [ ] **Step 1: Move the current renderer into `web_widget_backend.{h,cpp}`**

Move the `WebRenderer` class body from `web_renderer_main.cpp` into `web_renderer::WidgetBackend`. Replace the inline shim constant with `buildShimScript(properties_json, fps)` inserted into the page's script collection, and replace the inline input `switch` with `decodeInput(msg)` mapped to Qt events. Keep the `QTimer` at `1000 / fps` and the `grab()` capture, but replace the row `memcpy` loop with `publishFrame(frame_, image.constBits(), width_, height_)` (after `convertToFormat(QImage::Format_ARGB32)`).

- [ ] **Step 2: Wire `web_renderer_main.cpp` to the backend**

Keep `main()`'s argument parsing and shm setup. After `QApplication`, construct `WidgetBackend` and call `start(...)`; on success run `app.exec()`; on failure print `web renderer: widget backend failed` and return 4. Keep the `setenv("QT_QPA_PLATFORM", "offscreen", 0)` and Chromium flags exactly as today.

- [ ] **Step 3: Build and run the existing behaviour**

Run: `xmake f --web=y && xmake f -m debug && xmake build linux-wallpaperengine-webrender`
Expected: build succeeds.

Run (reuse the spike's `measure_helper.py` from `/tmp/opencode/webprobe` if present, otherwise run the helper against a fixture with a memfd): the helper starts, the wallpaper renders, and the engine shows the page. Expected: same output as before the refactor (behaviour-preserving).

- [ ] **Step 4: Run the full test suite**

Run: `xmake test`
Expected: all suites pass (27 + `web_renderer_tests`).

- [ ] **Step 5: Commit**

```bash
git add src/wallpaper/web/frame_renderer.h src/wallpaper/web/web_widget_backend.h src/wallpaper/web/web_widget_backend.cpp src/wallpaper/web/web_renderer_main.cpp xmake.lua
git commit -m "refactor(web): extract widget renderer behind FrameRenderer interface"
```

---

### Task 3: Render-control backend

**Files:**
- Create: `src/wallpaper/web/web_render_control.h`
- Create: `src/wallpaper/web/web_render_control.cpp`
- Modify: `xmake.lua` (add the sources; add `pkgconfig::Qt6WebEngineQuick` and `pkgconfig::Qt6Quick` to the webrender target packages)

**Interfaces:**
- Consumes: `FrameRenderer` (Task 2), `buildShimScript`/`decodeInput`/`publishFrame` (Task 1).
- Produces: `class web_renderer::RenderControlBackend : public FrameRenderer`, constructed as `RenderControlBackend(WebFrameBuffer* frame, uint32_t width, uint32_t height, uint32_t fps)`. `start(html, properties, ctrl_fd)` returns `false` when any of: `QQuickRenderControl::initialize()` fails, the FBO cannot be created, or the QML component fails to instantiate.

- [ ] **Step 1: Implement the backend**

In `start()`:
1. `QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL)` must be called before the `QQuickWindow` is constructed (a static setting; calling it later silently leaves the software adaptation).
2. Create `QOpenGLContext` + `QOffscreenSurface`, `makeCurrent`, then `window.setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(&context))`, then `control.initialize()`. Return `false` if `initialize()` returns false.
3. Create a `QOpenGLFramebufferObject(width, height)` with internal format `GL_RGBA8`; `window.setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(fbo.texture(), fbo.size()))`; `window.resize(width, height)`.
4. Load a `QQmlComponent` whose root is `WebEngineView { width; height; settings.localContentCanAccessFileUrls: true; settings.localContentCanAccessRemoteUrls: true }`; parent it to `window.contentItem()`; give it active focus.
5. Inject `buildShimScript(properties, fps)` into `QWebEngineProfile::defaultProfile()->scripts()` at `QWebEngineScript::DocumentCreation`, `MainWorld`, `runsOnSubFrames`, **before** the component is created.
6. Start a `QTimer` at `1000 / fps` calling `tick()`.

In `tick()`: drain the ctrl socket with `decodeInput` (quit on `Shutdown`/EOF); forward `RenderEvent`s as `QMouseEvent`/`QWheelEvent` to the `QQuickWindow`; then `polishItems()`, `beginFrame()`, `sync()`, `render()`, `endFrame()` (all five, in that order — Qt 6 requires the frame pair); then `publishFrame(frame_, fbo.toImage().convertToFormat(QImage::Format_ARGB32).constBits(), width, height)`.

- [ ] **Step 2: Build**

Run: `xmake build linux-wallpaperengine-webrender`
Expected: build succeeds with QtQuick/Qml/WebEngineQuick linked.

- [ ] **Step 3: Verify it renders the fixture offscreen**

Add a temporary `--dump-frame <path>` argument that, on the first `tick()` after 5 seconds, saves `fbo.toImage()` to `<path>` and exits. Run the helper against `red.html` (a local page with a red body) and against a real workshop `index.html`. Expected: the PNG is non-blank (red, or the wallpaper's UI). Remove the temporary argument before committing.

- [ ] **Step 4: Commit**

```bash
git add src/wallpaper/web/web_render_control.h src/wallpaper/web/web_render_control.cpp xmake.lua
git commit -m "feat(web): add QQuickRenderControl offscreen backend"
```

---

### Task 4: Backend selection and fallback

**Files:**
- Modify: `src/wallpaper/web/web_renderer_main.cpp`

**Interfaces:**
- Consumes: `RenderControlBackend` (Task 3), `WidgetBackend` (Task 2), both as `FrameRenderer`.
- Produces: no new public interface; the helper selects a backend at startup.

- [ ] **Step 1: Prepare the platform and Qt WebEngine before `QApplication`**

Compute `const bool has_display = getenv("WAYLAND_DISPLAY") || getenv("DISPLAY");`. If `has_display`, do **not** set `QT_QPA_PLATFORM` (let Qt pick wayland/xcb). If not, set it to `offscreen` and skip the render-control attempt (the widget backend is the only viable one). Before constructing `QApplication`, also call `QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts)` and `QtWebEngineQuick::initialize()` (required by the QML `WebEngineView`; harmless for the widget backend).

- [ ] **Step 2: Select the backend**

After `QApplication` and shm setup: if `has_display`, construct `RenderControlBackend` and call `start(...)`; on success use it. Otherwise, or on failure, construct `WidgetBackend` and call `start(...)`; on failure print an error and return 4. Log which backend was chosen at info level, e.g. `LOG_TAG_I("WEB", "Web renderer backend: %s", ...)`. (The helper prints to stderr; use a plain `fprintf` if the engine logger is unavailable in this target.)

- [ ] **Step 3: Verify the fallback**

Temporarily force `initialize()` to fail (e.g. an env var checked at the top of `start()`), run the helper, and confirm the log says the widget backend was chosen and the page still renders. Remove the temporary hook.

- [ ] **Step 4: Run the full suite**

Run: `xmake test`
Expected: all suites pass.

- [ ] **Step 5: Commit**

```bash
git add src/wallpaper/web/web_renderer_main.cpp
git commit -m "feat(web): select render-control backend with widget fallback"
```

---

### Task 5: Verify and document

**Files:**
- Modify: `docs/features.md` (the web wallpaper entry)

- [ ] **Step 1: Measure CPU against the baseline**

Run the helper with the real wallpaper at 1080p/60 fps and measure the helper process CPU over ~8 s (the spike used a `measure_helper.py` that creates a memfd + socketpair and reads `/proc/<pid>/stat`). Expected: roughly **3–5 ms/frame** versus the `grab()` baseline of **~13.5 ms/frame**.

- [ ] **Step 2: Confirm the fallback ladder is documented**

In the web wallpaper entry of `docs/features.md`, state that the helper renders offscreen via Qt Quick with a widget-capture fallback, and that a zero-copy GPU transport is planned. Keep the entry honest about what is and is not implemented.

- [ ] **Step 3: Commit**

```bash
git add docs/features.md
git commit -m "docs: describe web offscreen renderer and fallback"
```
