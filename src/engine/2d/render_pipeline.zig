const std = @import("std");
const render_graph = @import("render_graph.zig");
const render_target = @import("render_target.zig");

pub const RenderPipeline = struct {
    allocator: std.mem.Allocator,
    render_graph: render_graph.RenderGraph,
    render_targets: std.ArrayListUnmanaged(render_target.RenderTarget) = .{},

    pub fn init(allocator: std.mem.Allocator) RenderPipeline {
        return .{
            .allocator = allocator,
            .render_graph = render_graph.RenderGraph.init(allocator),
            .render_targets = .{},
        };
    }

    pub fn deinit(self: *RenderPipeline) void {
        for (self.render_targets.items) |*target| {
            target.destroy();
        }
        self.render_targets.deinit(self.allocator);
        self.render_graph.deinit();
    }
};
