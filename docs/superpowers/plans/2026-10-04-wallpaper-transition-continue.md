# P3: Transition `continue` mode (outgoing keeps animating) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In `continue` mode, step and render the retained outgoing instance every frame of the fade and feed its live output to the transition shader, so scene, video and web wallpapers keep animating (with crossfading audio) until the transition ends, then are destroyed.

**Architecture:** P2 retains `WallpaperManager::outgoing_instance_` and already ticks it for audio. P3 promotes that tick to a full update + offscreen draw: activate the outgoing instance, `runtime->update(dt)`, `runtime->setForceOffscreen(true)`, `runtime->draw()`, then re-copy `runtime->composedImage()` into the transition's overlay texture. The active instance then draws and `compositeTransition` blends the live outgoing frame over it. `freeze` keeps P2's static snapshot; the only new behavior is behind `TransitionConfig.continue_previous`.

**Tech Stack:** C++17, sokol_gfx, miniaudio, xmake + plain-main tests.

**Spec:** `docs/superpowers/specs/2026-10-04-wallpaper-transition-modes-design.md` §7.

## Global Constraints

- Depends on P1 (mode plumbing + groups) and P2 (instance retention + `outgoing_instance_`).
- `freeze` remains the default; `continue` must work for **scene, video and web**.
- Audio crossfade is unchanged from P1/P2: old group `1 -> 0`, new group `0 -> 1`.
- Two web child processes during a `continue` crossfade are acceptable.
- The outgoing instance is destroyed exactly at progress 1 (and only then), together with its audio group.
- sokol_gfx passes must not nest: sequence the outgoing offscreen draw and the active draw, never interleave.
- Repo gates: `xmake format`, `xmake check`, `xmake test`.

## Review Focus

- **Offscreen source hygiene:** `setForceOffscreen(true)` must be reset (or scoped) so a non-transitioning `continue` frame does not leave the active runtime offscreen.
- **Pass ordering:** the outgoing offscreen draw must complete before the transition copies its image, and before the active swapchain pass opens.
- **Web child count:** exactly one outgoing + one active child; on rapid switches or failure the stale child must be stopped.
- **Video decoder cadence:** the outgoing decoder must advance at wall-clock rate during the fade; if it is ticked twice per frame the video visibly speeds up.
- **Failure after a live step:** if stepping the outgoing instance fails mid-fade, the transition must still finish and destroy it (never leak the instance or its group).

## File Structure

- Modify `src/wallpaper/transition/wallpaper_transition.h` / `.cpp` — live source support.
- Modify `src/wallpaper/wallpaper_manager.h` / `.cpp` — full outgoing step for `continue`.
- Modify `src/app/frame_loop.cpp` — sequence the outgoing offscreen draw before the active draw/composite.
- Modify `src/wallpaper/2d/scene_2d_wallpaper.cpp` — expose a step+drawn-composed helper if needed.
- Modify `src/wallpaper/web/web_wallpaper.cpp` — ensure the outgoing web child is polled during the fade.
- Modify `docs/features.md`, `README.md` — `continue` is implemented.

---

### Task 1: Live transition source

**Files:**
- Modify: `src/wallpaper/transition/wallpaper_transition.h` / `.cpp`

**Interfaces:**
- Consumes: the existing `image_`/`texture_view_`/`attachment_view_` snapshot target, `drawOverlay`.
- Produces:
  ```cpp
  // Marks the transition as using a per-frame live source. begin() must already
  // have allocated image_/views for width x height.
  void setLive(bool live);
  bool live() const;
  // Re-blits `source` (the outgoing instance's composed frame this frame) into the
  // overlay image. Safe to call every frame while active().
  void updateSource(EngineContext& ctx, sg_view source, sg_image source_image, int width, int height);
  ```

- [ ] **Step 1: Add the live flag and updateSource**

Refactor the one-time copy inside `begin()` (currently copying `source` into `image_`) into a private `copySource(ctx, source, source_image, width, height)` used by both `begin()` and the new `updateSource()`. `updateSource` re-blits into the existing `image_` (no reallocation when the size is unchanged) and is a no-op when `!active_` or `!live_`.

