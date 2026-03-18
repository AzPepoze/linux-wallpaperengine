const std = @import("std");
const sokol = @import("sokol");
const core = @import("core");
const effect = @import("effect.zig");

const sg = sokol.gfx;

pub const RenderObject = struct {
    object_json: std.json.Value,
    image: ?sg.Image = null,
    offset: core.wallpaper.Vec2 = .{},
    effects: []effect.Effect = &.{},
};
