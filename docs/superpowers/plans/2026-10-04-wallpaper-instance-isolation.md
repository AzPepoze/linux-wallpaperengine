# P2: Wallpaper instance isolation + shared assets + background load Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let two wallpapers coexist during a transition — each with its own scene/video/web runtime and audio group — while the process-wide install/internal assets are built once and shared, and let the incoming wallpaper prepare in the background while the outgoing one keeps running.

**Architecture:** Extract the process-wide asset pieces (install provider, internal provider, their texture decode cache) into a `SharedAssets`. Introduce `WallpaperInstance`, which owns one wallpaper's per-instance asset manager, scene/parallax/shake state, user properties, paths, `std::unique_ptr<Wallpaper>`, and `AudioEngine::GroupId`. `EngineContext` stays the single "active view": scene/parallax/shake are swapped in and out of the active instance, and `asset_mgr` becomes a pointer to the active instance's manager. `WallpaperManager` holds `active_instance_` and (during a fade) `outgoing_instance_`, ticking the outgoing one for video/web audio so it crossfades. Background load is a separate sub-phase that moves JSON parse and package extraction off the main thread while the outgoing instance renders.

**Tech Stack:** C++17, sokol_gfx, miniaudio, TaskPool, cJSON, xmake + plain-main tests.

**Spec:** `docs/superpowers/specs/2026-10-04-wallpaper-transition-modes-design.md` §6.

## Global Constraints

- P1 must be landed first. This plan consumes: `AudioEngine::GroupId`, `createGroup`/`destroyGroup`/`setGroupVolume`/`beginGroupFade`/`groupFading`/`cancelGroupFade`, `EngineContext::audio_group`, `AssetManager::setAudioGroup`, `TransitionConfig.continue_previous`, `SwitchRequest::continue_previous`, `WallpaperManager` group crossfade.
- Generic/install/internal assets are built **once** and shared; per-wallpaper assets are per instance. Do not rebuild `EngineAssetProvider`/`InternalAssetProvider` on a switch.
- Only one instance is "active" in `EngineContext` at a time; `ctx.scene`/`ctx.parallax`/`ctx.shake`/`ctx.asset_mgr` always describe the active view.
- sokol_gfx is not thread-safe: all `sg_*` object creation stays on the main thread.
- The default mode stays `freeze`; this plan makes `freeze` correct for video/web audio and is the foundation P3 needs.
- Sub-phase **P2a** (Tasks 1-5) lands independently and is useful on its own. Sub-phase **P2b** (Tasks 6-7) is background load.
- Repo gates: `xmake format`, `xmake check`, `xmake test`. Single target: `xmake build <target> && ./bin/debug/<target>`.

## Review Focus

- **Two instances alive at once:** the outgoing instance's `Scene2DRuntime` must not read the incoming instance's `ctx.scene` when it is stepped/drawn; activation must stash and restore.
- **Video audio during `freeze`:** the outgoing instance's video decoder must be pumped during the fade even though it is not rendered, or its stream starves and the "crossfade" is silence followed by a cut.
- **`ctx.asset_mgr` null dereference:** after converting it to a pointer, every activation path (initial load, switch, failed switch, sandbox) must set it before any `resolveTexture`/`resolvePath` call.
- **Layer ownership across instances:** layers/`scene_tree`/`scripts` belong to exactly one instance; the activation swap must move them, never copy a raw pointer into two live instances.
- **Shared decode cache lifetime:** `releaseDecodedTextures()` on a switch must not evict engine/internal textures, or engine assets re-decode every switch.
- **Background-load failure:** if a background job fails, the outgoing instance must keep playing (no held frozen frame, no half-swapped context).

## File Structure

- Create `src/shared/assets/shared_assets.h` — `SharedAssets` (engine/internal providers + their `TextureDecodeCache`).
- Create `src/shared/assets/texture_decode_cache.h` / `.cpp` — standalone cache extracted from `AssetManager::DecodeCache`.
- Create `src/wallpaper/wallpaper_instance.h` / `.cpp` — `WallpaperInstance` + `activateInstance`/`deactivateInstance`.
- Modify `src/shared/assets/asset_manager.h` / `.cpp` — reference shared assets; per-instance wallpaper provider/cache; split `init`.
- Modify `src/shared/core/engine_context.h` — `asset_mgr` becomes `AssetManager*`.
- Modify `src/wallpaper/wallpaper_manager.h` / `.cpp` — own active/outgoing instances.
- Modify `src/wallpaper/wallpaper_loader.h` / `.cpp` — load into an instance.
- Modify `src/wallpaper/2d/scene_builder.h` / `.cpp` — split `parse` from `build` (P2b).
- Modify `src/app/frame_loop.cpp`, `src/app/main.cpp` — activation and per-instance ticks.
- Test: `tests/wallpaper_instance_test.cpp` (activation swap semantics, no GPU).

