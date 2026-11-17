#include "capture/capture_session.h"

#include <cstdio>
#include <filesystem>
#include <utility>

#include "shared/core/logger.h"
#include "shared/graphics/backend/gpu_readback.h"
#include "shared/graphics/backend/surface.h"

namespace capture {

namespace {

CaptureSession* g_active_session = nullptr;

std::string resolveOutputDir(const CaptureOptions& options, const std::string& wallpaper_name) {
    if (!options.dir.empty()) return options.dir;
    const std::string name = wallpaper_name.empty() ? "wallpaper" : wallpaper_name;
    return "./captures/" + name;
}

// Matches the "shot-0001" prefix of shotFileName, so a bundle folder sits next to its PNG.
std::string shotDirName(int index) {
    char name[32];
    snprintf(name, sizeof(name), "shot-%04d", index);
    return name;
}

}  // namespace

CaptureSession::CaptureSession(const CaptureOptions& options, const std::string& wallpaper_name)
    : options_(options),
      wallpaper_name_(wallpaper_name),
      dir_(resolveOutputDir(options, wallpaper_name)),
      schedule_(options) {
    if (ensureDirectory(dir_)) return;

    LOG_E("capture: cannot create output directory %s", dir_.c_str());
    done_ = true;
    surface::requestQuit();
}

CaptureSession::~CaptureSession() {
    // Shutdown path: write the output but do not request quit again.
    if (!done_) writeOutput();
}

void CaptureSession::prepareFrame(double scene_time, uint64_t /*frame_index*/) {
    shot_pending_ = !done_ && schedule_.shotDue(scene_time, true);
    if (!shot_pending_) return;

    pending_index_ = schedule_.shotsTaken();
    pending_requested_ = schedule_.requestedTime();
    pending_shot_dir_ = options_.dump ? (std::filesystem::path(dir_) / shotDirName(pending_index_)).string() : "";
}

void CaptureSession::onFrame(double scene_time, uint64_t /*frame_index*/, sg_image frame) {
    if (done_) return;

    if (shot_pending_) takeShot(scene_time, frame);
    shot_pending_ = false;
    if (schedule_.finished()) finish();
}

bool CaptureSession::dumpThisFrame() const { return shot_pending_ && options_.dump; }

std::string CaptureSession::shotDir() const { return dumpThisFrame() ? pending_shot_dir_ : std::string(); }

bool CaptureSession::finished() const { return done_; }

void CaptureSession::finish() {
    if (done_) return;

    writeOutput();
    surface::requestQuit();
}

void CaptureSession::takeShot(double scene_time, sg_image frame) {
    GpuImageReadbackResult image = gpu_readback_image_rgba8(frame);
    if (!image.success) {
        LOG_E("capture: could not read back the frame at %.3fs", scene_time);
        finish();
        return;
    }

    const int index = pending_index_;
    const double requested = pending_requested_;
    const std::string file = options_.file_name.empty() ? shotFileName(index, requested) : options_.file_name;
    const std::string path = (std::filesystem::path(dir_) / file).string();
    width_ = image.width;
    height_ = image.height;

    writer_.submit(path, image.width, image.height, std::move(image.rgba_data));
    shots_.push_back(ShotRecord{index, requested, scene_time, file});
}

void CaptureSession::writeOutput() {
    writer_.drain();
    // A single --screenshot file is written alone, without a manifest.
    if (!options_.file_name.empty()) {
        done_ = true;
        return;
    }
    if (writeManifest(dir_, manifestInfo())) {
        LOG_I("capture: wrote %zu shot(s) to %s", shots_.size(), dir_.c_str());
    } else {
        LOG_E("capture: failed to write manifest in %s", dir_.c_str());
    }
    done_ = true;
}

ManifestInfo CaptureSession::manifestInfo() const {
    ManifestInfo info;
    info.wallpaper = wallpaper_name_;
    info.width = width_ > 0 ? width_ : options_.width;
    info.height = height_ > 0 ? height_ : options_.height;
    info.fps = options_.fps;
    info.shots = shots_;
    return info;
}

void setActiveSession(CaptureSession* session) { g_active_session = session; }

void prepareFrameCapture(double scene_time, uint64_t frame_index) {
    if (!g_active_session) return;
    g_active_session->prepareFrame(scene_time, frame_index);
}

void onFrameComplete(double scene_time, uint64_t frame_index, sg_image frame) {
    if (!g_active_session) return;
    g_active_session->onFrame(scene_time, frame_index, frame);
}

bool captureDumpThisFrame() { return g_active_session && g_active_session->dumpThisFrame(); }

std::string currentShotDir() { return g_active_session ? g_active_session->shotDir() : std::string(); }

bool captureFinished() { return !g_active_session || g_active_session->finished(); }

}  // namespace capture
