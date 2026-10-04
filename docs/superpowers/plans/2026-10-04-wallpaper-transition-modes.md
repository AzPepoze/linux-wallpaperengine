# Wallpaper Transition Modes (P1: audio crossfade + freeze) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give `AudioEngine` audio groups, crossfade the outgoing wallpaper's audio into the incoming one over the transition, and add a `freeze`/`continue` transition mode (default `freeze`) with `continue` plumbing that falls back to `freeze` until P2/P3.

**Architecture:** Audio groups live inside the existing `AudioEngine` singleton: every voice and PCM stream belongs to a `GroupId`, and a group gain multiplies the per-voice base volume. A wallpaper's sounds join `ctx.audio_group`. On a switch the old group is marked fading (so `stop()` detaches instead of killing still-needed voices), a new group is created for the incoming wallpaper, and the manager ramps the two group gains against the transition's progress, destroying the old group at the end. The visual behavior is the existing `freeze` snapshot; mode is plumbed through CLI/config/control but `continue` logs and falls back to `freeze`.

**Tech Stack:** C++17, miniaudio (AudioEngine), cJSON (control protocol), sokol (unchanged), xmake + plain-main unit tests.

**Spec:** `docs/superpowers/specs/2026-10-04-wallpaper-transition-modes-design.md`

## Global Constraints

- Default mode is `freeze`; `continue` must be accepted but falls back to `freeze` in P1 with a log line.
- Both modes crossfade audio: old group gain `1 -> 0`, new group gain `0 -> 1` over the transition duration.
- `AudioEngine::kDefaultGroup == 0` is never destroyed and always has gain 1.0.
- Video/web audio crossfade is **out of scope for P1** (the decoder is destroyed at swap); do not claim it works.
- Any new CLI value option (e.g. `--transition-mode`) MUST be added to `cli_args::kValueOptions` or its value leaks into the positional wallpaper path (prior live bug with `--transition`).
- Test runner for a single target: `xmake build <target> && ./bin/$(mode)/<target>` (this repo does not support selecting a target via `xmake test`). Repo-wide gates: `xmake format`, `xmake check`, `xmake test`.
- Match existing code style; the repo runs `xmake check` (clang-tidy/format strict) — keep lines and names consistent.

## Review Focus

- **Unknown `--transition-mode` value:** must not silently switch to `continue`; it must warn and use `freeze` (same pattern as unknown `--transition`).
- **Repeated/rapid switches:** the old group from a previous unfinished crossfade must be destroyed, not leaked, and the newest request wins (`pending_switch_` already keeps only the newest).
- **Failed switch after the old wallpaper was cleared:** restore the old group's gain to 1.0 and cancel its fade; do not leave it ramped to 0.
- **First-ever load / no active wallpaper:** there is no old group to fade; new group gain must be 1.0 immediately, no dangling crossfade.
- **Selection `none` (hard cut):** audio must cut like the visual (destroy the old group immediately), not fade over 1 s.

---

### Task 1: AudioEngine audio groups

**Files:**
- Modify: `src/shared/audio/audio_engine.h`
- Modify: `src/shared/audio/audio_engine_internal.h`
- Modify: `src/shared/audio/audio_engine.cpp`
- Modify: `src/shared/audio/audio_engine_stream.cpp`
- Create: `tests/audio_engine_test.cpp`
- Modify: `xmake.lua` (add `audio_engine_tests` next to the other `add_test` calls)

**Interfaces:**
- Consumes: nothing.
- Produces:
  ```cpp
  using GroupId = uint32_t;
  static constexpr GroupId kDefaultGroup = 0;
  GroupId createGroup();
  void    destroyGroup(GroupId group);
  void    setGroupVolume(GroupId group, float volume);
  float   groupVolume(GroupId group) const;
  void    beginGroupFade(GroupId group);
  bool    groupFading(GroupId group) const;
  void    cancelGroupFade(GroupId group);
  SoundHandle  play(const std::string& path, bool loop, float volume, bool start_paused = false,
                    GroupId group = kDefaultGroup);
  StreamHandle createStream(uint32_t sample_rate, uint32_t channels, GroupId group = kDefaultGroup);
  ```

- [ ] **Step 1: Write the failing test**