---

## P2a — isolation + shared assets + retention

### Task 1: Extract `TextureDecodeCache` and `SharedAssets`

**Files:**
- Create: `src/shared/assets/texture_decode_cache.h`
- Create: `src/shared/assets/texture_decode_cache.cpp`
- Create: `src/shared/assets/shared_assets.h`
- Modify: `src/shared/assets/asset_manager.h` / `.cpp`
- Modify: `xmake.lua` (main target picks up the new files via the `src/**.cpp` glob; no change needed unless a test target includes them)

**Interfaces:**
- Consumes: nothing.
- Produces:
  ```cpp
  // texture_decode_cache.h
  class TextureDecodeCache {
     public:
      std::shared_ptr<const wallpaper_engine::DecodedImage> decode(const char* abs_path, int image_index) const;
      void prefetch(const char* abs_path, int image_index);  // enqueue on TaskPool
      void release();
  private:
      struct Impl; std::unique_ptr<Impl> impl_;
  };

  // shared_assets.h
  struct SharedAssets {
      std::string engine_path;
      std::unique_ptr<EngineAssetProvider> engine_provider;
      std::unique_ptr<InternalAssetProvider> internal_provider;
      std::unique_ptr<TextureDecodeCache> decode_cache;
  };
  ```

- [ ] **Step 1: Move `DecodeCache` out of `AssetManager` verbatim**

Lift the body of `AssetManager::DecodeCache` (`asset_manager.cpp:45-54`) and `AssetManager::decodeShared` (`asset_manager.cpp:106-124`) into `TextureDecodeCache`. Keep behavior identical (`std::shared_future` entries keyed by `path#index`). `prefetch` enqueues the decode task on `TaskPool` (from `prefetchPackageTextures`).

- [ ] **Step 2: Add `SharedAssets` and switch `AssetManager` to reference it**

In `AssetManager`, replace the `engine_provider`/`internal_provider` members with `SharedAssets* shared_ = nullptr;` and add:
```cpp
void attachShared(SharedAssets* shared);
void initWallpaper(const char* wallpaper_path);   // was the wallpaper half of init()
```
Keep `wallpaper_provider` and a `std::unique_ptr<TextureDecodeCache> wallpaper_decode_cache_` per instance. `resolvePath` uses `shared_->internal_provider`, then `wallpaper_provider`, then `shared_->engine_provider` (same order as today, `asset_manager.cpp:280-290`). `decodeShared` routes to `shared_->decode_cache` when the path is under the engine root or internal, else `wallpaper_decode_cache_`.

- [ ] **Step 3: Build**

Run: `xmake build linux-wallpaperengine`
Expected: compiles. (Callers still use `ctx.asset_mgr.init(...)`; Task 2 adapts them. If the build breaks only there, that is expected — finish Step 3 with the temporary shim `AssetManager::init` that creates a throwaway `SharedAssets` so the app still builds.)

- [ ] **Step 4: Commit**

```bash
git add src/shared/assets xmake.lua
git commit -m "refactor(assets): extract TextureDecodeCache and SharedAssets"
```

---

### Task 2: Make `EngineContext::asset_mgr` a pointer

**Files:**
- Modify: `src/shared/core/engine_context.h`
- Modify: every caller of `ctx.asset_mgr` (grep below)
- Modify: `src/app/main.cpp`

**Interfaces:**
- Consumes: `SharedAssets`, `AssetManager::attachShared`/`initWallpaper` (Task 1).
- Produces: `EngineContext::asset_mgr` is `AssetManager*`; a process-wide `SharedAssets shared_assets;` in `main.cpp`.

- [ ] **Step 1: Change the member**

In `engine_context.h`: `AssetManager* asset_mgr = nullptr;` (remove the value member). `asset_manager.h` include can stay.

- [ ] **Step 2: Update every reference**

