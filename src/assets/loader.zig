const std = @import("std");
const core = @import("core");

const logger = core.logger;

pub var wallpaper_engine_assets: ?[]const u8 = null;
pub var asset_root: []const u8 = "tmp";

pub fn resolveAssetPath(allocator: std.mem.Allocator, rel_path: []const u8) ![]const u8 {
    // 1. Try provided asset root (folder or extracted PKG)
    const root_path = try std.fs.path.join(allocator, &.{ asset_root, rel_path });
    if (std.fs.cwd().access(root_path, .{})) |_| {
        return root_path;
    } else |_| {
        allocator.free(root_path);
    }

    // 2. Try local assets
    const local_path = try std.fs.path.join(allocator, &.{ "assets", rel_path });
    if (std.fs.cwd().access(local_path, .{})) |_| {
        return local_path;
    } else |_| {
        allocator.free(local_path);
    }

    // 3. Try Steam assets
    if (wallpaper_engine_assets) |steam_root| {
        const steam_path = try std.fs.path.join(allocator, &.{ steam_root, rel_path });
        if (std.fs.cwd().access(steam_path, .{})) |_| {
            return steam_path;
        } else |_| {
            allocator.free(steam_path);
        }
    }

    return allocator.dupe(u8, rel_path);
}

pub fn findTextureFile(allocator: std.mem.Allocator, name: []const u8) !?[]const u8 {
    logger.Texture.debug("Finding texture file for {s}", .{name});
    if (name.len == 0) return null;

    var clean_name = name;
    if (std.mem.startsWith(u8, clean_name, "materials/")) {
        clean_name = clean_name["materials/".len..];
    }
    if (std.mem.endsWith(u8, clean_name, ".tex")) {
        clean_name = clean_name[0 .. clean_name.len - ".tex".len];
    }

    const extensions = [_][]const u8{
        ".tex",
        ".tex-json",
        ".png",
        ".jpg",
        ".jpeg",
    };

    // Check in asset_root first
    for (extensions) |ext| {
        const p = try std.fs.path.join(allocator, &.{ asset_root, clean_name });
        const full_p = try std.mem.concat(allocator, u8, &.{ p, ext });
        defer allocator.free(p);
        if (std.fs.cwd().access(full_p, .{})) |_| {
            logger.Texture.debug("Found texture at {s}", .{full_p});
            return full_p;
        } else |_| {
            allocator.free(full_p);
        }

        // Also check with materials/ prefix inside root
        const p_mat = try std.fs.path.join(allocator, &.{ asset_root, "materials", clean_name });
        const full_p_mat = try std.mem.concat(allocator, u8, &.{ p_mat, ext });
        defer allocator.free(p_mat);
        if (std.fs.cwd().access(full_p_mat, .{})) |_| {
            logger.Texture.debug("Found texture at {s}", .{full_p_mat});
            return full_p_mat;
        } else |_| {
            allocator.free(full_p_mat);
        }
    }

    // Fallback to legacy search dirs
    const search_dirs = [_][]const u8{
        "converted",
        "assets/materials",
        "assets",
    };

    for (search_dirs) |dir| {
        for (extensions) |ext| {
            const p = try std.fs.path.join(allocator, &.{ dir, clean_name });
            const full_p = try std.mem.concat(allocator, u8, &.{ p, ext });
            defer allocator.free(p);
            if (std.fs.cwd().access(full_p, .{})) |_| {
                logger.Texture.debug("Found texture at {s}", .{full_p});
                return full_p;
            } else |_| {
                allocator.free(full_p);
            }
        }
    }

    logger.Texture.warn("Texture file not found for {s}", .{name});
    return null;
}

pub fn loadModelConfig(allocator: std.mem.Allocator, path: []const u8) !core.wallpaper.ModelJSON {
    const full_path = try resolveAssetPath(allocator, path);
    defer allocator.free(full_path);

    const data = try std.fs.cwd().readFileAlloc(allocator, full_path, 10 * 1024 * 1024);
    defer allocator.free(data);

    const parsed = try std.json.parseFromSlice(std.json.Value, allocator, data, .{});
    defer parsed.deinit();

    var config = core.wallpaper.ModelJSON{};

    if (parsed.value == .object) {
        if (parsed.value.object.get("material")) |m| {
            if (m == .string) config.material = try allocator.dupe(u8, m.string);
        }
        if (parsed.value.object.get("puppet")) |p| {
            if (p == .string) config.puppet = try allocator.dupe(u8, p.string);
        }

        var textures_list: std.ArrayListUnmanaged([]const u8) = .{};
        errdefer {
            for (textures_list.items) |t| allocator.free(t);
            textures_list.deinit(allocator);
        }

        if (parsed.value.object.get("textures")) |tex| {
            switch (tex) {
                .array => |arr| {
                    for (arr.items) |item| {
                        if (item == .string) try textures_list.append(allocator, try allocator.dupe(u8, item.string));
                    }
                },
                .object => |obj| {
                    var it = obj.iterator();
                    while (it.next()) |entry| {
                        if (entry.value_ptr.* == .string) try textures_list.append(allocator, try allocator.dupe(u8, entry.value_ptr.*.string));
                    }
                },
                else => {},
            }
        }
        config.textures = try textures_list.toOwnedSlice(allocator);
    }

    return config;
}

pub fn freeModelConfig(allocator: std.mem.Allocator, model: core.wallpaper.ModelJSON) void {
    if (model.material.len > 0) allocator.free(model.material);
    if (model.puppet.len > 0) allocator.free(model.puppet);
    for (model.textures) |t| allocator.free(t);
    allocator.free(model.textures);
}