- [ ] **Step 2: Preserve `freeze`**

`begin()` defaults `live_ = false`; `freeze` never calls `updateSource`, so behavior is identical to P1/P2 (static snapshot).

- [ ] **Step 3: Extend the compositor self-test if one exists, else build**

Run: `xmake build linux-wallpaperengine`
Expected: ok. (The transition path is GPU-bound; the live source is verified manually in Task 4.)

- [ ] **Step 4: Commit**

```bash
git add src/wallpaper/transition
git commit -m "feat(transition): support a per-frame live overlay source"
```

---

### Task 2: Full outgoing step for scene and video in `continue`

**Files:**
- Modify: `src/wallpaper/wallpaper_manager.h` / `.cpp`
- Modify: `src/wallpaper/2d/scene_2d_wallpaper.h` / `.cpp`
- Modify: `src/app/frame_loop.cpp`

**Interfaces:**
- Consumes: `activateInstance`/`deactivateInstance` (P2), `setLive`/`updateSource` (Task 1), `Scene2DRuntime::setForceOffscreen`/`composedView`/`composedImage`/`update`/`draw`.
- Produces:
  ```cpp
  // Updates and offscreen-renders the outgoing instance, then refreshes the
  // transition's live source. No-op unless transitioning in continue mode.
  bool WallpaperManager::stepOutgoingForTransition(EngineContext& ctx, float dt);
  ```

- [ ] **Step 1: Implement `stepOutgoingForTransition`**

```cpp
bool WallpaperManager::stepOutgoingForTransition(EngineContext& ctx, float dt) {
    if (!outgoing_instance_ || !transition_.active() || !transition_.live()) return false;

    WallpaperInstance* previous = active_instance_.get();
    // Activate outgoing (stashes active's view), step it, render offscreen.
    activateInstance(ctx, *outgoing_instance_, previous);  // previous tracking handled below
    auto* scene = dynamic_cast<Scene2DWallpaper*>(outgoing_instance_->wallpaper.get());
    Scene2DRuntime* runtime = scene ? scene->getRuntime() : nullptr;
    bool ok = false;
    if (runtime && outgoing_instance_->wallpaper) {
        outgoing_instance_->wallpaper->update(dt, ctx);
        ctx.asset_mgr->updateVideoTextures(dt, ctx.scene.layers);  // video/audio cadence
        parallax_update(ctx, dt, surface::width(), surface::height());
        runtime->setForceOffscreen(true);
        runtime->draw();
        transition_.updateSource(ctx, runtime->composedView(), runtime->composedImage(), surface::width(),
                                 surface::height());
        runtime->setForceOffscreen(false);
        ok = true;
    }
    // Re-activate the incoming instance for this frame's normal work.
    WallpaperInstance* current = &*outgoing_instance_;
    activateInstance(ctx, *active_instance_, current);
    return ok;
}
```
(Adapt the `current` bookkeeping to the activation API: the manager should hold a `WallpaperInstance* active_view_` that `activateInstance` updates, rather than passing a local. Task 3 of P2 introduces exactly that pointer — use it here.)

- [ ] **Step 2: Wire it into the frame**

In `frame_loop.cpp::runFrame`, after `mgr.updateTransition(...)` and before the active runtime's offscreen draw / swapchain pass:
```cpp
if (mgr.isTransitioning()) {
    // continue mode fully steps the outgoing instance; only fall back to the
    // audio-only tick (freeze) when it did not.
    if (!mgr.stepOutgoingForTransition(ctx, (float)surface::frameDuration()))
        mgr.tickOutgoingAudio(ctx, (float)surface::frameDuration());
}
```
Ensure the outgoing offscreen draw happens while no pass is open (the active offscreen draw happens later, around `frame_loop.cpp:133`), and that the outgoing instance is ticked exactly once per frame (double-ticking the video decoder makes the outgoing video speed up).

