#include "wallpaper/wallpaper_manager.h"

#include <cjson/cJSON.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <utility>

#include "app/control/control_server.h"
#include "app/package_extractor.h"
#include "shared/assets/shared_assets.h"
#include "shared/core/logger.h"
#include "shared/core/task_pool.h"
#include "shared/graphics/backend/surface.h"
#include "wallpaper/2d/camera/parallax.h"
#include "wallpaper/2d/parser/scene_parser.h"
#include "wallpaper/2d/scene_2d_wallpaper.h"
#include "wallpaper/2d/script/scene_scripts.h"
#include "wallpaper/2d/script/script_engine.h"
#include "wallpaper/prepared_load.h"
#include "wallpaper/project_info.h"
#include "wallpaper/transition/transition_audio.h"
#include "wallpaper/user_properties.h"
#include "wallpaper/video/video_wallpaper.h"
#include "wallpaper/wallpaper_loader.h"

namespace {
std::string prepareReplacementRoot(const SwitchRequest& request) {
    if (isVideoFile(request.path.c_str())) return request.path;
    const bool package_file = request.is_pkg || isPackageFile(request.path);
    const std::string package_path = package_file ? request.path : request.path + "/scene.pkg";
    std::error_code error;
    if (std::filesystem::is_regular_file(package_path, error) && vfs::mount(package_path.c_str())) {
        if (vfs::exists("pkg:/scene.json")) return vfs::kRoot;
        vfs::unmount();
    }
    return prepareAssetRoot({request.path, package_file});
}

TaskPool& preparationPool() {
    // A separate serial queue stops rapid requests from exhausting the general pool.
    static TaskPool pool(1);
    return pool;
}

bool isSameWallpaper(const WallpaperInstance& active, const SwitchRequest& request) {
    if (request.path.empty() || request.is_pkg != active.state.is_pkg) return false;
    return std::filesystem::path(active.state.source_path).lexically_normal() ==
           std::filesystem::path(request.path).lexically_normal();
}

Layer* findLayer(const EngineContext& ctx, uint32_t object_id) {
    for (Layer* layer : ctx.scene.layers) {
        if (layer->scene_object_id == object_id) return layer;
    }
    return nullptr;
}

// The object JSON without its top-level "visible", so a visibility change can be told apart from others.
std::string jsonWithoutVisible(const std::string& json) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) return json;
    cJSON_DeleteItemFromObjectCaseSensitive(root, "visible");
    char* printed = cJSON_PrintUnformatted(root);
    std::string result = printed ? printed : json;
    cJSON_free(printed);
    cJSON_Delete(root);
    return result;
}

// Collects the layers whose visibility changes. False when anything else changed, or a layer cannot take it.
bool planVisibilityChanges(const EngineContext& ctx, const std::vector<wallpaper_engine::SceneObjectDocument>& fresh,
                           std::vector<std::pair<Layer*, bool>>& toggles) {
    if (fresh.size() != ctx.scene.bound_objects.size()) return false;
    for (size_t i = 0; i < fresh.size(); ++i) {
        const wallpaper_engine::SceneObjectDocument& old_object = ctx.scene.bound_objects[i];
        const wallpaper_engine::SceneObjectDocument& new_object = fresh[i];
        if (old_object.node.id != new_object.node.id ||
            jsonWithoutVisible(old_object.raw_json) != jsonWithoutVisible(new_object.raw_json))
            return false;
        if (old_object.visible == new_object.visible) continue;
        Layer* layer = findLayer(ctx, new_object.node.id);
        // A visibility script owns the flag every frame, so a stored value would not stick.
        if (!layer || !new_object.visible_script.empty()) return false;
        toggles.emplace_back(layer, new_object.visible);
    }
    return true;
}

