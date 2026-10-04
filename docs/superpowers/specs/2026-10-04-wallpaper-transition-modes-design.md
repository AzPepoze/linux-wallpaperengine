# Wallpaper transition end-behaviors: freeze / continue + audio crossfade

Date: 2026-10-04
Status: approved design, P1 pending implementation
Depends on: the Wallpaper Engine playlist transition shaders + control-socket
switch handoff feature (already implemented on `dev`; see git history around
`b621217` "capture the transition overlay as a scene stage"). This spec extends
that feature.

Plans: P1 `docs/superpowers/plans/2026-10-04-wallpaper-transition-modes.md`,
P2 `docs/superpowers/plans/2026-10-04-wallpaper-instance-isolation.md`,
P3 `docs/superpowers/plans/2026-10-04-wallpaper-transition-continue.md`.

## 1. Goal

When wallpaper switching completes, the outgoing wallpaper currently dies at
the moment the new one is swapped in: its audio is cut (the scene sound layer
is destroyed and `AudioEngine::stop` is called) and its last frame is held by
the transition snapshot. Two end-behaviors are requested:

- **freeze** (default): the outgoing visual is frozen (the existing snapshot)
  for the fade, then the old wallpaper is gone. Its audio must crossfade out.
- **continue**: the outgoing wallpaper keeps animating through the fade, then
  is killed. Its audio must crossfade out. Must work for scene, video and web.

In both modes the outgoing audio fades out while the incoming audio fades in
over the transition duration ("both modes fade audio"). Default mode is
`freeze` (cheaper). `continue` is required but is the larger refactor.

## 2. What real Wallpaper Engine does (verified)

- **The playlist transition is a crossfade.** The install shader
  `assets/shaders/HLSL/dx11playlisttransition.frag` samples the *outgoing*
  wallpaper (`g_Texture0`) and draws it over the already-rendered incoming
  wallpaper with a progress-driven mask and premultiplied alpha. Both images
  are on screen together for the whole transition. It is not a cut, a
  slide-away, or a fade-to-black by default.
- **WE blocks the fade until the new wallpaper has loaded.** Official
  changelog: "Improved fade animation by blocking it until the new wallpaper
  has loaded." A 2023 Steam report describes the opposite bug (default desktop
  flashing before the fade), i.e. the intended behavior is: load first, then
  fade.
- It is **not documented** whether the outgoing wallpaper keeps animating
  during the fade. The shader samples it live each frame, which suggests WE
  keeps it running (i.e. behaves like `continue`), but this is unproven. Treated
  as "likely continue", not as a requirement.

Design consequence: the new wallpaper should load in the background while the
old keeps running, and the transition should only render once the new one is
ready. This is what makes the old keep animating during the load.

## 3. Rulings

- **R1** Default mode is `freeze` (performance), but `continue` must be
  implemented and selectable.
- **R2** Both modes crossfade audio: old group gain `1 -> 0`, new group gain
  `0 -> 1`, over the transition duration.
- **R3** Isolation is preferred (and needed for background load and future
  multi-monitor), but shared/generic assets must not be rebuilt per instance —
  mirroring real WE where install/generic content is loaded once.
- **R4** Two web child processes during a crossfade are acceptable.
- **R5** The new wallpaper loads in the background while the old keeps running;
  the transition renders only after the new wallpaper is ready.
- **R6** `continue` must cover scene, video and web. Because a video decoder and
  the web child live in the wallpaper instance, this falls out of per-instance
  isolation.

## 4. Phasing

- **P1 — audio groups + crossfade + mode plumbing + `freeze`.**
  No dual runtime. `AudioEngine` gains audio groups; the outgoing group is
  faded while the incoming group fades in; scene sound layers crossfade
  correctly because their miniaudio voices are self-contained once playing.
  `freeze` is the visual behavior (existing snapshot). Synchronous load is
  unchanged. Mode flags are accepted; `continue` logs and falls back to
  `freeze`. Independently useful and low-risk.
- **P2 — `SharedAssets` split + `WallpaperInstance` isolation + background
  load.** New wallpapers build into their own instance across frames/workers
  while the active one keeps rendering, then swap. This makes "old keeps
  animating during the load" true for both modes, and enables video/web audio
  to survive the fade (their decoders/child live in the instance).
