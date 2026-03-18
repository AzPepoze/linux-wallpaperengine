const std = @import("std");
const sokol = @import("sokol");
const core = @import("core");
const assets = @import("assets");
const utils = @import("utils.zig");
const common = @import("types.zig");
const renderer_2d = @import("renderer_2d.zig");

const logger = core.logger;
const sokol_gfx = sokol.gfx;
const wallpaper = core.wallpaper;

pub const Renderer = struct {
    allocator: std.mem.Allocator,
    renderer_2d: renderer_2d.Renderer2D,
    pass_action: sokol_gfx.PassAction = .{},
    scaling_mode: common.ScalingMode = .fit,
    texture_cache: std.StringHashMapUnmanaged(sokol_gfx.Image) = .{},
    white_texture: sokol_gfx.Image = .{},
    scene_value: ?std.json.Value = null,

    pub fn init(allocator: std.mem.Allocator, width: f64, height: f64) Renderer {
        var pass_action = sokol_gfx.PassAction{};
        pass_action.colors[0] = .{
            .load_action = .CLEAR,
            .clear_value = .{ .r = 0.0, .g = 0.0, .b = 0.0, .a = 1.0 },
        };

        const white_pixel = [_]u32{ 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
        var img_data = sokol_gfx.ImageData{};
        img_data.mip_levels[0] = sokol_gfx.asRange(&white_pixel);
        const white_tex = sokol_gfx.makeImage(.{
            .width = 2,
            .height = 2,
            .pixel_format = .RGBA8,
            .data = img_data,
        });

        return .{
            .allocator = allocator,
            .renderer_2d = renderer_2d.Renderer2D.init(allocator, width, height),
            .pass_action = pass_action,
            .white_texture = white_tex,
        };
    }

    pub fn deinit(self: *Renderer) void {
        sokol_gfx.destroyImage(self.white_texture);
        self.clearResources();
        self.renderer_2d.deinit();
    }

    pub fn clearResources(self: *Renderer) void {
        var it = self.texture_cache.iterator();
        while (it.next()) |entry| {
            sokol_gfx.destroyImage(entry.value_ptr.*);
            self.allocator.free(entry.key_ptr.*);
        }
        self.texture_cache.clearAndFree(self.allocator);
        self.renderer_2d.render_objects.clearRetainingCapacity();
    }

    pub fn setScene(self: *Renderer, scene: std.json.Value) !void {
        self.scene_value = scene;
        self.clearResources();

        if (scene == .object) {
            if (scene.object.get("general")) |gen| {
                if (gen == .object) {
                    if (gen.object.get("clearcolor")) |cc| {
                        if (cc == .string) {
                            const c = utils.parseColor(cc.string);
                            self.pass_action.colors[0].clear_value = .{ .r = c[0], .g = c[1], .b = c[2], .a = 1.0 };
                        }
                    }
                }
            }
        }

        const objects = if (scene == .object) (if (scene.object.get("objects")) |objs| (if (objs == .array) objs.array.items else &.{}) else &.{}) else &.{};
        for (objects) |obj| {
            logger.Renderer.debug("Processing scene object: {s}", .{wallpaper.getObjectString(obj, "name")});
            var img: ?sokol_gfx.Image = null;
            const img_name = wallpaper.getObjectString(obj, "image");
            if (img_name.len > 0) {
                var final_img_name: ?[]const u8 = img_name;
                var model_textures_value: ?core.wallpaper.ModelJSON = null;

                if (std.mem.endsWith(u8, img_name, ".json")) {
                    final_img_name = null; // Don't try to load .json as texture by default
                    if (assets.loader.loadModelConfig(self.allocator, img_name)) |cfg| {
                        model_textures_value = cfg;
                        if (cfg.textures.len > 0) {
                            final_img_name = cfg.textures[0];
                        }
                    } else |e| {
                        // Built-in models might be missing, log but don't fail hard
                        if (e != error.FileNotFound) {
                            logger.Renderer.err("Failed to load model config {s}: {any}", .{ img_name, e });
                        } else {
                            logger.Renderer.debug("Model file not found (likely built-in): {s}", .{img_name});
                        }
                    }
                }

                if (final_img_name) |fname| {
                    const tex_path = try assets.loader.findTextureFile(self.allocator, fname);
                    if (tex_path) |path| {
                        img = try self.loadOrGetTexture(path);
                    }
                }

                if (model_textures_value) |cfg| {
                    assets.loader.freeModelConfig(self.allocator, cfg);
                }
            }

            try self.renderer_2d.render_objects.append(self.allocator, .{
                .object_json = obj,
                .image = img,
                .effects = &.{},
            });
        }
    }

    fn loadOrGetTexture(self: *Renderer, path: []const u8) !sokol_gfx.Image {
        if (self.texture_cache.get(path)) |cached_img| {
            self.allocator.free(path);
            return cached_img;
        }

        const decoded = assets.texture.decodeTex(self.allocator, path) catch |e| {
            self.allocator.free(path);
            return e;
        };

        defer self.allocator.free(decoded.pixels);

        var img_data = sokol_gfx.ImageData{};
        img_data.mip_levels[0] = sokol_gfx.asRange(decoded.pixels);

        const new_img = sokol_gfx.makeImage(.{
            .width = @as(i32, @intCast(decoded.width)),
            .height = @as(i32, @intCast(decoded.height)),
            .pixel_format = .RGBA8,
            .data = img_data,
        });

        try self.texture_cache.put(self.allocator, path, new_img);
        return new_img;
    }

    pub fn updateViewport(self: *Renderer, width: f64, height: f64) void {
        self.renderer_2d.updateViewport(width, height);
    }

    pub fn render(self: *Renderer) void {
        sokol_gfx.beginPass(.{ .action = self.pass_action, .swapchain = sokol.glue.swapchain() });

        for (self.renderer_2d.render_objects.items) |ro| {
            const obj = ro.object_json;
            if (!wallpaper.getObjectBool(obj, "visible")) continue;

            const origin = wallpaper.getObjectVec3(obj, "origin");
            const pos_x = @as(f32, @floatCast(origin.x)) + @as(f32, @floatCast(ro.offset.x));
            const pos_y = @as(f32, @floatCast(origin.y)) + @as(f32, @floatCast(ro.offset.y));

            const size = wallpaper.getObjectVec2(obj, "size");
            var size_x = @as(f32, @floatCast(size.x));
            var size_y = @as(f32, @floatCast(size.y));

            if (ro.image) |img| {
                if (size_x == 0 or size_y == 0) {
                    const info = sokol_gfx.queryImageDesc(img);
                    size_x = @as(f32, @floatFromInt(info.width));
                    size_y = @as(f32, @floatFromInt(info.height));
                }
            } else {
                if (size_x == 0) size_x = 256;
                if (size_y == 0) size_y = 256;
            }

            const scale = wallpaper.getObjectVec3(obj, "scale");
            const scale_x = if (scale.x != 0) @as(f32, @floatCast(scale.x)) else 1.0;
            const scale_y = if (scale.y != 0) @as(f32, @floatCast(scale.y)) else 1.0;
            const angles = wallpaper.getObjectVec3(obj, "angles");
            const rotation = @as(f32, @floatCast(angles.z));

            var tint = [4]f32{ 1, 1, 1, 1 };
            const color_str = wallpaper.getObjectString(obj, "color");
            if (color_str.len > 0) {
                const c = utils.parseColor(color_str);
                tint[0] = c[0];
                tint[1] = c[1];
                tint[2] = c[2];
            }
            tint[3] = @as(f32, @floatCast(wallpaper.getObjectFloat(obj, "alpha")));

            if (ro.image) |img| {
                self.renderer_2d.drawSprite(img, pos_x, pos_y, size_x * scale_x, size_y * scale_y, rotation, tint, ro.effects);
            } else {
                tint[3] *= 0.5;
                self.renderer_2d.drawSprite(self.white_texture, pos_x, pos_y, size_x * scale_x, size_y * scale_y, rotation, tint, ro.effects);
            }
        }

        sokol_gfx.endPass();
        sokol_gfx.commit();
    }
};