// Re-resolves the scene's bindings for `properties` and toggles layer visibility; false when a rebuild is needed.
bool applyBindingsInPlace(EngineContext& ctx, const UserProperties& properties) {
    const ProjectInfo info = ProjectInfo::detect(ctx.asset_root);
    wallpaper_engine::SceneDocument fresh;
    if (!wallpaper_engine::parseSceneFile(info.entry.c_str(), fresh, &properties)) return false;
    std::vector<std::pair<Layer*, bool>> toggles;
    if (!planVisibilityChanges(ctx, fresh.objects, toggles)) return false;
    for (const auto& [layer, visible] : toggles) layer->setVisible(visible);
    ctx.scene.bound_objects = std::move(fresh.objects);
    return true;
}

// Updates the live scene without reloading it; false when the change needs the scene rebuilt.
bool applyPropertiesInPlace(EngineContext& ctx, const std::vector<std::pair<std::string, std::string>>& changes) {
    if (!ctx.scene.scripts) return false;
    UserProperties next = ctx.user_properties;
    for (const auto& [key, value] : changes) next.setFromString(key, value);
    if (touchesBoundKey(ctx.scene.bound_user_keys, changes) && !applyBindingsInPlace(ctx, next)) {
        LOG_TAG_I("WALLPAPER_MGR", "Property change needs the scene rebuilt; reloading");
        return false;
    }
    ctx.user_properties = std::move(next);
    // Scripts see the new values through engine.userProperties and applyUserProperties on the next update.
    ctx.scene.scripts->setUserProperties(ctx.user_properties);
    LOG_TAG_I("WALLPAPER_MGR", "Applied %zu property change(s) in place", changes.size());
    return true;
}
}  // namespace

struct WallpaperManager::LoadJob : PreparedLoad {
    LoadJob() : PreparedLoad(std::future<bool>{}) {}
    SwitchRequest request;
    TransitionConfig config;
    ProjectInfo info;
    wallpaper_engine::SceneDocument document;
    std::unique_ptr<WallpaperInstance> instance;
    std::string error;
    bool held_snapshot = false;
    unsigned preparation_frames = 0;
    double max_frame_ms = 0.0;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_frame = started;
};

bool WallpaperManager::load(const std::string& scene_directory, EngineContext& ctx) {
    if (scene_directory.empty()) return false;

    const ProjectInfo info = ProjectInfo::detect(scene_directory);
    if (!WallpaperLoader::canLoad(info)) return false;

    // The outgoing wallpaper stays alive and playing until the transition ends.
    if (active_instance_) outgoing_instance_ = std::move(active_instance_);

    auto instance = std::make_unique<WallpaperInstance>();
    instance->package = vfs::currentPackage();
    instance->pass_action = ctx.pass_action;
    instance->audio_group = AudioEngine::instance().createGroup();
    LOG_TAG_I("WALLPAPER_MGR", "New instance audio group=%u for %s", (unsigned)instance->audio_group,
              scene_directory.c_str());
    if (shared_assets_) instance->assets.attachShared(shared_assets_);
    instance->state.wallpaper_path = scene_directory;
    instance->state.source_path = ctx.wallpaper_path;
    instance->state.is_pkg = ctx.is_pkg;
    // Copy the user's scaling into the new instance before activation replaces SceneState.
    instance->state.scene.scaling_mode = ctx.scene.scaling_mode;

    activateInstance(ctx, *instance, active_view_);
    // Incoming audio starts silent when a transition captured a frame; otherwise full volume.
    AudioEngine::instance().setGroupVolume(instance->audio_group, transition_.active() ? 0.0f : 1.0f);

    auto wallpaper = WallpaperLoader::load(info, ctx);
    if (!wallpaper) {
        AudioEngine::instance().destroyGroup(instance->audio_group);
        if (outgoing_instance_) {
            activateInstance(ctx, *outgoing_instance_, active_view_);
            active_instance_ = std::move(outgoing_instance_);
        } else {
            deactivateInstance(ctx, active_view_);
        }
        return false;
    }
    instance->wallpaper = std::move(wallpaper);
    active_instance_ = std::move(instance);
    const char* scaling = ctx.scene.scaling_mode == SCALING_COVER     ? "cover"
                          : ctx.scene.scaling_mode == SCALING_STRETCH ? "stretch"
                                                                      : "fit";
    LOG_TAG_I("OPTIONS", "Loaded wallpaper scaling: %s (scale=%.3f, offsets=%.1f, %.1f)", scaling,
              ctx.scene.render_scale, ctx.scene.offset_x, ctx.scene.offset_y);
    return true;
}