Run `grep -rn "asset_mgr\." src` and change each to `asset_mgr->`. The set is ~47 call sites (grep for confirmation); do them file by file, rebuilding periodically.

- [ ] **Step 3: Create and attach shared assets at startup**

In `main.cpp`, add a `static SharedAssets shared_assets;` next to `ctx`. Wherever `ctx.asset_mgr.init(ctx.engine_path, ...)` is called (`main.cpp:133`, `:167`, and inside `WallpaperLoader::load`), set `shared_assets.engine_path = ctx.engine_path`;
`shared_assets.engine_provider = std::make_unique<EngineAssetProvider>(ctx.engine_path);`
`shared_assets.internal_provider = std::make_unique<InternalAssetProvider>();`
`shared_assets.decode_cache = std::make_unique<TextureDecodeCache>();`
once (guard with a `bool` or a `SharedAssets::ensure(engine_path)` helper). Then `ctx.asset_mgr->attachShared(&shared_assets); ctx.asset_mgr->initWallpaper(path);`.

- [ ] **Step 4: Build and run the suite**

Run: `xmake build linux-wallpaperengine && xmake test`
Expected: build ok; all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src && git commit -m "refactor(context): reference the active AssetManager by pointer"
```

---

### Task 3: Introduce `WallpaperInstance` and activation

**Files:**
- Create: `src/wallpaper/wallpaper_instance.h` / `.cpp`
- Create: `tests/wallpaper_instance_test.cpp`
- Modify: `xmake.lua` (add `wallpaper_instance_tests`)

**Interfaces:**
- Consumes: `EngineContext`, `AssetManager`, `SceneState`, `ParallaxState`, `CameraShakeState`, `UserProperties`, `AudioEngine::GroupId`.
- Produces:
  ```cpp
  struct WallpaperInstance {
      std::string asset_root;
      std::string wallpaper_path;
      bool is_pkg = false;
      scene_type_t scene_type = SCENE_TYPE_2D;
      UserProperties user_properties;
      SceneState scene;
      ParallaxState parallax;
      CameraShakeState shake;
      AssetManager assets;
      std::unique_ptr<Wallpaper> wallpaper;
      AudioEngine::GroupId audio_group = AudioEngine::kDefaultGroup;
  };

  // Moves `instance`'s per-wallpaper view into ctx, stashing whatever `current`
  // pointed at back into that instance first. Sets ctx.asset_mgr = &instance.assets.
  void activateInstance(EngineContext& ctx, WallpaperInstance& instance, WallpaperInstance*& current);
  // Stashes the current instance's view back and clears ctx.asset_mgr.
  void deactivateInstance(EngineContext& ctx, WallpaperInstance*& current);
  ```

- [ ] **Step 1: Write the failing test**

`tests/wallpaper_instance_test.cpp` — no GPU, no device:
```cpp
#include "wallpaper/wallpaper_instance.h"

#include "test_util.h"

int main() {
    EngineContext ctx;
    WallpaperInstance a;
    WallpaperInstance b;
    a.scene.scene_w = 100.0f;
    b.scene.scene_w = 200.0f;
    a.parallax.amount = 1.0f;
    b.parallax.amount = 2.0f;

    WallpaperInstance* current = nullptr;
    activateInstance(ctx, a, current);
    CHECK(current == &a);
    CHECK(ctx.scene.scene_w == 100.0f);
    CHECK(ctx.parallax.amount == 1.0f);
    CHECK(ctx.asset_mgr == &a.assets);

    // Mutating the active view must land back in the instance on the next swap.
    ctx.scene.scene_w = 111.0f;
    activateInstance(ctx, b, current);
    CHECK(current == &b);
    CHECK(ctx.scene.scene_w == 200.0f);
    CHECK(a.scene.scene_w == 111.0f);

    deactivateInstance(ctx, current);
    CHECK(current == nullptr);
    CHECK(ctx.asset_mgr == nullptr);
    CHECK(b.scene.scene_w == 200.0f);
    return test::finish("wallpaper instance checks");
}
```
Add to `xmake.lua`:
```lua
add_test("wallpaper_instance_tests", {"tests/wallpaper_instance_test.cpp",
                                      "src/wallpaper/wallpaper_instance.cpp",
                                      "src/shared/assets/asset_manager.cpp",
                                      "src/shared/assets/providers/asset_provider.cpp",
                                      "src/shared/assets/media/video_texture.cpp",
                                      "src/shared/assets/media/video_audio.cpp",
                                      "src/shared/assets/tex_decoder.cpp", "src/shared/assets/tex_format.cpp",
                                      "src/shared/assets/tex_header.cpp", "src/shared/assets/tex_payload.cpp",
                                      "src/shared/assets/texture_decode_cache.cpp", "src/shared/core/vfs.cpp",
                                      "src/shared/core/logger.cpp", "src/shared/core/task_pool.cpp",
                                      "src/shared/core/utils.cpp"},
         {"miniaudio", "lz4", "stb", "cjson"}, {"avformat", "avutil", "swresample", "dl", "m", "pthread"})
