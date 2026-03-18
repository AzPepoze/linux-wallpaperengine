const std = @import("std");
const core = @import("core");
const wallpaper = core.wallpaper;

pub fn parseColor(hex: []const u8) [3]f32 {
    if (hex.len == 0) return .{ 1, 1, 1 };
    var s = hex;
    if (std.mem.startsWith(u8, s, "#")) s = s[1..];

    if (s.len != 6 and s.len != 3) {
        var it = std.mem.splitScalar(u8, s, ' ');
        var res = [3]f32{ 1, 1, 1 };
        var i: usize = 0;
        while (it.next()) |part| {
            if (i >= 3) break;
            res[i] = std.fmt.parseFloat(f32, part) catch 1.0;
            i += 1;
        }
        return res;
    }

    const r = std.fmt.parseInt(u8, if (s.len == 6) s[0..2] else s[0..1], 16) catch 255;
    const g = std.fmt.parseInt(u8, if (s.len == 6) s[2..4] else s[1..2], 16) catch 255;
    const b = std.fmt.parseInt(u8, if (s.len == 6) s[4..6] else s[2..3], 16) catch 255;

    const div: f32 = if (s.len == 6) 255.0 else 15.0;
    return .{
        @as(f32, @floatFromInt(r)) / div,
        @as(f32, @floatFromInt(g)) / div,
        @as(f32, @floatFromInt(b)) / div,
    };
}

pub fn preprocessShader(allocator: std.mem.Allocator, source: []const u8, combos: std.StringHashMap(i32)) ![]const u8 {
    var sb: std.ArrayListUnmanaged(u8) = .{};
    try sb.appendSlice(allocator, "#version 120\n");

    var it = combos.iterator();
    while (it.next()) |entry| {
        var writer = sb.writer(allocator);
        try writer.print("#define {s} {d}\n", .{ entry.key_ptr.*, entry.value_ptr.* });
    }

    if (!combos.contains("BLENDMODE")) try sb.appendSlice(allocator, "#define BLENDMODE 0\n");

    try sb.appendSlice(allocator,
        \\#define frac fract
        \\#define lerp mix
        \\#define texSample2D texture2D
        \\#define atan2(y, x) atan(y, x)
        \\#define mul(a, b) ((b) * (a))
        \\#define g_ModelViewProjectionMatrix mvp
        \\#define g_Texture0 texture0
        \\#define a_Position vertexPosition
        \\#define a_TexCoord vertexTexCoord
        \\#define saturate(x) clamp(x, 0.0, 1.0)
        \\
    );

    try sb.appendSlice(allocator, source);
    return sb.toOwnedSlice(allocator);
}

pub fn mergeJsonIntoMap(allocator: std.mem.Allocator, map: *std.StringHashMap(std.json.Value), v: std.json.Value) !void {
    if (v != .object) return;
    var it = v.object.iterator();
    while (it.next()) |entry| {
        const key = try allocator.dupe(u8, entry.key_ptr.*);
        if (map.get(key)) |existing| {
            deinitJson(allocator, existing);
            try map.put(key, try dupeJson(allocator, entry.value_ptr.*));
        } else {
            try map.put(key, try dupeJson(allocator, entry.value_ptr.*));
        }
    }
}

pub fn mergeCombosIntoMap(allocator: std.mem.Allocator, map: *std.StringHashMap(i32), v: std.json.Value) !void {
    if (v != .object) return;
    var it = v.object.iterator();
    while (it.next()) |entry| {
        const val: i32 = switch (entry.value_ptr.*) {
            .integer => |i| @intCast(i),
            .float => |f| @intFromFloat(f),
            .object => |o| if (o.get("value")) |ov| switch (ov) {
                .integer => |i| @intCast(i),
                .float => |f| @intFromFloat(f),
                else => 0,
            } else 0,
            else => 0,
        };
        const key = try allocator.dupe(u8, entry.key_ptr.*);
        try map.put(key, val);
    }
}

pub fn mergeTexturesIntoArray(textures: *[8]?[]const u8, v: std.json.Value, allocator: std.mem.Allocator) !void {
    if (v != .array) return;
    for (v.array.items, 0..) |item, i| {
        if (i >= 8) break;
        if (item == .string) {
            if (textures[i]) |old| allocator.free(old);
            textures[i] = try allocator.dupe(u8, item.string);
        }
    }
}

pub fn dupeJson(allocator: std.mem.Allocator, v: std.json.Value) !std.json.Value {
    return switch (v) {
        .null => .null,
        .bool => |b| .{ .bool = b },
        .integer => |i| .{ .integer = i },
        .float => |f| .{ .float = f },
        .number_string => |s| .{ .number_string = try allocator.dupe(u8, s) },
        .string => |s| .{ .string = try allocator.dupe(u8, s) },
        .array => |a| {
            var new_a = try std.json.Array.initCapacity(allocator, a.items.len);
            for (a.items) |item| {
                new_a.append(try dupeJson(allocator, item)) catch unreachable;
            }
            return .{ .array = new_a };
        },
        .object => |o| {
            var new_o = std.json.ObjectMap.init(allocator);
            var it = o.iterator();
            while (it.next()) |entry| {
                new_o.put(try allocator.dupe(u8, entry.key_ptr.*), try dupeJson(allocator, entry.value_ptr.*)) catch unreachable;
            }
            return .{ .object = new_o };
        },
    };
}

pub fn deinitJson(allocator: std.mem.Allocator, v: std.json.Value) void {
    switch (v) {
        .string => |s| allocator.free(s),
        .array => |a| {
            for (a.items) |item| deinitJson(allocator, item);
            @constCast(&a).deinit();
        },
        .object => |o| {
            var it = o.iterator();
            while (it.next()) |entry| {
                allocator.free(entry.key_ptr.*);
                deinitJson(allocator, entry.value_ptr.*);
            }
            @constCast(&o).deinit();
        },
        else => {},
    }
}