void WallpaperManager::destroyInstance(EngineContext& ctx, std::unique_ptr<WallpaperInstance>& instance) {
    if (!instance) return;
    // Make it the active view so its runtime cleanup tears down its own layers.
    activateInstance(ctx, *instance, active_view_);
    instance->wallpaper.reset();  // ~Wallpaper -> clear() -> Scene2DRuntime::cleanup()
    instance->assets.clearVideoTextures();
    AudioEngine::instance().destroyGroup(instance->audio_group);
    instance->audio_group = AudioEngine::kDefaultGroup;
    instance.reset();
    active_view_ = nullptr;  // it pointed into the destroyed instance
    if (active_instance_)
        activateInstance(ctx, *active_instance_, active_view_);
    else {
        ctx.asset_mgr = nullptr;
        ctx.audio_group = AudioEngine::kDefaultGroup;
        ctx.scene = {};
        vfs::bindPackage({});
        ScriptEngine::instance().setActiveScope(nullptr);
        ScriptEngine::instance().setCreationScope(nullptr);
    }
}

void WallpaperManager::update(float dt, EngineContext& ctx) {
    if (outgoing_instance_ && !transition_.active()) {
        if (fading_group_ != AudioEngine::kDefaultGroup) AudioEngine::instance().setGroupVolume(fading_group_, 0.0f);
        fading_group_ = AudioEngine::kDefaultGroup;
        auto retired = std::make_shared<LoadJob>();
        retired->cancel();
        retired->instance = std::move(outgoing_instance_);
        retired_jobs_.push_back(std::move(retired));
    }

    if (outgoing_instance_ && transition_.active() && !transition_.live()) tickOutgoingAudio(ctx, dt);
    if (active_instance_ && active_instance_->wallpaper) active_instance_->wallpaper->update(dt, ctx);
}

bool WallpaperManager::stepOutgoingForTransition(EngineContext& ctx, float dt) {
    if (!outgoing_instance_ || !transition_.active() || !transition_.live()) return false;

    activateInstance(ctx, *outgoing_instance_, active_view_);
    auto* scene = dynamic_cast<Scene2DWallpaper*>(outgoing_instance_->wallpaper.get());
    Scene2DRuntime* runtime = scene ? scene->getRuntime() : nullptr;
    bool ok = false;
    if (runtime && outgoing_instance_->wallpaper) {
        outgoing_instance_->wallpaper->update(dt, ctx);
        outgoing_instance_->assets.updateVideoTextures(dt, ctx.scene.layers);
        parallax_update(ctx, dt, surface::width(), surface::height());
        runtime->setForceOffscreen(true);
        runtime->draw();
        transition_.updateSource(ctx, runtime->composedView(), runtime->composedImage(), surface::width(),
                                 surface::height());
        runtime->setForceOffscreen(false);
        ok = true;
    }
    activateInstance(ctx, *active_instance_, active_view_);
    return ok;
}

void WallpaperManager::tickOutgoingAudio(EngineContext& ctx, float dt) {
    if (!outgoing_instance_) return;
    activateInstance(ctx, *outgoing_instance_, active_view_);
    outgoing_instance_->assets.updateVideoTextures(dt, ctx.scene.layers);
    activateInstance(ctx, *active_instance_, active_view_);
}

void WallpaperManager::render(EngineContext& ctx) {
    if (active_instance_ && active_instance_->wallpaper) active_instance_->wallpaper->render(ctx);
}

void WallpaperManager::onResize(float width, float height) {
    if (active_instance_ && active_instance_->wallpaper) active_instance_->wallpaper->onResize(width, height);
}

void WallpaperManager::handleInput(const sapp_event* event, EngineContext& ctx) {
    if (active_instance_ && active_instance_->wallpaper) active_instance_->wallpaper->handleInput(event, ctx);
}