Create `tests/audio_engine_test.cpp`:

```cpp
#include "shared/audio/audio_engine.h"

#include "test_util.h"

int main() {
    AudioEngine& audio = AudioEngine::instance();

    // Groups are pure bookkeeping and work with no audio device open.
    CHECK(audio.groupVolume(AudioEngine::kDefaultGroup) == 1.0f);

    const AudioEngine::GroupId g1 = audio.createGroup();
    const AudioEngine::GroupId g2 = audio.createGroup();
    CHECK(g1 != AudioEngine::kDefaultGroup);
    CHECK(g2 != AudioEngine::kDefaultGroup);
    CHECK(g1 != g2);

    audio.setGroupVolume(g1, 0.25f);
    CHECK(audio.groupVolume(g1) == 0.25f);
    CHECK(audio.groupVolume(g2) == 1.0f);

    CHECK(!audio.groupFading(g1));
    audio.beginGroupFade(g1);
    CHECK(audio.groupFading(g1));
    audio.cancelGroupFade(g1);
    CHECK(!audio.groupFading(g1));

    audio.destroyGroup(g1);
    audio.destroyGroup(AudioEngine::kDefaultGroup);  // must be a no-op
    CHECK(audio.groupVolume(AudioEngine::kDefaultGroup) == 1.0f);

    return test::finish("audio engine group checks");
}
```

- [ ] **Step 2: Register the test target and run it to verify it fails**

In `xmake.lua`, next to the other `add_test(...)` calls, add:

```lua
add_test("audio_engine_tests", {"tests/audio_engine_test.cpp", "src/shared/audio/audio_engine.cpp",
                                "src/shared/audio/audio_engine_stream.cpp", "src/shared/core/vfs.cpp",
                                "src/shared/core/logger.cpp"},
         {"miniaudio", "lz4", "stb"}, {"dl", "m", "pthread"})
```

Run: `xmake build audio_engine_tests && ./bin/debug/audio_engine_tests`
Expected: FAIL to compile — `createGroup`/`groupVolume`/etc. are not members of `AudioEngine`.

- [ ] **Step 3: Add the group API to the header**

In `src/shared/audio/audio_engine.h`, add after the `StreamHandle` declarations:

```cpp
    using GroupId = uint32_t;
    static constexpr GroupId kDefaultGroup = 0;

    // Groups multiply every voice/stream in them by a shared gain. kDefaultGroup
    // always exists at gain 1.0 and is never destroyed.
    GroupId createGroup();
    void destroyGroup(GroupId group);
    void setGroupVolume(GroupId group, float volume);
    float groupVolume(GroupId group) const;
    // While fading, stop() detaches the voice (keeps it playing, owned by the
    // group) instead of destroying it, so it can fade out via the group gain.
    void beginGroupFade(GroupId group);
    bool groupFading(GroupId group) const;
    void cancelGroupFade(GroupId group);
```

Change the two signatures:

```cpp
    SoundHandle play(const std::string& path, bool loop, float volume, bool start_paused = false,
                     GroupId group = kDefaultGroup);
    StreamHandle createStream(uint32_t sample_rate, uint32_t channels, GroupId group = kDefaultGroup);
```

- [ ] **Step 4: Add group state to the impl**

In `src/shared/audio/audio_engine_internal.h`, inside `struct AudioEngine::Impl`, add:

```cpp
    struct GroupState {
        float volume = 1.0f;
        bool fading = false;
    };
    std::vector<GroupState> groups = {GroupState{}};  // index 0 == kDefaultGroup
    std::vector<GroupId> group_free;
```

Add `GroupId group = kDefaultGroup;` and `float base_volume = 1.0f;` to `SoundSlot`, and
`GroupId group = kDefaultGroup;` to `Stream`.

- [ ] **Step 5: Implement group management and gain application**

In `src/shared/audio/audio_engine.cpp`, after `setAudioDisabled`/`isAudioDisabled` (before `play`),
implement:

```cpp
AudioEngine::GroupId AudioEngine::createGroup() {
    GroupId id;
    if (!impl->group_free.empty()) {
        id = impl->group_free.back();
        impl->group_free.pop_back();
        impl->groups[id] = Impl::GroupState{};
    } else {
        impl->groups.push_back(Impl::GroupState{});
        id = (GroupId)(impl->groups.size() - 1);
    }
    return id;
}

void AudioEngine::destroyGroup(GroupId group) {
    if (group == kDefaultGroup || group >= impl->groups.size()) return;
    for (size_t i = 0; i < impl->sound_slots.size(); ++i) {
        auto& slot = impl->sound_slots[i];
        if (slot && slot->active && slot->group == group) {
            ma_sound_stop(&slot->sound);
            slot->shutdown();
            impl->sound_free.push_back((SoundHandle)(i + 1));
        }
    }
    for (size_t i = 0; i < impl->streams.size(); ++i) {
        auto& stream = impl->streams[i];
        if (!stream || stream->group != group) continue;
        if (stream->sound_ready) ma_sound_uninit(&stream->sound);
        if (stream->rb_ready) ma_pcm_rb_uninit(&stream->rb);
        if (stream->base.vtable) ma_data_source_uninit(&stream->base);
        stream.reset();
        impl->stream_free.push_back((StreamHandle)(i + 1));
    }
    impl->groups[group] = Impl::GroupState{};
    impl->group_free.push_back(group);
}

void AudioEngine::setGroupVolume(GroupId group, float volume) {
    if (group >= impl->groups.size()) return;
    impl->groups[group].volume = std::max(0.0f, volume);
    const float gain = impl->groups[group].volume;
    for (size_t i = 0; i < impl->sound_slots.size(); ++i) {
        auto& slot = impl->sound_slots[i];
        if (slot && slot->active && slot->group == group)
            ma_sound_set_volume(&slot->sound, slot->base_volume * gain);
    }
    for (auto& stream : impl->streams) {
        if (stream && stream->group == group && stream->sound_ready)
            ma_sound_set_volume(&stream->sound, stream->muted ? 0.0f : stream->volume * gain);
    }
}

float AudioEngine::groupVolume(GroupId group) const {
    if (group >= impl->groups.size()) return 1.0f;
    return impl->groups[group].volume;
}

void AudioEngine::beginGroupFade(GroupId group) {
    if (group < impl->groups.size()) impl->groups[group].fading = true;
}

bool AudioEngine::groupFading(GroupId group) const {
    return group < impl->groups.size() && impl->groups[group].fading;
}

void AudioEngine::cancelGroupFade(GroupId group) {
    if (group < impl->groups.size()) impl->groups[group].fading = false;
}
```

Update `play` to take `GroupId group`, and set `slot->group = group; slot->base_volume = std::max(0.0f, volume);`
then apply `slot->base_volume * groupVolume(group)` via `ma_sound_set_volume`. Update `stop` to detach when
the slot's group is fading:

```cpp
void AudioEngine::stop(SoundHandle handle) {
    if (handle == kInvalidSound || handle > impl->sound_slots.size()) return;
    auto& slot = impl->sound_slots[handle - 1];
    if (!slot || !slot->active) return;
    if (groupFading(slot->group)) return;  // detached; destroyGroup() cleans up
    ma_sound_stop(&slot->sound);
    slot->shutdown();
    impl->sound_free.push_back(handle);
}
```

Update `setVolume` to store `base_volume` and apply `base_volume * groupVolume(slot->group)`.

In `src/shared/audio/audio_engine_stream.cpp`, update `createStream(sample_rate, channels, group)` to set
`stream->group = group` and apply the group gain to the initial `ma_sound_set_volume`; update
`setStreamMuted` and `setStreamVolume` to multiply by `groupVolume(stream->group)`.

In `audio_engine.cpp` `shutdown()`, after clearing slots/streams, add
`impl->groups = {Impl::GroupState{}};` and `impl->group_free.clear();`.
In `destroyStream` (stream file), no group change is needed (P1 destroys streams immediately).

- [ ] **Step 6: Run the test to verify it passes**

Run: `xmake build audio_engine_tests && ./bin/debug/audio_engine_tests`
Expected: PASS — `audio engine group checks: N checks, 0 failures`.

- [ ] **Step 7: Commit**

```bash
git add src/shared/audio tests/audio_engine_test.cpp xmake.lua
git commit -m "feat(audio): add audio groups with fading voice detach"
```

---

### Task 2: Transition mode parsing + `TransitionConfig.continue_previous`