- [ ] **Step 3: Select the mode**

In `WallpaperManager::beginPendingSwitch`, remove P1's `continue` fallback log. Set `transition_.setLive(config.continue_previous)` when the captured transition begins. For `continue`, `begin()` still snapshots the outgoing frame once (so the first fade frame has an image); subsequent frames come from `updateSource`.

- [ ] **Step 4: Build and manual scene/video check**

Run: `xmake build linux-wallpaperengine`
Manual: launch a visibly animating scene wallpaper, switch with `--transition-mode continue --transition fade --transition-duration 5000`. Confirm the old wallpaper keeps moving while it fades, its audio ramps down, and it disappears at the end. Repeat with a video wallpaper.

- [ ] **Step 5: Commit**

```bash
git add src && git commit -m "feat(transition): continue mode animates the outgoing wallpaper"
```

---

### Task 3: `continue` for web wallpapers

**Files:**
- Modify: `src/wallpaper/web/web_wallpaper.cpp` / `.h`
- Modify: `src/wallpaper/wallpaper_manager.cpp` (if the web path needs a non-scene step)

**Interfaces:**
- Consumes: `WebWallpaper::pollChild`/`update` (polls the child and refreshes `image_`), `Scene2DWallpaper::getRuntime` (web builds a scene from its frame image).
- Produces: outgoing web instance keeps polling its child and advancing its frame during the fade.

- [ ] **Step 1: Verify the web path uses the scene runtime**

`WebWallpaper` derives `Scene2DWallpaper` and builds its scene from the child frame via `SceneBuilder::buildImageScene`. Confirm `getRuntime()` is non-null and `update()` polls the child then the runtime draws the refreshed image. If `WebWallpaper::update` does not refresh `image_` before the runtime draw, call the poll in `stepOutgoingForTransition` before `runtime->draw()`.

- [ ] **Step 2: Ensure the outgoing child is not stopped early**

`WebWallpaper::clear()` stops the child; it must run only when the outgoing instance is destroyed at progress 1, not at swap. Confirm `WallpaperManager` destroys `outgoing_instance_` only in `updateTransition` at the end.

- [ ] **Step 3: Build and manual web check**

Run: `xmake build linux-wallpaperengine` (with `--web=y`) and switch to/from a web wallpaper with `--transition-mode continue`. Confirm two children run during the fade and the outgoing one exits at the end (`ps`/child pid), and the outgoing web frame keeps animating.

- [ ] **Step 4: Commit**

```bash
git add src/wallpaper/web src/wallpaper/wallpaper_manager.cpp
git commit -m "feat(transition): continue mode for web wallpapers"
```

---

### Task 4: Docs + diagnostic verification

**Files:**
- Modify: `README.md`, `docs/features.md`
- Modify: `docs/superpowers/specs/2026-10-04-wallpaper-transition-modes-design.md` (mark `continue` implemented)

- [ ] **Step 1: Diagnostics pixel check**

Run A with `--diagnose --diagnose-frame <frame>`, and from B switch with `--transition-mode continue --transition glass_shatter --transition-duration 12000`. The captured `015-transition-*.png` stage should show the outgoing wallpaper's *current* frame (not a stale snapshot) under the effect.

- [ ] **Step 2: Docs**

Update the mode description: `freeze` (default, frozen outgoing) and `continue` (outgoing keeps animating through the fade); both crossfade audio. Remove any "continue not implemented" note.

- [ ] **Step 3: Full gates and commit**

```bash
xmake format && xmake check && xmake test
git add README.md docs src && git commit -m "docs: continue mode and live transition source"
```

---

## Final verification

- [ ] `xmake format` clean, `xmake check` clean, `xmake test` green.
- [ ] Manual `continue` switch on scene, video and web: outgoing animates, audio crossfades, outgoing destroyed at the end.
- [ ] Two web children observed during a web-to-web `continue` fade, one remaining after.
- [ ] Re-check the Review Focus list; each case is covered by Tasks 1-3.
