const std = @import("std");
const sokol = @import("sokol");
const sapp = sokol.app;
const core = @import("core");
const logger = core.logger;

/// InputState tracks current input from Sokol events
pub const InputState = struct {
    mouse_x: f32 = 0.0,
    mouse_y: f32 = 0.0,
    mouse_down: bool = false,

    key_f8_down: bool = false,
    debug_ui_visible: bool = false,

    window_width: i32 = 1280,
    window_height: i32 = 720,

    pub fn init() InputState {
        return .{};
    }

    pub fn updateWindowSize() InputState {
        return .{
            .window_width = sapp.width(),
            .window_height = sapp.height(),
        };
    }
};

/// AppLifecycle manages Sokol callbacks and app state transitions
pub const AppLifecycle = struct {
    allocator: std.mem.Allocator,
    is_initialized: bool = false,

    pub fn init(allocator: std.mem.Allocator) AppLifecycle {
        return .{
            .allocator = allocator,
        };
    }

    pub fn deinit(self: *AppLifecycle) void {
        self.is_initialized = false;
        logger.App.info("AppLifecycle shutdown complete", .{});
    }

    pub fn markInitialized(self: *AppLifecycle) void {
        self.is_initialized = true;
        logger.App.info("AppLifecycle initialized", .{});
    }

    pub fn isReady(self: *const AppLifecycle) bool {
        return self.is_initialized;
    }
};

/// RuntimeState wraps all runtime-essential state that persists across frames
pub const RuntimeState = struct {
    allocator: std.mem.Allocator,
    lifecycle: AppLifecycle,
    input: InputState,

    pkg_path: ?[]const u8 = null,
    folder_path: ?[]const u8 = null,
    scene_data: ?std.json.Parsed(std.json.Value) = null,

    pub fn init(allocator: std.mem.Allocator) RuntimeState {
        return .{
            .allocator = allocator,
            .lifecycle = AppLifecycle.init(allocator),
            .input = InputState.init(),
        };
    }

    pub fn deinit(self: *RuntimeState) void {
        if (self.scene_data) |*data| {
            data.deinit();
        }
        if (self.pkg_path) |path| {
            self.allocator.free(path);
        }
        if (self.folder_path) |path| {
            self.allocator.free(path);
        }
        self.lifecycle.deinit();
        logger.App.info("RuntimeState cleanup complete", .{});
    }

    pub fn setWallpaperPath(self: *RuntimeState, is_pkg: bool, path: []const u8) !void {
        const owned_path = try self.allocator.dupe(u8, path);
        if (is_pkg) {
            if (self.pkg_path) |old| self.allocator.free(old);
            self.pkg_path = owned_path;
        } else {
            if (self.folder_path) |old| self.allocator.free(old);
            self.folder_path = owned_path;
        }
    }

    pub fn updateInput(self: *RuntimeState, new_input: InputState) void {
        self.input = new_input;
    }

    pub fn toggleDebugUI(self: *RuntimeState) void {
        self.input.debug_ui_visible = !self.input.debug_ui_visible;
        logger.App.debug("RuntimeState Debug UI: {}", .{self.input.debug_ui_visible});
    }
};