```
(Adjust the file list during implementation if `asset_manager.cpp` pulls more; the goal is that the test links.)

- [ ] **Step 2: Run it to verify it fails**

Run: `xmake build wallpaper_instance_tests && ./bin/debug/wallpaper_instance_tests`
Expected: FAIL to compile — `wallpaper_instance.h` missing.

- [ ] **Step 3: Implement `WallpaperInstance` and the activation functions**

`activateInstance`:
```cpp
void activateInstance(EngineContext& ctx, WallpaperInstance& instance, WallpaperInstance*& current) {
    if (current == &instance) return;
    if (current) {
        current->scene = std::move(ctx.scene);
        current->parallax = ctx.parallax;
        current->shake = ctx.shake;
        current->user_properties = ctx.user_properties;
        current->asset_root = ctx.asset_root;
        current->wallpaper_path = ctx.wallpaper_path;
        current->is_pkg = ctx.is_pkg;
        current->scene_type = ctx.scene_type;
    }
    ctx.scene = std::move(instance.scene);
    ctx.parallax = instance.parallax;
    ctx.shake = instance.shake;
    ctx.user_properties = instance.user_properties;
    std::snprintf(ctx.asset_root, sizeof(ctx.asset_root), "%s", instance.asset_root.c_str());
    std::snprintf(ctx.wallpaper_path, sizeof(ctx.wallpaper_path), "%s", instance.wallpaper_path.c_str());
    ctx.is_pkg = instance.is_pkg;
    ctx.scene_type = instance.scene_type;
    ctx.asset_mgr = &instance.assets;
    ctx.audio_group = instance.audio_group;
    current = &instance;
}
```
`deactivateInstance` mirrors the stash and resets `ctx.asset_mgr = nullptr; ctx.audio_group = AudioEngine::kDefaultGroup;`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `xmake build wallpaper_instance_tests && ./bin/debug/wallpaper_instance_tests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/wallpaper/wallpaper_instance.* tests/wallpaper_instance_test.cpp xmake.lua
git commit -m "feat(wallpaper): add WallpaperInstance and activation swap"
```

---

### Task 4: `WallpaperManager` owns active/outgoing instances

**Files:**
- Modify: `src/wallpaper/wallpaper_manager.h` / `.cpp`
- Modify: `src/wallpaper/wallpaper_loader.h` / `.cpp`
- Modify: `src/app/main.cpp` (`loadInitialWallpaper`)
- Modify: `src/app/frame_loop.cpp`

**Interfaces:**
- Consumes: `WallpaperInstance`, `activateInstance`/`deactivateInstance` (Task 3); P1 group crossfade.
- Produces:
  ```cpp
  bool WallpaperManager::load(const std::string& scene_directory, EngineContext& ctx);  // unchanged signature
  WallpaperInstance* activeInstance();
  WallpaperInstance* outgoingInstance();  // non-null only during a fade
  ```

- [ ] **Step 1: `WallpaperLoader::load` returns a built instance**

Change `WallpaperLoader::load(const ProjectInfo&, EngineContext&, WallpaperInstance& out)`:
- Set `out.asset_root`/`out.wallpaper_path`/`out.is_pkg`/`out.scene_type`.
- Create/replace `out.assets` and `out.assets.attachShared(shared)`; `out.assets.initWallpaper(out.asset_root.c_str())`; `out.assets.setAudioGroup(out.audio_group)`.
- `createWallpaper(...)` builds the runtime; the returned `std::unique_ptr<Wallpaper>` goes to `out.wallpaper`.
- Preserve the existing `loadUserProperties`, `prefetchPackageTextures`, `releaseDecodedTextures`, and `applyVideoProperties` behavior, but against `out` instead of `ctx` (activation makes `ctx` a view over `out`, so this may be a thin rewrite).

- [ ] **Step 2: `WallpaperManager::load` creates and activates an instance**

Add a member `WallpaperInstance* active_view_ = nullptr;` to `WallpaperManager` — it is the `current` passed to `activateInstance` so the manager can tell which instance the global view currently reflects.

`WallpaperManager::load(scene_directory, ctx)`:
1. `ProjectInfo info = ProjectInfo::detect(...)`; `canLoad`.
2. `auto instance = std::make_unique<WallpaperInstance>();`
3. `instance->audio_group = AudioEngine::instance().createGroup();`
4. `activateInstance(ctx, *instance, active_view_);` (stashes any previous active view).
5. `WallpaperLoader::load(info, ctx, *instance)`; on failure, `destroyGroup(instance->audio_group)`, `deactivateInstance(ctx, active_view_)`, return false.
6. `active_instance_ = std::move(instance)`.

Replace `active_wallpaper_` with `active_instance_` in `update`/`render`/`pause`/`resume`/`handleInput`/`getActiveWallpaper` (`getActiveWallpaper()` returns `active_instance_->wallpaper.get()`).

- [ ] **Step 3: Tick the outgoing instance's video/audio during a fade**

In `WallpaperManager`, add `void tickOutgoingAudio(EngineContext& ctx, float dt);` that (when `outgoing_instance_` exists) temporarily activates it, calls `outgoing_instance_->assets.updateVideoTextures(dt, outgoing_instance_->scene.layers)`, then re-activates the active instance. Call it from `update()` when `transition_.active()`.

In `beginPendingSwitch`:
- Keep P1's group fade logic, but now the outgoing instance is retained:
  `outgoing_instance_ = std::move(active_instance_);` before loading the new instance; store `outgoing_instance_->audio_group` as the fading group.
- Load the new instance into `active_instance_` as in Step 2.
- On transition end (in `updateTransition`), destroy the outgoing instance with **its own view active**, or `Scene2DRuntime::cleanup()` will tear down the active instance's layers (it reads `ctx.scene`). Order:
  ```cpp
  activateInstance(ctx, *outgoing_instance_, active_view_);  // switch the view to outgoing
  outgoing_instance_->wallpaper->clear();                    // runtime cleanup, stops child/decoder
  outgoing_instance_.reset();
  active_view_ = nullptr;                                    // it pointed into the reset instance
  activateInstance(ctx, *active_instance_, active_view_);    // restore the active view
  destroyGroup(fading_group_);
  ```
  (`~Scene2DWallpaper` also calls `clear()`, so the explicit call is safe and makes the ordering visible.)
- On a new switch while already fading, reset `outgoing_instance_` (and destroy its group) before replacing it — use the same activate-before-clear ordering — mirroring P1's rapid-switch rule.

- [ ] **Step 4: Update the frame loop and initial load**

In `frame_loop.cpp::updateFrame`, the active instance is already in `ctx` (activated by `beginPendingSwitch`/`load`). Add `if (mgr.isTransitioning()) mgr.tickOutgoingAudio(ctx, dt);` before `ctx.asset_mgr->updateVideoTextures(...)`, and make sure `ctx.asset_mgr` is the active instance's.

In `main.cpp::loadInitialWallpaper`, the `ctx.asset_mgr.init(...)` calls are replaced by `WallpaperManager::load`, which sets up the shared assets and the instance.

- [ ] **Step 5: Build, run the suite, and manually switch a scene → video → web**

Run: `xmake build linux-wallpaperengine && xmake test`
Manual: switch between a scene with sound, a video with audio, and (web build) a web wallpaper. Confirm video/web audio now ramps down with the outgoing group during `freeze` instead of cutting, and the incoming wallpaper starts normally.

- [ ] **Step 6: Commit**

```bash
git add src/wallpaper src/app tests xmake.lua
git commit -m "feat(wallpaper): retain an outgoing instance for crossfade and audio"
```

---

### Task 5: P2a docs + P1 limitation removal

**Files:**
- Modify: `docs/features.md`
- Modify: `docs/superpowers/specs/2026-10-04-wallpaper-transition-modes-design.md` (mark the P1 video/web limitation resolved in P2)

- [ ] **Step 1: Update docs**

Remove the "video/web audio is not crossfaded" caveat from `docs/features.md` and from the spec's §5.6/§8 once the manual check in Task 4 passes.

- [ ] **Step 2: Commit**

```bash
git add docs && git commit -m "docs: video/web audio crossfade via instance retention"
```

---

## P2b — background load

### Task 6: Split `SceneBuilder` CPU parse from GPU build

**Files:**
- Modify: `src/wallpaper/2d/scene_builder.h` / `.cpp`
- Modify: `src/wallpaper/2d/scene_2d_wallpaper.cpp`

**Interfaces:**
- Consumes: `wallpaper_engine::parseSceneFile`, `SceneBuilder::buildFromDocument`.
- Produces:
  ```cpp
  // CPU-only; safe off the main thread (no sg_* calls).
  bool SceneBuilder::parse(const char* scene_json_path, const UserProperties& props,
                           wallpaper_engine::SceneDocument& out);
  ```

- [ ] **Step 1: Extract the parse**

`SceneBuilder::load` currently is `parseSceneFile` then `buildFromDocument` (`scene_builder.cpp:41-52`). Add `parse()` that wraps `wallpaper_engine::parseSceneFile`. Keep `load()` calling `parse()` then `buildFromDocument()` so behavior is unchanged.

- [ ] **Step 2: Build and run the suite**

Run: `xmake build linux-wallpaperengine && xmake test`
Expected: unchanged behavior (refactor only).

- [ ] **Step 3: Commit**

```bash
git add src/wallpaper/2d && git commit -m "refactor(scene): split scene parse from scene build"
```

---

### Task 7: Background prepare + main-thread commit

**Files:**
- Create: `src/wallpaper/asset_load_job.h` / `.cpp`
- Modify: `src/wallpaper/wallpaper_manager.h` / `.cpp`
- Modify: `src/app/frame_loop.cpp`

**Interfaces:**
- Consumes: `SceneBuilder::parse` (Task 6), `ProjectInfo`, `prepareAssetRoot`/package mount, `TaskPool`.
- Produces:
  ```cpp
  class AssetLoadJob {
     public:
      // Starts CPU work (package mount + scene parse) on the task pool.
      void start(ProjectInfo info, EngineContext* staging_ctx, UserProperties props);
      bool ready() const;                 // poll from the main thread
      bool failed() const;
      wallpaper_engine::SceneDocument takeDocument();  // main thread, after ready()
  };
  ```

- [ ] **Step 1: Implement the job**

Run package extraction/mount and `SceneBuilder::parse` inside `TaskPool::instance().enqueue(...)`. The job must not touch `ctx.asset_mgr` or any `sg_*`. Store the parsed document behind a mutex/future. `ready()`/`failed()` are non-blocking polls.

- [ ] **Step 2: Stage the switch**

In `beginPendingSwitch`, when a switch is requested and an instance is already active:
1. Capture the outgoing frame (existing) **after** the job is ready only for `none`; for a real effect, keep the P1 behavior of capturing at swap. (WE blocks the fade until loaded, so: start the job, keep rendering the active instance, and when `ready()`, capture + swap.)
2. While the job runs, `beginPendingSwitch` returns without swapping; the active instance keeps updating/rendering as normal. Track `pending_load_job_`.
3. When `ready()`, commit on the main thread: create the new `WallpaperInstance`, activate it, `buildFromDocument` into `ctx.scene`, build the wallpaper, move the old to `outgoing_instance_`, start the transition.
4. On `failed()`, log and drop the job; the active instance is untouched (Review Focus).

- [ ] **Step 3: Verify the outgoing keeps animating during the prepare**

Manual: start a background-load switch on a scene wallpaper; while the job runs, the current wallpaper must keep animating (not freeze), then the crossfade plays. Confirm with a long/packaged wallpaper and the debug overlay frame counter.

- [ ] **Step 4: Build, run the suite, commit**

```bash
xmake format && xmake check && xmake test
git add src && git commit -m "feat(wallpaper): prepare switches in the background before fading"
```

---

## Final verification

- [ ] `xmake format` clean, `xmake check` clean, `xmake test` green.
- [ ] Manual scene→video→web switches crossfade audio in `freeze`.
- [ ] The old wallpaper keeps animating while a background load is in progress.
- [ ] Re-check the Review Focus list; each case is covered by Tasks 3/4/7.
