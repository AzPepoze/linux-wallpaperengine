const std = @import("std");
const sokol = @import("sokol");
const sapp = sokol.app;
const logger = @import("./logger.zig");

/// FrameTimer provides synchronized timing across all subsystems
/// Drives animations, effects, and audio synchronization
pub const FrameTimer = struct {
    frame_count: u64 = 0,
    total_time: f64 = 0.0,
    delta_time: f64 = 0.0,

    frame_times: [120]f64 = undefined,
    frame_index: usize = 0,

    allocator: std.mem.Allocator,

    pub fn init(allocator: std.mem.Allocator) FrameTimer {
        var timer: FrameTimer = .{
            .allocator = allocator,
            .frame_times = undefined,
        };
        @memset(&timer.frame_times, 0.0);
        logger.App.info("FrameTimer initialized", .{});
        return timer;
    }

    pub fn deinit(self: *FrameTimer) void {
        _ = self;
    }

    /// Call once per frame to update timing
    pub fn tick(self: *FrameTimer) void {
        const frame_time = sapp.frameDuration();
        self.frame_times[self.frame_index] = frame_time;
        self.frame_index = (self.frame_index + 1) % 120;

        self.delta_time = frame_time;
        self.total_time += frame_time;
        self.frame_count += 1;
    }

    /// Get current delta time in seconds
    pub fn getDeltaTime(self: *const FrameTimer) f64 {
        return self.delta_time;
    }

    /// Get total elapsed time in seconds
    pub fn getTotalTime(self: *const FrameTimer) f64 {
        return self.total_time;
    }

    /// Get current frame count (0-indexed)
    pub fn getFrameCount(self: *const FrameTimer) u64 {
        return self.frame_count;
    }

    /// Calculate average frame time over last 120 frames (roughly 2 seconds at 60 FPS)
    pub fn getAverageFrameTime(self: *const FrameTimer) f64 {
        var sum: f64 = 0.0;
        for (self.frame_times) |t| {
            sum += t;
        }
        return sum / 120.0;
    }

    /// Calculate current FPS estimate
    pub fn getCurrentFPS(self: *const FrameTimer) f64 {
        const avg = self.getAverageFrameTime();
        if (avg > 0) return 1.0 / avg else return 0.0;
    }
};