**Files:**
- Modify: `src/wallpaper/transition/transition_catalog.h`
- Modify: `src/wallpaper/transition/transition_catalog.cpp`
- Modify: `src/wallpaper/transition/transition_shader.h`
- Modify: `tests/transition_catalog_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  ```cpp
  enum class Mode { Freeze, Continue };
  bool parseMode(const std::string& text, Mode& out);
  bool resolveTransitionModeSetting(const std::string& raw, bool& out_continue, std::string& error);
  // struct TransitionConfig { int selection; int duration_ms; bool continue_previous = false; };
  ```

- [ ] **Step 1: Write the failing tests**

In `tests/transition_catalog_test.cpp`, before `return test::finish(...)`, add:

```cpp
    Mode mode = Mode::Continue;
    CHECK(parseMode("freeze", mode) && mode == Mode::Freeze);
    CHECK(parseMode("continue", mode) && mode == Mode::Continue);
    CHECK(parseMode("FREEZE", mode) && mode == Mode::Freeze);
    CHECK(!parseMode("bogus", mode));

    bool continue_previous = true;
    CHECK(resolveTransitionModeSetting("", continue_previous, error) && !continue_previous);
    CHECK(resolveTransitionModeSetting("freeze", continue_previous, error) && !continue_previous);
    CHECK(resolveTransitionModeSetting("continue", continue_previous, error) && continue_previous);
    CHECK(!resolveTransitionModeSetting("bogus", continue_previous, error) && !error.empty());
```

Add `std::string error;` declaration near the top of `main()` if not present, and `#include <string>`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `xmake build transition_catalog_tests && ./bin/debug/transition_catalog_tests`
Expected: FAIL to compile — `Mode`/`parseMode`/`resolveTransitionModeSetting` are not declared.

- [ ] **Step 3: Declare the API**

In `src/wallpaper/transition/transition_catalog.h`, after `parseSelection`, add:

```cpp
// How the outgoing wallpaper behaves once a transition ends.
enum class Mode {
    Freeze,    // outgoing visual is frozen for the fade (default)
    Continue,  // outgoing keeps animating through the fade (P2/P3)
};

bool parseMode(const std::string& text, Mode& out);

// Resolves a raw --transition-mode string. Empty means freeze. Sets out_continue
// and fills `error` on an unknown value.
bool resolveTransitionModeSetting(const std::string& raw, bool& out_continue, std::string& error);
```

- [ ] **Step 4: Implement it**

In `src/wallpaper/transition/transition_catalog.cpp`, add `#include <algorithm>` and `#include <cctype>`, then:

```cpp
bool parseMode(const std::string& text, Mode& out) {
    std::string lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (lowered == "freeze") {
        out = Mode::Freeze;
        return true;
    }
    if (lowered == "continue") {
        out = Mode::Continue;
        return true;
    }
    return false;
}

bool resolveTransitionModeSetting(const std::string& raw, bool& out_continue, std::string& error) {
    if (raw.empty()) {
        out_continue = false;
        return true;
    }
    Mode mode;
    if (!parseMode(raw, mode)) {
        error = "unknown transition mode '" + raw + "'";
        return false;
    }
    out_continue = mode == Mode::Continue;
    error.clear();
    return true;
}
```

- [ ] **Step 5: Add the config field**

In `src/wallpaper/transition/transition_shader.h`, extend `TransitionConfig`:

