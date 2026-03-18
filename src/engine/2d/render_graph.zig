const std = @import("std");
const render_pass = @import("render_pass.zig");

pub const RenderGraph = struct {
    allocator: std.mem.Allocator,
    passes: std.ArrayListUnmanaged(render_pass.RenderPass) = .{},
    backbuffer_pass: render_pass.RenderPass,

    pub fn init(allocator: std.mem.Allocator) RenderGraph {
        return .{
            .allocator = allocator,
            .passes = .{},
            .backbuffer_pass = .{
                .name = "backbuffer",
                .effect = null,
                .input_image = null,
                .output_image = null,
                .clear_color = .{ 0.0, 0.0, 0.0, 1.0 },
                .enabled = true,
            },
        };
    }

    pub fn deinit(self: *RenderGraph) void {
        self.passes.deinit(self.allocator);
    }
};