- **P3 — `continue`.** During the fade, step and render the outgoing instance
  into the transition source each frame; destroy it at the end.

## 5. P1 design (detailed)

### 5.1 `AudioEngine` groups

New public API (`src/shared/audio/audio_engine.h`):

```cpp
using GroupId = uint32_t;
static constexpr GroupId kDefaultGroup = 0;

GroupId createGroup();                       // never returns kDefaultGroup
void    destroyGroup(GroupId group);         // kDefaultGroup is a no-op
void    setGroupVolume(GroupId group, float volume);
float   groupVolume(GroupId group) const;    // for tests / bookkeeping
// Marks a group as fading: stop(handle) detaches instead of destroying, so the
// voice keeps playing and is owned by the group until destroyGroup().
void    beginGroupFade(GroupId group);
bool    groupFading(GroupId group) const;
void    cancelGroupFade(GroupId group);      // failure path

// play() and createStream() take an optional group (default kDefaultGroup):
SoundHandle  play(const std::string& path, bool loop, float volume,
                  bool start_paused = false, GroupId group = kDefaultGroup);
StreamHandle createStream(uint32_t sample_rate, uint32_t channels,
                          GroupId group = kDefaultGroup);
```

Internals (`audio_engine_internal.h` / `.cpp`):

- `struct GroupState { float volume = 1.0f; bool fading = false; };`
  stored in `std::vector<GroupState> groups;` (`groups[0]` = default, never
  destroyed; `groups` may grow lazily; a freed slot can be reused but for P1
  append-only reuse is fine).
- `SoundSlot` gains `GroupId group` and `float base_volume`. Effective sound
  volume is `base_volume * groups[group].volume` (master stays on the
  miniaudio engine). `play` stores base+group and applies effective;
  `setVolume` updates base and re-applies.
- `Stream` gains `GroupId group`. Effective stream volume is
  `volume * groups[group].volume`; `setStreamMuted`/`setStreamVolume` use it.
- `setGroupVolume` updates the state then re-applies the effective volume to
  every active sound slot and stream in that group.
- `stop(handle)`: if `groupFading(slot->group)` then mark the slot as detached
  (keep playing, do **not** free the handle or the slot) and return; otherwise
  behave as today (stop + uninit + free).
  - Detached voices are freed by `destroyGroup(group)`.
- `destroyStream(handle)`: if `groupFading(stream->group)` — for P1 there is no
  benefit because a starved stream is silent (the decoder is gone), so keep the
  existing behavior (destroy immediately). Documented limitation: **video/web
  audio is not crossfaded in P1**; it is fixed in P2 when the instance (and its
  decoder) is retained.
- `destroyGroup(group)`: no-op for `kDefaultGroup`; otherwise stop/uninit and
  free every active sound slot and stream whose `group == group` (including
  detached voices), then reset the group state.
- `shutdown()` clears groups too.

### 5.2 Active group on the context

`EngineContext` (`src/shared/core/engine_context.h`) gains:

```cpp
AudioEngine::GroupId audio_group = AudioEngine::kDefaultGroup;
```

`AssetManager` (`src/shared/assets/asset_manager.h/.cpp`) gains
`void setAudioGroup(AudioEngine::GroupId)` + a stored `audio_group_`, passed to
`createStream(...)` when a video texture with audio is created. This prepares
video audio; it does not fix it until P2.

`SoundLayer` (`src/wallpaper/2d/layers/sound/sound_layer.cpp`) stores a
`AudioEngine::GroupId group_` captured in `createFromDocument` from
`ctx.audio_group`, and passes it to `AudioEngine::play(...)` in
`playCurrent()`. No change to `stop()` (the engine handles fade-detach).

### 5.3 Mode plumbing

- `src/wallpaper/transition/transition_catalog.h/.cpp`:
  ```cpp
  enum class Mode { Freeze, Continue };
  bool parseMode(const std::string& text, Mode& out);       // "freeze"/"continue"
  // Resolves a raw --transition-mode string. Empty means freeze. Sets
  // out_continue and fills `error` on an unknown value.
  bool resolveTransitionModeSetting(const std::string& raw, bool& out_continue,
                                    std::string& error);
  ```