```cpp
struct TransitionConfig {
    int selection = (int)lwe::transition::Effect::Fade;
    int duration_ms = 1000;
    bool continue_previous = false;
};
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `xmake build transition_catalog_tests && ./bin/debug/transition_catalog_tests`
Expected: PASS — `transition catalog checks: N checks, 0 failures`.

- [ ] **Step 7: Commit**

```bash
git add src/wallpaper/transition tests/transition_catalog_test.cpp
git commit -m "feat(transition): add freeze/continue mode parsing and config field"
```

---

### Task 3: `transition_mode` in the control protocol

**Files:**
- Modify: `src/app/control/control_protocol.h`
- Modify: `src/app/control/control_protocol.cpp`
- Modify: `tests/control_protocol_test.cpp`

**Interfaces:**
- Consumes: `lwe::transition::parseMode` from Task 2.
- Produces: `SwitchRequest::continue_previous`; JSON string key `"transition_mode"` (`"freeze"`/`"continue"`).

- [ ] **Step 1: Write the failing tests**

In `tests/control_protocol_test.cpp`, add round-trip and error checks alongside the existing ones
(adjust names to the file's local style; it uses `CHECK` from `test_util.h`):

```cpp
    {
        SwitchRequest request;
        request.path = "/wp";
        request.continue_previous = true;
        SwitchRequest decoded;
        std::string error;
        CHECK(decodeSwitchRequest(encodeSwitchRequest(request), decoded, error));
        CHECK(decoded.continue_previous);
    }
    {
        SwitchRequest request;
        request.path = "/wp";  // default: freeze
        SwitchRequest decoded;
        std::string error;
        CHECK(decodeSwitchRequest(encodeSwitchRequest(request), decoded, error));
        CHECK(!decoded.continue_previous);
    }
    {
        SwitchRequest decoded;
        std::string error;
        CHECK(!decodeSwitchRequest("{\"path\":\"/wp\",\"transition_mode\":\"bogus\"}", decoded, error));
        CHECK(!error.empty());
    }
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `xmake build control_protocol_tests && ./bin/debug/control_protocol_tests`
Expected: FAIL to compile — no member `continue_previous`.

- [ ] **Step 3: Add the field and encode/decode**

In `src/app/control/control_protocol.h`, add to `SwitchRequest`:

```cpp
    bool continue_previous = false;  // false == freeze (default)
```

In `encodeSwitchRequest` (`control_protocol.cpp`), after the transition number:

```cpp
    cJSON_AddStringToObject(root, "transition_mode", request.continue_previous ? "continue" : "freeze");
```

In `decodeSwitchRequest`, after parsing `transition_time_ms`:

```cpp
    if (const cJSON* mode = cJSON_GetObjectItemCaseSensitive(root, "transition_mode"); cJSON_IsString(mode)) {
        lwe::transition::Mode parsed_mode = lwe::transition::Mode::Freeze;
        if (!lwe::transition::parseMode(mode->valuestring, parsed_mode)) {
            cJSON_Delete(root);
            error = std::string("unknown transition_mode '") + mode->valuestring + "'";
            return false;
        }
        parsed.continue_previous = parsed_mode == lwe::transition::Mode::Continue;
    }
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `xmake build control_protocol_tests && ./bin/debug/control_protocol_tests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/app/control tests/control_protocol_test.cpp
git commit -m "feat(control): carry transition_mode in switch requests"
```

---

### Task 4: CLI flag, config key, and main wiring

**Files:**
- Modify: `src/app/cli_options.h`
- Modify: `src/app/cli_options.cpp`
- Modify: `src/app/cli_args.cpp`
- Modify: `src/shared/core/utils.h`
- Modify: `src/shared/core/utils.cpp`
- Modify: `src/app/main.cpp`
- Modify: `config.example.json`
- Modify: `tests/cli_args_test.cpp`

**Interfaces:**
- Consumes: `lwe::transition::resolveTransitionModeSetting` from Task 2; `SwitchRequest::continue_previous` from Task 3.
- Produces: `CliOptions.transition_mode`; `bool read_config_transition_mode(char* out_mode, size_t len)`; `TransitionConfig.continue_previous` set from CLI/config.

- [ ] **Step 1: Write the failing test**

In `tests/cli_args_test.cpp`, add:

```cpp
    const V mode = {"app", "--transition-mode", "freeze", "/wp"};
    CHECK(cli_args::positional(mode) == "/wp");
    CHECK(cli_args::optionValue(mode, {"--transition-mode"}, value) && value == "freeze");
    CHECK(cli_args::takesValue("--transition-mode"));
```

(`value` already exists in the file.)

- [ ] **Step 2: Run the test to verify it fails**

Run: `xmake build cli_tests && ./bin/debug/cli_tests`
Expected: FAIL — `--transition-mode` is treated as a positional value, so `positional` is not `/wp`.

- [ ] **Step 3: Register the value option**

In `src/app/cli_args.cpp`, add `"--transition-mode"` to `kValueOptions` (after `"--transition-duration"`).

- [ ] **Step 4: Parse the flag and config key**

In `src/app/cli_options.h`, add `std::string transition_mode;` near `transition`.

In `src/app/cli_options.cpp`, after the `--transition`/`--transition-duration` parsing:

```cpp
    cli_args::optionValue(args, {"--transition-mode"}, opts.transition_mode);
