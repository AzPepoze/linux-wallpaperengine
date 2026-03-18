const std = @import("std");
const core = @import("core");
const logger = core.logger;

pub const FileEntry = struct {
    name: []const u8,
    offset: u32,
    size: u32,
};

fn readPkgString(allocator: std.mem.Allocator, file: std.fs.File) ![]const u8 {
    var size_buf: [4]u8 = undefined;
    _ = try file.readAll(&size_buf);
    const size = std.mem.readInt(u32, &size_buf, .little);

    const buf = try allocator.alloc(u8, size);
    _ = try file.readAll(buf);
    return buf;
}

pub fn extractPkg(allocator: std.mem.Allocator, pkg_path: []const u8, output_dir: []const u8) !void {
    var file = try std.fs.cwd().openFile(pkg_path, .{});
    defer file.close();

    const version = try readPkgString(allocator, file);
    defer allocator.free(version);
    logger.info("Package Version: {s}", .{version});

    var file_count_buf: [4]u8 = undefined;
    _ = try file.readAll(&file_count_buf);
    const file_count = std.mem.readInt(u32, &file_count_buf, .little);
    logger.info("File Count: {d}", .{file_count});

    var entries = try allocator.alloc(FileEntry, file_count);
    defer {
        for (entries) |entry| allocator.free(entry.name);
        allocator.free(entries);
    }

    for (0..file_count) |i| {
        const name = try readPkgString(allocator, file);

        var offset_buf: [4]u8 = undefined;
        _ = try file.readAll(&offset_buf);
        const offset = std.mem.readInt(u32, &offset_buf, .little);

        var size_buf: [4]u8 = undefined;
        _ = try file.readAll(&size_buf);
        const size = std.mem.readInt(u32, &size_buf, .little);

        entries[i] = .{ .name = name, .offset = offset, .size = size };
    }

    const data_start_pos = try file.getPos();
    logger.debug("Data start position: {d}", .{data_start_pos});

    try std.fs.cwd().makePath(output_dir);
    var out_dir = try std.fs.cwd().openDir(output_dir, .{});
    defer out_dir.close();

    var copy_buf: [64 * 1024]u8 = undefined;

    for (entries, 0..) |entry, i| {
        if (i % 20 == 0 or i == file_count - 1) {
            logger.debug("Extracting file {d}/{d}: {s} (Size: {d})", .{ i + 1, file_count, entry.name, entry.size });
        }

        const destPath = entry.name;
        if (std.fs.path.dirname(destPath)) |dir| {
            try out_dir.makePath(dir);
        }

        try file.seekTo(data_start_pos + entry.offset);

        var out_file = try out_dir.createFile(destPath, .{});
        defer out_file.close();

        var remaining = entry.size;
        while (remaining > 0) {
            const to_read = @min(remaining, copy_buf.len);
            const amt = try file.read(copy_buf[0..to_read]);
            if (amt == 0) return error.EndOfStream;
            try out_file.writeAll(copy_buf[0..amt]);
            remaining -= @as(u32, @intCast(amt));
        }
    }

    logger.info("Extraction completed successfully", .{});
}
