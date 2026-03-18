const std = @import("std");

pub const Level = enum {
    debug,
    info,
    warn,
    err,

    pub fn asString(self: Level) []const u8 {
        return switch (self) {
            .debug => "DEBUG",
            .info => "INFO",
            .warn => "WARN",
            .err => "ERROR",
        };
    }

    pub fn colorCode(self: Level) []const u8 {
        return switch (self) {
            .debug => "\x1b[36m", // Cyan
            .info => "\x1b[34m", // Blue
            .warn => "\x1b[33m", // Yellow
            .err => "\x1b[31m", // Red
        };
    }
};

pub const Category = enum {
    general,
    app,
    scene,
    unpack,
    texture,
    renderer,
    shader,

    pub fn asString(self: Category) []const u8 {
        return switch (self) {
            .general => "GENERAL",
            .app => "APP",
            .scene => "SCENE",
            .unpack => "UNPACK",
            .texture => "TEXTURE",
            .renderer => "RENDERER",
            .shader => "SHADER",
        };
    }

    pub fn colorCode(self: Category) []const u8 {
        return switch (self) {
            .general => "\x1b[37m", // White
            .app => "\x1b[38;5;208m", // Orange
            .scene => "\x1b[32m", // Green
            .unpack => "\x1b[35m", // Magenta
            .texture => "\x1b[33m", // Yellow
            .renderer => "\x1b[34m", // Blue
            .shader => "\x1b[36m", // Cyan
        };
    }
};

pub var current_level: Level = .info;
pub var silent: bool = false;

const color_reset = "\x1b[0m";

pub fn log(level: Level, category: Category, comptime format: []const u8, args: anytype) void {
    if (silent) return;
    if (@intFromEnum(level) < @intFromEnum(current_level)) return;

    // In Zig 0.15.2, std.debug.print is the most reliable way for diagnostic output
    std.debug.print("{s}[{s}]{s}{s}[{s}]{s} " ++ format ++ "\n", .{ level.colorCode(), level.asString(), color_reset, category.colorCode(), category.asString(), color_reset } ++ args);
}

// Global helpers (default to general category)
pub fn debug(comptime format: []const u8, args: anytype) void {
    log(.debug, .general, format, args);
}
pub fn info(comptime format: []const u8, args: anytype) void {
    log(.info, .general, format, args);
}
pub fn warn(comptime format: []const u8, args: anytype) void {
    log(.warn, .general, format, args);
}
pub fn err(comptime format: []const u8, args: anytype) void {
    log(.err, .general, format, args);
}

// Category helpers
pub const Scene = struct {
    pub fn debug(comptime format: []const u8, args: anytype) void {
        log(.debug, .scene, format, args);
    }
    pub fn info(comptime format: []const u8, args: anytype) void {
        log(.info, .scene, format, args);
    }
    pub fn warn(comptime format: []const u8, args: anytype) void {
        log(.warn, .scene, format, args);
    }
    pub fn err(comptime format: []const u8, args: anytype) void {
        log(.err, .scene, format, args);
    }
};

pub const Unpack = struct {
    pub fn debug(comptime format: []const u8, args: anytype) void {
        log(.debug, .unpack, format, args);
    }
    pub fn info(comptime format: []const u8, args: anytype) void {
        log(.info, .unpack, format, args);
    }
    pub fn warn(comptime format: []const u8, args: anytype) void {
        log(.warn, .unpack, format, args);
    }
    pub fn err(comptime format: []const u8, args: anytype) void {
        log(.err, .unpack, format, args);
    }
};

pub const Texture = struct {
    pub fn debug(comptime format: []const u8, args: anytype) void {
        log(.debug, .texture, format, args);
    }
    pub fn info(comptime format: []const u8, args: anytype) void {
        log(.info, .texture, format, args);
    }
    pub fn warn(comptime format: []const u8, args: anytype) void {
        log(.warn, .texture, format, args);
    }
    pub fn err(comptime format: []const u8, args: anytype) void {
        log(.err, .texture, format, args);
    }
};

pub const Renderer = struct {
    pub fn debug(comptime format: []const u8, args: anytype) void {
        log(.debug, .renderer, format, args);
    }
    pub fn info(comptime format: []const u8, args: anytype) void {
        log(.info, .renderer, format, args);
    }
    pub fn warn(comptime format: []const u8, args: anytype) void {
        log(.warn, .renderer, format, args);
    }
    pub fn err(comptime format: []const u8, args: anytype) void {
        log(.err, .renderer, format, args);
    }
};

pub const Shader = struct {
    pub fn debug(comptime format: []const u8, args: anytype) void {
        log(.debug, .shader, format, args);
    }
    pub fn info(comptime format: []const u8, args: anytype) void {
        log(.info, .shader, format, args);
    }
    pub fn warn(comptime format: []const u8, args: anytype) void {
        log(.warn, .shader, format, args);
    }
    pub fn err(comptime format: []const u8, args: anytype) void {
        log(.err, .shader, format, args);
    }
};

pub const App = struct {
    pub fn debug(comptime format: []const u8, args: anytype) void {
        log(.debug, .app, format, args);
    }
    pub fn info(comptime format: []const u8, args: anytype) void {
        log(.info, .app, format, args);
    }
    pub fn warn(comptime format: []const u8, args: anytype) void {
        log(.warn, .app, format, args);
    }
    pub fn err(comptime format: []const u8, args: anytype) void {
        log(.err, .app, format, args);
    }
};