```

In `src/shared/core/utils.h`, declare:

```cpp
bool read_config_transition_mode(char* out_mode, size_t mode_len);
```

In `src/shared/core/utils.cpp`, add (mirroring `read_config_transition`'s candidate loop):

```cpp
bool read_config_transition_mode(char* out_mode, size_t mode_len) {
    if (!out_mode || mode_len == 0) return false;
    const char* config_candidates[] = {"config.json", "../config.json", "../../config.json", "../../../config.json",
                                       "../../../../config.json"};
    for (const char* cfg : config_candidates) {
        char* config_str = read_file_to_string(cfg);
        if (!config_str) continue;
        cJSON* config_json = cJSON_Parse(config_str);
        if (config_json) {
            cJSON* mode = cJSON_GetObjectItemCaseSensitive(config_json, "transition_mode");
            if (cJSON_IsString(mode) && mode->valuestring[0] != '\0') {
                copyPath(out_mode, mode_len, mode->valuestring);
                cJSON_Delete(config_json);
                free(config_str);
                return true;
            }
            cJSON_Delete(config_json);
        }
        free(config_str);
    }
    return false;
}
```

In `src/app/cli_options.cpp`, before `sargs_shutdown()`, add the config fallback:

```cpp
    char config_mode[64] = {};
    if (opts.transition_mode.empty() && read_config_transition_mode(config_mode, sizeof(config_mode)))
        opts.transition_mode = config_mode;
```

Add `"transition_mode": "freeze"` to `config.example.json` next to `"transition"`.

- [ ] **Step 5: Wire it into main**

In `src/app/main.cpp`, in `applyCliToContext()` after `resolveTransitionSetting`:

```cpp
    bool continue_previous = false;
    std::string mode_error;
    if (!lwe::transition::resolveTransitionModeSetting(cli.transition_mode, continue_previous, mode_error))
        LOG_W("[CONTROL] %s; using freeze", mode_error.c_str());
    transition_config.continue_previous = continue_previous;
```

For a value that fails resolution, `resolveTransitionModeSetting` leaves `out_continue` untouched, so assign
`continue_previous = false;` first (initialize) to guarantee freeze. In the handoff block (the
`ControlClient::tryHandoff` path), resolve the same mode and set `request.continue_previous = continue_previous;`.

- [ ] **Step 6: Run the test to verify it passes**

Run: `xmake build cli_tests && ./bin/debug/cli_tests`
Expected: PASS.

Then build the whole app: `xmake build linux-wallpaperengine` — Expected: ok.

- [ ] **Step 7: Commit**

```bash
git add src/app/cli_options.h src/app/cli_options.cpp src/app/cli_args.cpp src/shared/core/utils.h \
        src/shared/core/utils.cpp src/app/main.cpp config.example.json tests/cli_args_test.cpp
git commit -m "feat(cli): add --transition-mode and transition_mode config key"
```

---

### Task 5: Thread the active audio group through context, sound layers, and video

**Files:**
- Modify: `src/shared/core/engine_context.h`
- Modify: `src/wallpaper/2d/layers/sound/sound_layer.h`
- Modify: `src/wallpaper/2d/layers/sound/sound_layer.cpp`
- Modify: `src/shared/assets/asset_manager.h`
- Modify: `src/shared/assets/asset_manager.cpp`

**Interfaces:**
- Consumes: `AudioEngine::GroupId` from Task 1.
- Produces: `EngineContext::audio_group`; `AssetManager::setAudioGroup(GroupId)`; sound voices and video
  streams created in the active group.

- [ ] **Step 1: Add the context field**

In `src/shared/core/engine_context.h`, add to `EngineContext` (after `is_pkg`/`runtime_mode`):

```cpp
    AudioEngine::GroupId audio_group = AudioEngine::kDefaultGroup;
