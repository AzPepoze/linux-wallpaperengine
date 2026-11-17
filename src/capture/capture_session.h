#ifndef CAPTURE_SESSION_H
#define CAPTURE_SESSION_H

#include <stdint.h>

#include <string>
#include <vector>

#include "capture/capture_options.h"
#include "capture/capture_output.h"
#include "capture/capture_schedule.h"
#include "capture/image_writer.h"
#include "sokol_gfx.h"

namespace capture {

// Takes the screenshots of one run and writes them with a manifest.
class CaptureSession {
   public:
    CaptureSession(const CaptureOptions& options, const std::string& wallpaper_name);
    ~CaptureSession();

    // Called before the frame is drawn: decides whether a shot is due on this frame.
    void prepareFrame(double scene_time, uint64_t frame_index);
    // Called after the frame is committed: reads back and writes the pending shot.
    void onFrame(double scene_time, uint64_t frame_index, sg_image frame);
    // True when the pending shot should get a diagnostics bundle.
    bool dumpThisFrame() const;
    // Folder of the pending shot's bundle; empty when no bundle is due.
    std::string shotDir() const;
    bool finished() const;
    // Writes the manifest and requests quit. Idempotent.
    void finish();

   private:
    void takeShot(double scene_time, sg_image frame);
    void writeOutput();
    ManifestInfo manifestInfo() const;

    CaptureOptions options_;
    std::string wallpaper_name_;
    std::string dir_;
    CaptureSchedule schedule_;
    ImageWriter writer_;
    std::vector<ShotRecord> shots_;
    int width_ = 0;
    int height_ = 0;
    bool done_ = false;

    bool shot_pending_ = false;
    int pending_index_ = 0;
    double pending_requested_ = 0.0;
    std::string pending_shot_dir_;
};

// Sets the session that receives frames. nullptr clears it.
void setActiveSession(CaptureSession* session);
// No-op when no session is active.
void prepareFrameCapture(double scene_time, uint64_t frame_index);
// No-op when no session is active.
void onFrameComplete(double scene_time, uint64_t frame_index, sg_image frame);
// False when no session is active.
bool captureDumpThisFrame();
// Empty when no session is active or no bundle is due.
std::string currentShotDir();
// True when no active session needs more frames.
bool captureFinished();

}  // namespace capture

#endif  // CAPTURE_SESSION_H