- `TransitionConfig` (`transition_shader.h`) gains
  `bool continue_previous = false;`.
- `SwitchRequest` (`src/app/control/control_protocol.h/.cpp`) gains
  `bool continue_previous = false;`, encoded/decoded as a JSON string key
  `"transition_mode"` with values `"freeze"`/`"continue"` (unknown value leaves
  the default and returns a decode error).
- CLI: `CliOptions.transition_mode` (`--transition-mode`), registered in
  `cli_args::kValueOptions` (so its value does not leak into the positional
  wallpaper path — this was a prior live bug with `--transition`), parsed in
  `cli_options.cpp`, and a `transition_mode` config key as fallback.
- `main.cpp`: resolve the mode, set `transition_config.continue_previous`, and
  set `request.continue_previous` for the initiating launch.
- `WallpaperManager::beginPendingSwitch`: `config.continue_previous =
  request.continue_previous`; in P1, if true, log
  `"transition-mode continue is not implemented yet (P2); using freeze"` and
  force `false`.

### 5.4 Switch flow with audio crossfade (P1)

In `WallpaperManager::beginPendingSwitch(ctx)`:

1. Resolve config (selection, duration, mode) as today.
2. `old_group = ctx.audio_group`. Only crossfade when there is an active
   wallpaper and `old_group != kDefaultGroup`.
3. If crossfading, `AudioEngine::instance().beginGroupFade(old_group)` **before**
   `switchWallpaper` so that `clear()` destroying the old `SoundLayer`s only
   detaches their voices instead of stopping them.
4. `new_group = AudioEngine::instance().createGroup()`;
   `AudioEngine::instance().setGroupVolume(new_group, 0.0f)` when a transition
   will play (else `1.0f`). Set `ctx.audio_group = new_group` and
   `ctx.asset_mgr.setAudioGroup(new_group)` **before** loading, so the new
   wallpaper's sound layers and video streams join the new group.
5. Capture the outgoing frame (existing) and `switchWallpaper` (existing).
6. Store `fading_group_ = old_group` and `active_group_ = new_group` on the
   manager; `audio_crossfade_ = crossfading`.
7. On `switchWallpaper` failure: `destroyGroup(new_group)`, restore
   `ctx.audio_group = old_group`, `setGroupVolume(old_group, 1.0f)`,
   `cancelGroupFade(old_group)`, clear `fading_group_`. (`hold()` already keeps
   the frozen frame.) Note: if `clear()` destroyed the old layers, the detached
   voices keep playing at full volume — acceptable.
8. `updateTransition(dt)` becomes: `transition_.update(dt)` then, if
   `audio_crossfade_` and `transition_.active()`, `p = transition_.progress()`,
   `setGroupVolume(fading_group_, 1 - p)`, `setGroupVolume(active_group_, p)`.
   When the transition ends (`!active()` or `p >= 1`), `destroyGroup(fading_group_)`,
   `setGroupVolume(active_group_, 1.0f)`, `audio_crossfade_ = false`.
9. Selection `none` (hard cut): no visual transition, so `destroyGroup(old_group)`
   immediately (audio cut, matching the visual cut). Documented.

### 5.5 Tests (P1)

- `transition_catalog_tests`: `parseMode` accepts `freeze`/`continue` (and is
  case-insensitive), rejects unknown; `resolveTransitionModeSetting` empty ->
  freeze, invalid -> error.
- `control_protocol_tests`: encode/decode round-trips `transition_mode`;
  invalid value -> decode error; missing -> default freeze.
- `cli_tests` (`cli_args_test.cpp`): `--transition-mode` is in `kValueOptions`
  (value does not leak into positional).
- `AudioEngine` group bookkeeping: `createGroup` returns a non-default id and
  distinct ids; `setGroupVolume`/`groupVolume` round-trip; `destroyGroup`
  no-ops on `kDefaultGroup`; `beginGroupFade`/`groupFading`/`cancelGroupFade`.
  These run without an audio device (the engine stays unavailable; `play`
  returns invalid and is not exercised).
