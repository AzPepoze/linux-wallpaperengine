const std = @import("std");
const sokol = @import("sokol");
const core = @import("core");
const assets = @import("assets");
const utils = @import("utils.zig");
const material_pass = @import("material_pass.zig");

const sg = sokol.gfx;
const wallpaper = core.wallpaper;
const logger = core.logger.Shader;

pub const Effect = struct {
    passes: []material_pass.MaterialPass,
    allocator: std.mem.Allocator,

    pub fn load(allocator: std.mem.Allocator, path: []const u8, instance_json: std.json.Value) !Effect {
        const full_path = try std.fs.path.join(allocator, &.{ assets.loader.asset_root, path });
        defer allocator.free(full_path);

        const content = try std.fs.cwd().readFileAlloc(allocator, full_path, 1024 * 1024);
        defer allocator.free(content);

        const parsed = try std.json.parseFromSlice(std.json.Value, allocator, content, .{});
        defer parsed.deinit();

        const passes_json = parsed.value.object.get("passes") orelse return error.NoPasses;
        if (passes_json != .array) return error.InvalidPasses;

        var passes = try allocator.alloc(material_pass.MaterialPass, passes_json.array.items.len);
        for (passes_json.array.items, 0..) |p_json, i| {
            var pass = material_pass.MaterialPass.init(allocator);

            if (p_json.object.get("material")) |mat_path| {
                try pass.loadFromMaterial(mat_path.string);
            }

            if (instance_json.object.get("passes")) |inst_passes| {
                if (inst_passes == .array and inst_passes.array.items.len > i) {
                    const inst_p = inst_passes.array.items[i];
                    if (inst_p.object.get("constantshadervalues")) |v| try utils.mergeJsonIntoMap(allocator, &pass.constant_values, v);
                    if (inst_p.object.get("combos")) |v| try utils.mergeCombosIntoMap(allocator, &pass.combos, v);
                    if (inst_p.object.get("textures")) |v| try utils.mergeTexturesIntoArray(&pass.textures, v, allocator);
                }
            }

            try pass.compile();
            pass.updateUniforms();
            passes[i] = pass;
        }

        return .{
            .passes = passes,
            .allocator = allocator,
        };
    }

    pub fn deinit(self: *Effect) void {
        for (self.passes) |*p| p.deinit();
        self.allocator.free(self.passes);
    }
};