void WallpaperManager::pause() {
    if (active_instance_ && active_instance_->wallpaper) active_instance_->wallpaper->pause();
}

void WallpaperManager::resume() {
    if (active_instance_ && active_instance_->wallpaper) active_instance_->wallpaper->resume();
}

void WallpaperManager::clear(EngineContext& ctx) {
    if (load_job_) {
        load_job_->cancelled = true;
        retired_jobs_.push_back(std::move(load_job_));
    }
    for (auto& job : retired_jobs_) {
        job->cancelled = true;
        job->drain();
        destroyInstance(ctx, job->instance);
    }
    retired_jobs_.clear();

    if (fading_group_ != AudioEngine::kDefaultGroup) AudioEngine::instance().setGroupVolume(fading_group_, 0.0f);
    fading_group_ = AudioEngine::kDefaultGroup;
    audio_crossfade_ = false;
    if (outgoing_instance_) destroyInstance(ctx, outgoing_instance_);
    if (active_instance_) {
        LOG_TAG_I("WALLPAPER_MGR", "Clearing active wallpaper instance...");
        destroyInstance(ctx, active_instance_);
        LOG_TAG_I("WALLPAPER_MGR", "Active wallpaper cleared.");
    }
}

void WallpaperManager::pollControl(EngineContext& ctx) {
    if (!control_) return;
    std::vector<SwitchRequest> requests;
    control_->poll(requests);
    for (SwitchRequest& request : requests) {
        if (request.toggle_debug_ui) {
            ctx.debug.show_ui = !ctx.debug.show_ui;
            continue;
        }
        if (active_instance_ && isSameWallpaper(*active_instance_, request) && !request.properties.empty()) {
            if (applyPropertiesInPlace(ctx, request.properties)) continue;
            // A bound key needs a rebuild: reload with every current value so earlier live changes survive it.
            UserProperties merged = ctx.user_properties;
            for (const auto& [key, value] : request.properties) merged.setFromString(key, value);
            request.properties = merged.toStrings();
        }
        // Keep only the newest request: an in-flight switch should not queue up.
        pending_switch_ = std::move(request);
    }
}

bool WallpaperManager::takePendingSwitch(SwitchRequest& out) {
    if (!pending_switch_) return false;
    out = std::move(*pending_switch_);
    pending_switch_.reset();
    return true;
}