```

(`audio_engine.h` is already included transitively via `asset_manager.h`.)

- [ ] **Step 2: Route sound layers into the group**

In `src/wallpaper/2d/layers/sound/sound_layer.h`, add a private member:

```cpp
    AudioEngine::GroupId group_ = AudioEngine::kDefaultGroup;
```

In `sound_layer.cpp::createFromDocument`, after `layer->initFromDocument(object, ctx);` add
`layer->group_ = ctx.audio_group;`. In `playCurrent()` pass the group:

```cpp
    current = AudioEngine::instance().play(paths[current_index], loop, (doc.mute || !visible) ? 0.0f : doc.volume,
                                           false, group_);
```

- [ ] **Step 3: Route video streams into the group**

In `src/shared/assets/asset_manager.h`, add public methods and a member:

```cpp
    void setAudioGroup(AudioEngine::GroupId group) {
        audio_group_ = group;
    }
    // ...
    AudioEngine::GroupId audio_group_ = AudioEngine::kDefaultGroup;
```

In `asset_manager.cpp` `updateVideoTextures`, change the stream creation to include the group:

```cpp
                video.audio_stream = AudioEngine::instance().createStream(48000, 2, audio_group_);
```

- [ ] **Step 4: Build to verify**

Run: `xmake build linux-wallpaperengine`
Expected: ok.

- [ ] **Step 5: Commit**

```bash
git add src/shared/core/engine_context.h src/wallpaper/2d/layers/sound src/shared/assets
git commit -m "feat(audio): play a wallpaper's sound and video audio in its group"
```

---

### Task 6: Audio crossfade + freeze fallback in WallpaperManager

**Files:**
- Modify: `src/wallpaper/wallpaper_manager.h`
- Modify: `src/wallpaper/wallpaper_manager.cpp`

**Interfaces:**
- Consumes: `AudioEngine` groups (Task 1); `TransitionConfig.continue_previous` (Task 2);
  `SwitchRequest::continue_previous` (Task 3); `EngineContext::audio_group` and
  `AssetManager::setAudioGroup` (Task 5).
- Produces: per-switch group crossfade; `updateTransition(dt)` drives both the visual transition and the
  audio gain ramp.

- [ ] **Step 1: Add manager state**

In `src/wallpaper/wallpaper_manager.h`, add `#include "shared/audio/audio_engine.h"` and private members:

```cpp
    AudioEngine::GroupId fading_group_ = AudioEngine::kDefaultGroup;
    AudioEngine::GroupId active_group_ = AudioEngine::kDefaultGroup;
    bool audio_crossfade_ = false;
```

Change `void updateTransition(float dt) { ... }` from inline to a declaration defined in the `.cpp`:

```cpp
    void updateTransition(float dt);
```

- [ ] **Step 2: Implement the group lifecycle in `beginPendingSwitch`**

In `src/wallpaper/wallpaper_manager.cpp::beginPendingSwitch`, after resolving `config` and before
capturing the frame:

```cpp
    if (config.continue_previous) {
        LOG_TAG_W("WALLPAPER_MGR",
                  "transition-mode 'continue' is not implemented yet (P2); using freeze");
        config.continue_previous = false;
    }

    AudioEngine& audio = AudioEngine::instance();
    // A new switch during an in-flight crossfade supersedes the older outgoing
    // group; destroy it so it cannot leak (its visual is already replaced too).
    if (audio_crossfade_) {
        audio.destroyGroup(fading_group_);
        fading_group_ = AudioEngine::kDefaultGroup;
        audio_crossfade_ = false;
    }
    const bool has_old = active_wallpaper_ && ctx.audio_group != AudioEngine::kDefaultGroup;
    const AudioEngine::GroupId old_group = ctx.audio_group;
    const bool crossfade = has_old && config.selection != lwe::transition::kSelectionNone;
    if (crossfade) audio.beginGroupFade(old_group);

    const AudioEngine::GroupId new_group = audio.createGroup();
    audio.setGroupVolume(new_group, crossfade ? 0.0f : 1.0f);
    ctx.audio_group = new_group;
    ctx.asset_mgr.setAudioGroup(new_group);
```

Keep the existing capture/`switchWallpaper` code. On the failure path
(`!switchWallpaper(...)`), before `transition_.hold()`, add:

```cpp
        audio.destroyGroup(new_group);
        ctx.audio_group = old_group;
        ctx.asset_mgr.setAudioGroup(old_group);
        if (crossfade) {
            audio.setGroupVolume(old_group, 1.0f);
            audio.cancelGroupFade(old_group);
        }
```

On success (before `return true;`), record the crossfade:

```cpp
    fading_group_ = crossfade ? old_group : AudioEngine::kDefaultGroup;
    active_group_ = new_group;
    audio_crossfade_ = crossfade;
```

When `config.selection == lwe::transition::kSelectionNone` and `has_old`, destroy the old group immediately
(audio matches the hard visual cut):

```cpp
    if (has_old && config.selection == lwe::transition::kSelectionNone) audio.destroyGroup(old_group);
```

Place that on the success path (after `switchWallpaper` succeeds).

- [ ] **Step 3: Define `updateTransition` with the audio ramp**

In `src/wallpaper/wallpaper_manager.cpp`, add:

```cpp
void WallpaperManager::updateTransition(float dt) {
    transition_.update(dt);

    if (!audio_crossfade_) return;
    AudioEngine& audio = AudioEngine::instance();
    if (transition_.active()) {
        const float progress = transition_.progress();
        audio.setGroupVolume(fading_group_, 1.0f - progress);
        audio.setGroupVolume(active_group_, progress);
        return;
    }
    // Transition finished: kill the outgoing group and leave the new one at full.
    audio.destroyGroup(fading_group_);
    audio.setGroupVolume(active_group_, 1.0f);
    fading_group_ = AudioEngine::kDefaultGroup;
    audio_crossfade_ = false;
}
```

Also, in `WallpaperManager::clear()`, reset audio bookkeeping so a torn-down instance does not keep a
dangling group: after resetting `active_wallpaper_`, call
`AudioEngine::instance().destroyGroup(fading_group_);` and reset `fading_group_`/`audio_crossfade_`
(the `active_group_` is left as `ctx.audio_group`; the next switch fades it).

- [ ] **Step 4: Build and run the full suite**

Run: `xmake build linux-wallpaperengine && xmake test`
Expected: build ok; all tests pass (including the new `audio_engine_tests`).

- [ ] **Step 5: Manual audio crossfade check (scene wallpaper with looping sound)**

Launch a scene wallpaper that has a looping sound, then trigger a switch with a transition
(`--transition fade --transition-duration 3000`) via a second launch. Confirm the outgoing sound ramps
down while the incoming wallpaper's sound ramps up with no hard cut or click, and that the incoming sound
ends at full volume after the transition.

- [ ] **Step 6: Commit**

```bash
git add src/wallpaper/wallpaper_manager.h src/wallpaper/wallpaper_manager.cpp
git commit -m "feat(transition): crossfade wallpaper audio and add freeze mode plumbing"
```

---

### Task 7: Documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/features.md`
- Modify: `docs/superpowers/plans/2026-10-04-wallpaper-transition-modes.md` (check off tasks as they land)

**Interfaces:**
- Consumes: the finished P1 behavior.
- Produces: user-facing docs.

- [ ] **Step 1: Document the flag and default**

In `README.md`, next to the existing `--transition` / `--transition-duration` docs, add
`--transition-mode freeze|continue` and the `transition_mode` config key. State the default is `freeze`
and that `continue` is not implemented yet (falls back to `freeze`).

- [ ] **Step 2: Document behavior and the P1 audio limitation**

In `docs/features.md`, add a short section: both modes crossfade audio (old out, new in) over the
transition; `freeze` keeps the outgoing visual frozen; `continue` (planned) keeps it animating. Note the
P1 limitation that video/web audio is not crossfaded yet (the decoder is released at swap; fixed in P2).

- [ ] **Step 3: Commit**

```bash
git add README.md docs/features.md docs/superpowers/plans/2026-10-04-wallpaper-transition-modes.md
git commit -m "docs: describe transition modes and audio crossfade"
```

---

## Final verification

- [ ] `xmake format` — clean.
- [ ] `xmake check` — All checks passed.
- [ ] `xmake test` — all tests pass (existing 21 + `audio_engine_tests`).
- [ ] Re-read the Review Focus list; confirm each case is handled by Tasks 4/6 (unknown mode,
      rapid switches, failed switch, first load, `none`).