- Live/manual: scene wallpaper with a looping sound switched during playback —
  old sound ramps down while the new ramps up, no click/cut. Video audio is
  documented as a P1 cut.

### 5.6 Docs (P1)

- `README.md`: document `--transition-mode freeze|continue` and the
  `transition_mode` config key; note `continue` is P3 (P1 falls back to freeze).
- `docs/features.md` (or the existing feature doc): same, plus the P1 audio
  limitation for video/web.

## 6. P2 design (outline — not implemented in P1)

- **Shared vs per-instance assets.** Split `AssetManager` so the
  process-wide pieces are built once at startup and shared:
  - `SharedAssets`: `EngineAssetProvider` (WE install root),
    `InternalAssetProvider`, a shared install-asset decode cache, other generic
    content.
  - `WallpaperInstance`: one per running wallpaper — `std::unique_ptr<Wallpaper>`,
    its own `WallpaperAssetProvider` + per-instance decode cache, its own video
    textures/decoders, its own `SceneState`/`ParallaxState`/`CameraShakeState`,
    and its own `AudioEngine::GroupId`.
  - `AssetManager` becomes "shared engine assets + this instance's wallpaper
    assets" (a reference to `SharedAssets`) instead of owning everything. This
    is the real-WE split: generic data loaded once, per-wallpaper data per
    instance.
- **`WallpaperInstance` retention.** `WallpaperManager` holds `active_` plus,
  during a transition, `outgoing_` (the previous instance) instead of destroying
  it at swap. `ctx.scene`/`ctx.parallax`/`ctx.shake`/`ctx.asset_mgr` are the
  *active view*, swapped to whichever instance is being stepped. This avoids
  rewriting the ~257 `ctx.scene`, ~42 `ctx.parallax`, ~24 `ctx.shake`,
  ~47 `ctx.asset_mgr` references.
- **Background load.** Build the new instance over several frames/workers while
  the active one keeps rendering: package extract, JSON parse, texture decode
  and shader compile can run off-thread (the project already has a task pool and
  async shader init), but sokol GPU object creation must happen on the main
  thread. Commit the instance and start the transition only when ready, matching
  WE's "block the fade until loaded".
- **State isolation.** `AssetManager` must become a pointer/indirection rather
  than a value so two contexts can coexist. `ScriptEngine` has one global frame
  clock (`beginFrame`); two live script sets may need per-instance calls.
- **Audio.** With the outgoing instance retained, `destroyStream` occurs only at
  transition end, so video/web audio crossfades in both modes. `AudioEngine`
  groups from P1 are unchanged; the outgoing instance owns the old group.

## 7. P3 design (outline — `continue`)

- Each frame of the fade, step the outgoing instance (`outgoing_->update(dt,
  outgoing_ctx)`) and render it offscreen to produce the transition's
  `g_Texture0` source, instead of the static snapshot. The existing
  `Scene2DRuntime::setForceOffscreen(true)` + `composedView()`/`composedImage()`
  path is the mechanism (already used by `beginPendingSwitch` to capture the
  snapshot).
- Video: step the outgoing video decoder and upload its frame; audio crossfades.
- Web: step the outgoing web child (its frame image is the source). Two web
  children during the crossfade is acceptable (R4).
- At progress 1, `destroyGroup(outgoing group)` and destroy the outgoing
  instance.

## 8. Risks and decisions log

- R1/R2 accepted as above. Unknown WE behavior (does the old animate during the
  fade) is not a requirement; `continue` is our own mode.
- P1 cannot crossfade video/web audio (decoder destroyed at swap). Documented;
  fixed in P2.
- Detached voices (scene sounds) survive `clear()` and are owned by the group;
  `destroyGroup` is the single cleanup point. If a group is never destroyed
  (e.g. a failed switch), its detached voices play until the next switch — a
  bounded, accepted leak of one group + its voices.
- Background load (P2) is a substantial refactor of `WallpaperLoader` /
  `Scene2DRuntime` / `AssetManager`; it is the same refactor as per-instance
  isolation, which is why they are one phase.
- Multi-monitor: the isolation is designed to support N instances, but running
  multiple outputs in one process is not part of this feature (currently one
  process per output).