bool WallpaperManager::beginPendingSwitch(EngineContext& ctx) {
    SwitchRequest request;
    const bool requested = takePendingSwitch(request);
    if (requested) {
        if (load_job_) {
            if (load_job_->held_snapshot) transition_.cancel();
            load_job_->cancel();
            retired_jobs_.push_back(std::move(load_job_));
        }
        if (outgoing_instance_) {
            if (fading_group_ != AudioEngine::kDefaultGroup)
                AudioEngine::instance().setGroupVolume(fading_group_, 0.0f);
            fading_group_ = AudioEngine::kDefaultGroup;
            audio_crossfade_ = false;
            if (active_instance_) AudioEngine::instance().setGroupVolume(active_instance_->audio_group, 1.0f);
            auto retired = std::make_shared<LoadJob>();
            retired->cancel();
            retired->instance = std::move(outgoing_instance_);
            retired_jobs_.push_back(std::move(retired));
        }
        transition_.cancel();
        auto job = std::make_shared<LoadJob>();
        job->request = request;
        job->config = transition_config_;
        job->config.selection = request.transition;
        job->config.duration_ms = request.transition_time_ms > 0 ? request.transition_time_ms : 1000;
        job->config.continue_previous = request.continue_previous;
        if (job->config.selection == lwe::transition::kSelectionRandom) {
            random_seed_ = random_seed_ * 1664525u + 1013904223u;
            job->config.selection = lwe::transition::pickRandomEffect(random_seed_);
        }
        job->instance = std::make_unique<WallpaperInstance>();
        auto& instance = *job->instance;
        instance.pass_action = ctx.pass_action;
        instance.state.scene.scaling_mode = ctx.scene.scaling_mode;
        if (request.scaling == "fit")
            instance.state.scene.scaling_mode = SCALING_FIT;
        else if (request.scaling == "stretch")
            instance.state.scene.scaling_mode = SCALING_STRETCH;
        else if (request.scaling == "default" || request.scaling == "fill")
            instance.state.scene.scaling_mode = SCALING_COVER;
        instance.state.is_pkg = request.is_pkg;
        instance.state.wallpaper_path = request.path;
        instance.state.source_path = request.path;
        if (shared_assets_) instance.assets.attachShared(shared_assets_);
        job->preparation = preparationPool().enqueue([job] {
            try {
                if (job->cancelled) return false;
                vfs::ScopedBinding binding({});
                const auto& request = job->request;
                const std::string root = prepareReplacementRoot(request);
                if (root.empty() || job->cancelled) return false;
                job->instance->package = vfs::currentPackage();
                job->info = ProjectInfo::detect(root);
                if (!WallpaperLoader::canLoad(job->info)) return false;
                job->instance->state.asset_root = root;
                job->instance->state.user_properties =
                    WallpaperLoader::prepareProperties(job->info, request.properties);
                if (job->info.type == ProjectType::Scene &&
                    !wallpaper_engine::parseSceneFile(job->info.entry.c_str(), job->document,
                                                      &job->instance->state.user_properties))
                    return false;
                job->instance->assets.prepareWallpaper(root.c_str());
                if (job->info.type == ProjectType::Scene && !job->instance->assets.prepareSceneAssets(job->document))
                    return false;
                if (job->info.type == ProjectType::Video)
                    if (!job->instance->assets.prepareVideo(job->info.entry.c_str())) return false;
                return !job->cancelled;
            } catch (const std::exception& error) {
                job->error = error.what();
                return false;
            }
        });
        if (!request.continue_previous && job->config.selection != lwe::transition::kSelectionNone) {
            if (auto* scene = dynamic_cast<Scene2DWallpaper*>(getActiveWallpaper())) {
                if (auto* runtime = scene->getRuntime()) {
                    runtime->setForceOffscreen(true);
                    runtime->draw();
                    job->held_snapshot = transition_.begin(ctx, runtime->composedView(), runtime->composedImage(),
                                                           surface::width(), surface::height(), job->config);
                    runtime->setForceOffscreen(false);
                    if (job->held_snapshot) {
                        transition_.setLive(false);
                        transition_.hold();
                    }
                }
            }
        }
        load_job_ = std::move(job);
        if (request.has_fps) ctx.fps_limit = request.fps;
        if (request.has_volume || request.has_muted) {
            auto& audio = AudioEngine::instance();
            float volume = request.has_volume ? request.volume / 100.0f : audio.masterVolume();
            if (request.has_muted && request.muted) volume = 0.0f;
            audio.setMasterVolume(volume);
        }
    }
    pollLoad(ctx);
    return requested;
}

