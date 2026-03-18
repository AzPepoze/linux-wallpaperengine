const std = @import("std");
const sokol = @import("sokol");
const material_pass = @import("material_pass.zig");

const sg = sokol.gfx;

pub const RenderPass = struct {
    name: []const u8,
    effect: ?material_pass.MaterialPass,
    input_image: ?sg.Image,
    output_image: ?sg.Image,
    clear_color: [4]f32,
    enabled: bool,
};
