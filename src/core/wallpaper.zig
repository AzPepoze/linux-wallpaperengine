const std = @import("std");

pub const Vec2 = struct {
    x: f64 = 0,
    y: f64 = 0,
};

pub const Vec3 = struct {
    x: f64 = 0,
    y: f64 = 0,
    z: f64 = 0,
};

// We use std.json.Value for almost everything to be extremely robust
pub const Scene = struct {
    value: std.json.Value,
    allocator: std.mem.Allocator,

    pub fn parse(allocator: std.mem.Allocator, json: []const u8) !Scene {
        const parsed = try std.json.parseFromSlice(std.json.Value, allocator, json, .{});
        return .{
            .value = parsed.value,
            .allocator = allocator,
        };
    }

    pub fn getObjects(self: Scene) []std.json.Value {
        if (self.value.object.get("objects")) |objs| {
            if (objs == .array) return objs.array.items;
        }
        return &.{};
    }

    pub fn getClearColor(self: Scene) []const u8 {
        if (self.value.object.get("general")) |gen| {
            if (gen == .object) {
                if (gen.object.get("clearcolor")) |cc| {
                    if (cc == .string) return cc.string;
                }
            }
        }
        return "";
    }
};

pub fn getObjectString(obj: std.json.Value, key: []const u8) []const u8 {
    if (obj != .object) return "";
    if (obj.object.get(key)) |v| {
        if (v == .string) return v.string;
        if (v == .object) {
            if (v.object.get("value")) |ov| {
                if (ov == .string) return ov.string;
            }
        }
    }
    return "";
}

pub fn getObjectBool(obj: std.json.Value, key: []const u8) bool {
    if (obj != .object) return false;
    if (obj.object.get(key)) |v| {
        return switch (v) {
            .bool => |b| b,
            .object => |o| if (o.get("value")) |ov| switch (ov) {
                .bool => |b| b,
                else => true,
            } else true,
            else => true,
        };
    }
    return true; // Default to true if not found (visible=true)
}

pub fn jsonToFloat(v: std.json.Value) f64 {
    return switch (v) {
        .float => |f| f,
        .integer => |i| @as(f64, @floatFromInt(i)),
        .string => |s| std.fmt.parseFloat(f64, s) catch 0.0,
        .object => |o| if (o.get("value")) |ov| jsonToFloat(ov) else 0.0,
        else => 0.0,
    };
}

pub fn getObjectFloat(obj: std.json.Value, key: []const u8) f64 {
    if (obj != .object) return 1.0;
    if (obj.object.get(key)) |v| {
        return jsonToFloat(v);
    }
    return 1.0;
}

pub fn getObjectVec3(obj: std.json.Value, key: []const u8) Vec3 {
    if (obj != .object) return .{};
    if (obj.object.get(key)) |v| {
        const target = if (v == .object) v.object.get("value") orelse v else v;
        switch (target) {
            .string => |s| {
                var it = std.mem.tokenizeAny(u8, s, " ,");
                var res = Vec3{};
                if (it.next()) |x| {
                    res.x = std.fmt.parseFloat(f64, x) catch 0;
                    res.y = res.x;
                    res.z = res.x;
                }
                if (it.next()) |y| res.y = std.fmt.parseFloat(f64, y) catch 0;
                if (it.next()) |z| res.z = std.fmt.parseFloat(f64, z) catch 0;
                return res;
            },
            .float, .integer => {
                const f = jsonToFloat(target);
                return .{ .x = f, .y = f, .z = f };
            },
            .object => |o| {
                return .{
                    .x = if (o.get("x")) |x| jsonToFloat(x) else 0,
                    .y = if (o.get("y")) |y| jsonToFloat(y) else 0,
                    .z = if (o.get("z")) |z| jsonToFloat(z) else 0,
                };
            },
            else => {},
        }
    }
    return .{ .x = 1, .y = 1, .z = 1 }; // Default for scale etc
}

pub fn getObjectVec2(obj: std.json.Value, key: []const u8) Vec2 {
    if (obj != .object) return .{};
    if (obj.object.get(key)) |v| {
        const target = if (v == .object) v.object.get("value") orelse v else v;
        switch (target) {
            .string => |s| {
                var it = std.mem.tokenizeAny(u8, s, " ,");
                var res = Vec2{};
                if (it.next()) |x| {
                    res.x = std.fmt.parseFloat(f64, x) catch 0;
                    res.y = res.x;
                }
                if (it.next()) |y| res.y = std.fmt.parseFloat(f64, y) catch 0;
                return res;
            },
            .float, .integer => {
                const f = jsonToFloat(target);
                return .{ .x = f, .y = f };
            },
            .object => |o| {
                return .{
                    .x = if (o.get("x")) |x| jsonToFloat(x) else 0,
                    .y = if (o.get("y")) |y| jsonToFloat(y) else 0,
                };
            },
            else => {},
        }
    }
    return .{ .x = 1, .y = 1 };
}

// Hierarchical property lookup
pub fn getMergedValue(scene_values: ?std.json.Value, material_values: ?std.json.Value, key: []const u8) ?std.json.Value {
    if (scene_values) |sv| {
        if (sv == .object) {
            if (sv.object.get(key)) |v| return v;
        }
    }
    if (material_values) |mv| {
        if (mv == .object) {
            if (mv.object.get(key)) |v| return v;
        }
    }
    return null;
}

pub fn getMergedFloat(scene_values: ?std.json.Value, material_values: ?std.json.Value, key: []const u8, default: f64) f64 {
    const v = getMergedValue(scene_values, material_values, key) orelse return default;
    return jsonToFloat(v);
}

pub fn getMergedVec2(scene_values: ?std.json.Value, material_values: ?std.json.Value, key: []const u8, default: Vec2) Vec2 {
    const v = getMergedValue(scene_values, material_values, key) orelse return default;
    // We wrap it in a dummy object to reuse getObjectVec2
    var dummy = std.json.ObjectMap.init(std.heap.page_allocator);
    dummy.put(key, v) catch return default;
    const val = getObjectVec2(.{ .object = dummy }, key);
    dummy.deinit();
    return val;
}

pub fn getMergedVec3(scene_values: ?std.json.Value, material_values: ?std.json.Value, key: []const u8, default: Vec3) Vec3 {
    const v = getMergedValue(scene_values, material_values, key) orelse return default;
    var dummy = std.json.ObjectMap.init(std.heap.page_allocator);
    dummy.put(key, v) catch return default;
    const val = getObjectVec3(.{ .object = dummy }, key);
    dummy.deinit();
    return val;
}