void WallpaperManager::pollLoad(EngineContext& ctx) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
    for (auto it = retired_jobs_.begin(); it != retired_jobs_.end();) {
        auto& job = *it;
        if (job->preparation.valid() &&
            job->preparation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        if (job->preparation.valid()) job->preparation.get();
        if (job->instance->audio_group != AudioEngine::kDefaultGroup)
            AudioEngine::instance().setGroupVolume(job->instance->audio_group, 0.0f);
        if (job->instance->wallpaper) {
            activateInstance(ctx, *job->instance, active_view_);
            auto* scene = dynamic_cast<Scene2DWallpaper*>(job->instance->wallpaper.get());
            const bool cleaned = !scene || scene->stepCleanup(std::chrono::milliseconds(2));
            if (active_instance_) activateInstance(ctx, *active_instance_, active_view_);
            if (!cleaned) {
                ++it;
                break;
            }
        }
        destroyInstance(ctx, job->instance);
        it = retired_jobs_.erase(it);
        break;
    }
    if (active_instance_) {
        ScriptEngine::instance().setActiveScope(ctx.scene.scripts);
        ScriptEngine::instance().setCreationScope(ctx.scene.scripts);
    }
    if (!load_job_ || std::chrono::steady_clock::now() >= deadline) return;
    auto& job = *load_job_;
    ++job.preparation_frames;
    const auto frame_time = std::chrono::steady_clock::now();
    job.max_frame_ms =
        std::max(job.max_frame_ms, std::chrono::duration<double, std::milli>(frame_time - job.last_frame).count());
    job.last_frame = frame_time;
    if (job.state == LoadJob::State::Preparing) {
        if (job.preparation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        if (!job.pollPreparation()) {
            LOG_TAG_E("WALLPAPER_MGR", "Preparation failed for %s: %s", job.request.path.c_str(), job.error.c_str());
            if (job.held_snapshot) transition_.cancel();
            job.state = LoadJob::State::Failed;
            retired_jobs_.push_back(std::move(load_job_));
            return;
        }
        LOG_TAG_I("WALLPAPER_MGR", "CPU preparation ready for %s after %.2fms", job.request.path.c_str(),
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - job.started).count());
        job.instance->audio_group = AudioEngine::instance().createGroup();
        AudioEngine::instance().setGroupVolume(job.instance->audio_group, 0.0f);
        activateInstance(ctx, *job.instance, active_view_);
        ScriptEngine::instance().setAssetsDir(std::string(ctx.engine_path) + "/assets");
        ScriptEngine::instance().setWallpaperId(std::filesystem::path(job.request.path).filename().string());
        if (job.info.type == ProjectType::Scene) {
            auto scene = std::make_unique<Scene2DWallpaper>(ctx);
            scene->beginLoadDocument(std::move(job.document), ctx);
            job.instance->wallpaper = std::move(scene);
        } else {
            if (job.info.type == ProjectType::Video) {
                job.instance->assets.setVideoPlayback(job.info.video.rate, job.info.video.volume);
                if (job.info.video.fit == VideoFit::Fill && ctx.scene.scaling_mode == SCALING_FIT)
                    ctx.scene.scaling_mode = SCALING_COVER;
                auto video = std::make_unique<VideoWallpaper>(ctx);
                if (video->load(job.info.entry, ctx)) {
                    video->beginPrewarmLoaded(ctx);
                    job.instance->wallpaper = std::move(video);
                }
            } else {
                const auto overrides = ctx.cli_properties;
                ctx.cli_properties = job.request.properties;
                job.instance->wallpaper = WallpaperLoader::load(job.info, ctx);
                ctx.cli_properties = overrides;
            }
        }
        job.state = LoadJob::State::Graphics;
        if (active_instance_) activateInstance(ctx, *active_instance_, active_view_);
    }
    if (job.state == LoadJob::State::Graphics && std::chrono::steady_clock::now() < deadline) {
        activateInstance(ctx, *job.instance, active_view_);
        auto* scene = dynamic_cast<Scene2DWallpaper*>(job.instance->wallpaper.get());
        const bool done = !scene || scene->stepLoad(std::max(std::chrono::milliseconds(1),
                                                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                                                 deadline - std::chrono::steady_clock::now())),
                                                    ctx);
        const bool failed = !job.instance->wallpaper || (scene && scene->loadingFailed());
        if (active_instance_) activateInstance(ctx, *active_instance_, active_view_);
        if (active_instance_) {
            ScriptEngine::instance().setActiveScope(ctx.scene.scripts);
            ScriptEngine::instance().setCreationScope(ctx.scene.scripts);
        }
        if (failed) {
            LOG_TAG_E("WALLPAPER_MGR", "Graphics setup failed for %s", job.request.path.c_str());
            if (job.held_snapshot) transition_.cancel();
            job.state = LoadJob::State::Failed;
            retired_jobs_.push_back(std::move(load_job_));
        } else if (done) {
            job.instance->assets.releaseDecodedTextures();
            job.markReady();
            if (job.canCommit()) commitLoad(ctx);
        }
    }
}

