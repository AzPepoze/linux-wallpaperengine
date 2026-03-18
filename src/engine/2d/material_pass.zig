const std = @import("std");
const sokol = @import("sokol");
const core = @import("core");
const assets = @import("assets");
const utils = @import("utils.zig");

const sg = sokol.gfx;
const wallpaper = core.wallpaper;
const common = @import("types.zig");

pub const MaterialPass = struct {
    shader_name: []const u8,
    blending: []const u8,

    constant_values: std.StringHashMap(std.json.Value),
    combos: std.StringHashMap(i32),
    textures: [8]?[]const u8,
    resolved_textures: [8]sg.Image,

    shader: ?sg.Shader,
    pipeline: ?sg.Pipeline,

    uniforms: std.ArrayListUnmanaged(common.UniformInfo),
    uniform_data: []u8,

    allocator: std.mem.Allocator,

    pub fn init(allocator: std.mem.Allocator) MaterialPass {
        return .{
            .shader_name = "",
            .blending = "normal",
            .constant_values = std.StringHashMap(std.json.Value).init(allocator),
            .combos = std.StringHashMap(i32).init(allocator),
            .textures = .{null} ** 8,
            .resolved_textures = undefined,
            .shader = null,
            .pipeline = null,
            .uniforms = .{},
            .uniform_data = &.{},
            .allocator = allocator,
        };
    }

    pub fn deinit(self: *MaterialPass) void {
        self.allocator.free(self.shader_name);
        self.allocator.free(self.blending);

        var it = self.constant_values.iterator();
        while (it.next()) |entry| {
            self.allocator.free(entry.key_ptr.*);
            utils.deinitJson(self.allocator, entry.value_ptr.*);
        }
        self.constant_values.deinit();

        var combo_it = self.combos.iterator();
        while (combo_it.next()) |entry| {
            self.allocator.free(entry.key_ptr.*);
        }
        self.combos.deinit();

        for (self.textures, 0..) |t, i| {
            if (t) |name| {
                self.allocator.free(name);
                self.textures[i] = null;
            }
        }

        for (self.uniforms.items) |u| {
            self.allocator.free(u.name);
        }
        self.uniforms.deinit(self.allocator);
        if (self.uniform_data.len > 0) self.allocator.free(self.uniform_data);

        if (self.shader) |s| if (s.id != 0) sg.destroyShader(s);
        if (self.pipeline) |pip| if (pip.id != 0) sg.destroyPipeline(pip);
    }

    pub fn loadFromMaterial(self: *MaterialPass, path: []const u8) !void {
        const full_path = try std.fs.path.join(self.allocator, &.{ assets.loader.asset_root, path });
        defer self.allocator.free(full_path);

        const content = try std.fs.cwd().readFileAlloc(self.allocator, full_path, 1024 * 1024);
        defer self.allocator.free(content);

        const parsed = try std.json.parseFromSlice(std.json.Value, self.allocator, content, .{});
        defer parsed.deinit();

        const pass_json = if (parsed.value.object.get("passes")) |p| (if (p == .array and p.array.items.len > 0) p.array.items[0] else p) else return error.NoPass;

        self.shader_name = try self.allocator.dupe(u8, if (pass_json.object.get("shader")) |s| s.string else "");
        self.blending = try self.allocator.dupe(u8, if (pass_json.object.get("blending")) |b| b.string else "normal");

        if (pass_json.object.get("constantshadervalues")) |v| try utils.mergeJsonIntoMap(self.allocator, &self.constant_values, v);
        if (pass_json.object.get("combos")) |v| try utils.mergeCombosIntoMap(self.allocator, &self.combos, v);
        if (pass_json.object.get("textures")) |v| try utils.mergeTexturesIntoArray(&self.textures, v, self.allocator);
    }

    pub fn compile(self: *MaterialPass) !void {
        if (self.shader_name.len == 0) return;

        const v_filename = try std.mem.concat(self.allocator, u8, &.{ self.shader_name, ".vert" });
        defer self.allocator.free(v_filename);
        const f_filename = try std.mem.concat(self.allocator, u8, &.{ self.shader_name, ".frag" });
        defer self.allocator.free(f_filename);

        const v_path = try std.fs.path.join(self.allocator, &.{ assets.loader.asset_root, "shaders", v_filename });
        const f_path = try std.fs.path.join(self.allocator, &.{ assets.loader.asset_root, "shaders", f_filename });
        defer self.allocator.free(v_path);
        defer self.allocator.free(f_path);

        const v_source = try std.fs.cwd().readFileAlloc(self.allocator, v_path, 1024 * 1024);
        const f_source = try std.fs.cwd().readFileAlloc(self.allocator, f_path, 1024 * 1024);
        defer self.allocator.free(v_source);
        defer self.allocator.free(f_source);

        const v_processed = try utils.preprocessShader(self.allocator, v_source, self.combos);
        const f_processed = try utils.preprocessShader(self.allocator, f_source, self.combos);
        defer self.allocator.free(v_processed);
        defer self.allocator.free(f_processed);

        try self.parseUniforms(f_processed);

        var d = sg.ShaderDesc{};
        d.vertex_func.source = v_processed.ptr;
        d.fragment_func.source = f_processed.ptr;

        if (self.uniform_data.len > 0) {
            var block = &d.uniform_blocks[0];
            block.stage = .FRAGMENT;
            block.size = @intCast(self.uniform_data.len);

            var u_idx: usize = 0;
            for (self.uniforms.items) |u| {
                if (u.type == .sampler2D) continue;
                if (u_idx >= 16) break;

                block.glsl_uniforms[u_idx] = .{
                    .type = switch (u.type) {
                        .float => .FLOAT,
                        .vec2 => .FLOAT2,
                        .vec3 => .FLOAT3,
                        .vec4 => .FLOAT4,
                        .mat4 => .MAT4,
                        else => .FLOAT,
                    },
                    .glsl_name = u.name.ptr,
                };
                u_idx += 1;
            }
        }

        for (0..8) |i| {
            d.views[i].texture.stage = .FRAGMENT;
            d.views[i].texture.image_type = ._2D;
            d.samplers[i].stage = .FRAGMENT;

            const name = try std.fmt.allocPrint(self.allocator, "g_Texture{d}", .{i});
            defer self.allocator.free(name);

            d.texture_sampler_pairs[i] = .{
                .stage = .FRAGMENT,
                .view_slot = @intCast(i),
                .sampler_slot = @intCast(i),
                .glsl_name = name.ptr,
            };
        }

        self.shader = sg.makeShader(d);
        if (self.shader.?.id == 0) return error.ShaderCompilationFailed;

        var pip_desc = sg.PipelineDesc{
            .shader = self.shader.?,
            .index_type = .UINT16,
        };
        pip_desc.layout.attrs[0] = .{ .format = .FLOAT2 };
        pip_desc.layout.attrs[1] = .{ .format = .FLOAT2 };

        if (std.mem.eql(u8, self.blending, "additive")) {
            pip_desc.colors[0].blend = .{
                .enabled = true,
                .src_factor_rgb = .SRC_ALPHA,
                .dst_factor_rgb = .ONE,
            };
        } else {
            pip_desc.colors[0].blend = .{
                .enabled = true,
                .src_factor_rgb = .SRC_ALPHA,
                .dst_factor_rgb = .ONE_MINUS_SRC_ALPHA,
            };
        }

        self.pipeline = sg.makePipeline(pip_desc);
    }

    pub fn parseUniforms(self: *MaterialPass, source: []const u8) !void {
        var it = std.mem.tokenizeAny(u8, source, "\n;");
        var offset: usize = 0;

        while (it.next()) |line| {
            const trimmed = std.mem.trimLeft(u8, line, " \t");
            if (std.mem.startsWith(u8, trimmed, "uniform")) {
                var words = std.mem.tokenizeAny(u8, trimmed, " \t[]");
                _ = words.next();
                const type_str = words.next() orelse continue;
                const name_str = words.next() orelse continue;

                if (std.mem.startsWith(u8, name_str, "mvp") or std.mem.startsWith(u8, name_str, "g_ModelViewProjectionMatrix")) continue;

                const utype: common.UniformType = if (std.mem.eql(u8, type_str, "float")) .float else if (std.mem.eql(u8, type_str, "vec2")) .vec2 else if (std.mem.eql(u8, type_str, "vec3")) .vec3 else if (std.mem.eql(u8, type_str, "vec4")) .vec4 else if (std.mem.eql(u8, type_str, "mat4")) .mat4 else if (std.mem.eql(u8, type_str, "sampler2D")) .sampler2D else continue;

                const size = switch (utype) {
                    .float => @as(usize, 4),
                    .vec2 => 8,
                    .vec3 => 12,
                    .vec4 => 16,
                    .mat4 => 64,
                    .sampler2D => 0,
                };

                if (size > 0) {
                    try self.uniforms.append(self.allocator, .{
                        .name = try self.allocator.dupe(u8, name_str),
                        .type = utype,
                        .offset = offset,
                        .size = size,
                    });
                    offset += size;
                }
            }
        }

        if (offset > 0) {
            offset = (offset + 15) & ~@as(usize, 15);
            self.uniform_data = try self.allocator.alloc(u8, offset);
            @memset(self.uniform_data, 0);
        }
    }

    pub fn updateUniforms(self: *MaterialPass) void {
        if (self.uniform_data.len == 0) return;

        for (self.uniforms.items) |u| {
            var key: []const u8 = u.name;
            if (std.mem.startsWith(u8, key, "g_")) key = key[2..];

            const val = self.constant_values.get(key) orelse continue;

            switch (u.type) {
                .float => {
                    const f = @as(f32, @floatCast(wallpaper.jsonToFloat(val)));
                    std.mem.copyForwards(u8, self.uniform_data[u.offset .. u.offset + 4], std.mem.asBytes(&f));
                },
                .vec2 => {
                    const v_val = if (val == .object) (val.object.get("value") orelse val) else val;
                    const v = switch (v_val) {
                        .string => |s| blk: {
                            var it_inner = std.mem.tokenizeAny(u8, s, " ,");
                            var res = wallpaper.Vec2{};
                            if (it_inner.next()) |x| {
                                res.x = std.fmt.parseFloat(f64, x) catch 0;
                                res.y = res.x;
                            }
                            if (it_inner.next()) |y| res.y = std.fmt.parseFloat(f64, y) catch 0;
                            break :blk res;
                        },
                        .float, .integer => blk: {
                            const f = wallpaper.jsonToFloat(v_val);
                            break :blk wallpaper.Vec2{ .x = f, .y = f };
                        },
                        .object => |o| blk: {
                            const x_val = o.get("x") orelse o.get("0") orelse std.json.Value{ .float = 0 };
                            const y_val = o.get("y") orelse o.get("1") orelse std.json.Value{ .float = 0 };
                            break :blk wallpaper.Vec2{ .x = wallpaper.jsonToFloat(x_val), .y = wallpaper.jsonToFloat(y_val) };
                        },
                        else => wallpaper.Vec2{},
                    };
                    const vf = [2]f32{ @floatCast(v.x), @floatCast(v.y) };
                    std.mem.copyForwards(u8, self.uniform_data[u.offset .. u.offset + 8], std.mem.asBytes(&vf));
                },
                .vec3 => {
                    const v_val = if (val == .object) (val.object.get("value") orelse val) else val;
                    const v = switch (v_val) {
                        .string => |s| blk: {
                            var it_inner = std.mem.tokenizeAny(u8, s, " ,");
                            var res = wallpaper.Vec3{ .x = 0, .y = 0, .z = 0 };
                            if (it_inner.next()) |x| {
                                res.x = std.fmt.parseFloat(f64, x) catch 0;
                                res.y = res.x;
                                res.z = res.x;
                            }
                            if (it_inner.next()) |y| res.y = std.fmt.parseFloat(f64, y) catch 0;
                            if (it_inner.next()) |z| res.z = std.fmt.parseFloat(f64, z) catch 0;
                            break :blk res;
                        },
                        .float, .integer => blk: {
                            const f = wallpaper.jsonToFloat(v_val);
                            break :blk wallpaper.Vec3{ .x = f, .y = f, .z = f };
                        },
                        .object => |o| blk: {
                            const x_val = o.get("x") orelse o.get("0") orelse std.json.Value{ .float = 0 };
                            const y_val = o.get("y") orelse o.get("1") orelse std.json.Value{ .float = 0 };
                            const z_val = o.get("z") orelse o.get("2") orelse std.json.Value{ .float = 0 };
                            break :blk wallpaper.Vec3{ .x = wallpaper.jsonToFloat(x_val), .y = wallpaper.jsonToFloat(y_val), .z = wallpaper.jsonToFloat(z_val) };
                        },
                        else => wallpaper.Vec3{},
                    };
                    const vf = [3]f32{ @floatCast(v.x), @floatCast(v.y), @floatCast(v.z) };
                    std.mem.copyForwards(u8, self.uniform_data[u.offset .. u.offset + 12], std.mem.asBytes(&vf));
                },
                .vec4 => {
                    const v_val = if (val == .object) (val.object.get("value") orelse val) else val;
                    var v4: [4]f32 = .{ 0, 0, 0, 1 };
                    switch (v_val) {
                        .string => |s| {
                            var it_inner = std.mem.tokenizeAny(u8, s, " ,");
                            var idx: usize = 0;
                            while (idx < 4) : (idx += 1) {
                                if (it_inner.next()) |component| {
                                    v4[idx] = @floatCast(std.fmt.parseFloat(f64, component) catch 0);
                                } else break;
                            }
                        },
                        .float, .integer => {
                            const f = wallpaper.jsonToFloat(v_val);
                            v4 = .{ @floatCast(f), @floatCast(f), @floatCast(f), 1 };
                        },
                        .object => |o| {
                            const x_val = o.get("x") orelse o.get("r") orelse o.get("0") orelse std.json.Value{ .float = 0 };
                            const y_val = o.get("y") orelse o.get("g") orelse o.get("1") orelse std.json.Value{ .float = 0 };
                            const z_val = o.get("z") orelse o.get("b") orelse o.get("2") orelse std.json.Value{ .float = 0 };
                            const w_val = o.get("w") orelse o.get("a") orelse o.get("3") orelse std.json.Value{ .float = 1 };
                            v4 = .{ @floatCast(wallpaper.jsonToFloat(x_val)), @floatCast(wallpaper.jsonToFloat(y_val)), @floatCast(wallpaper.jsonToFloat(z_val)), @floatCast(wallpaper.jsonToFloat(w_val)) };
                        },
                        else => {},
                    }
                    std.mem.copyForwards(u8, self.uniform_data[u.offset .. u.offset + 16], std.mem.asBytes(&v4));
                },
                else => {},
            }
        }
    }
};