void WallpaperManager::commitLoad(EngineContext& ctx) {
    const auto request = load_job_->request;
    const auto config = load_job_->config;
    AudioEngine& audio = AudioEngine::instance();
    // A new switch supersedes an in-flight fade: drop its outgoing instance/group.
    if (outgoing_instance_) {
        destroyInstance(ctx, outgoing_instance_);
        audio.setGroupVolume(fading_group_, 0.0f);
        fading_group_ = AudioEngine::kDefaultGroup;
        audio_crossfade_ = false;
    }

    const bool has_old = active_instance_ && ctx.audio_group != AudioEngine::kDefaultGroup;
    const AudioEngine::GroupId old_group = ctx.audio_group;
    const lwe::transition::AudioSwitchPlan plan =
        lwe::transition::planAudioSwitch(active_instance_ != nullptr, has_old, config.selection);

    bool captured = load_job_->held_snapshot;
    if (captured) transition_.startHeld(config);
    if (!captured && config.selection != lwe::transition::kSelectionNone) {
        if (auto* scene = dynamic_cast<Scene2DWallpaper*>(getActiveWallpaper())) {
            if (Scene2DRuntime* runtime = scene->getRuntime()) {
                runtime->setForceOffscreen(true);
                runtime->draw();
                captured = transition_.begin(ctx, runtime->composedView(), runtime->composedImage(), surface::width(),
                                             surface::height(), config);
                if (captured) transition_.setLive(config.continue_previous);
                runtime->setForceOffscreen(false);
            }
        }
    }
    // Without a transition, drop any in-flight or held snapshot so it can't stick.
    if (!captured) transition_.cancel();

    outgoing_instance_ = std::move(active_instance_);
    active_instance_ = std::move(load_job_->instance);
    activateInstance(ctx, *active_instance_, active_view_);
    if (!captured) audio.setGroupVolume(active_instance_->audio_group, 1.0f);

    // Commit at the frame boundary, after the incoming graphics are ready.
    const AudioEngine::GroupId new_group = ctx.audio_group;
    LOG_TAG_I("WALLPAPER_MGR", "audio switch: old_group=%u new_group=%u fade=%d cut=%d", (unsigned)old_group,
              (unsigned)new_group, plan.fade_old ? 1 : 0, plan.destroy_old_now ? 1 : 0);
    if (plan.destroy_old_now) {
        audio.setGroupVolume(old_group, 0.0f);
        if (outgoing_instance_) {
            auto retired = std::make_shared<LoadJob>();
            retired->cancel();
            retired->instance = std::move(outgoing_instance_);
            retired_jobs_.push_back(std::move(retired));
        }
    } else if (plan.fade_old) {
        fading_group_ = old_group;
        active_group_ = new_group;
        audio_crossfade_ = true;
    } else {
        fading_group_ = AudioEngine::kDefaultGroup;
        active_group_ = new_group;
        audio_crossfade_ = false;
    }
    LOG_TAG_I("WALLPAPER_MGR", "Switched to %s after %.2fms preparation; fade=%dms", request.path.c_str(),
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - load_job_->started).count(),
              config.duration_ms);
    LOG_TAG_I("WALLPAPER_MGR", "Preparation playback: %u frames, max presented interval %.2fms",
              load_job_->preparation_frames, load_job_->max_frame_ms);
    // Re-apply the layout so a changed scaling takes effect without a restart.
    onResize((float)surface::width(), (float)surface::height());
    load_job_.reset();
}

void WallpaperManager::updateTransition(float dt) {
    const bool was_active = transition_.active();
    transition_.update(dt);
    if (was_active && !transition_.active()) LOG_TAG_I("WALLPAPER_MGR", "Visual transition completed");

    if (!audio_crossfade_) return;
    AudioEngine& audio = AudioEngine::instance();
    if (transition_.active()) {
        const float progress = transition_.progress();
        audio.setGroupVolume(fading_group_, 1.0f - progress);
        audio.setGroupVolume(active_group_, progress);
        return;
    }
    // Fade finished: the outgoing instance is torn down in update(ctx).
    audio.setGroupVolume(active_group_, 1.0f);
    audio_crossfade_ = false;
}
