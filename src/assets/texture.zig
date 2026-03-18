const std = @import("std");
const texzel = @import("texzel");
const lzig4 = @import("lzig4");
const core = @import("core");
const logger = core.logger;

pub const TextureFormat = enum(u32) {
    RGBA8 = 0,
    DXT1 = 4,
    DXT5 = 6,
    DXT1_ALT = 7,
    RG88 = 8,
    R8 = 9,
};

pub const DecodedTexture = struct {
    pixels: []u8,
    width: u32,
    height: u32,
};

pub fn decodeTex(allocator: std.mem.Allocator, path: []const u8) !DecodedTexture {
    var file = std.fs.cwd().openFile(path, .{}) catch |e| {
        logger.err("Failed to open texture file {s}: {any}", .{ path, e });
        return e;
    };
    defer file.close();

    var read_buf: [4096]u8 = undefined;
    var f_reader = file.readerStreaming(&read_buf);
    const reader = &f_reader.interface;

    const magic_buf = reader.takeArray(8) catch |e| {
        logger.err("Failed to read magic from {s}: {any}", .{ path, e });
        return e;
    };
    if (!std.mem.eql(u8, magic_buf, "TEXV0005")) {
        logger.err("Invalid magic in {s}: expected TEXV0005, got {s}", .{ path, magic_buf });
        return error.InvalidMagic;
    }

    try file.seekBy(1); // skip null
    _ = reader.takeArray(8) catch {}; // skip second magic (TEXB0003 etc)
    try file.seekBy(1); // skip null

    const format_val = std.mem.readInt(u32, try reader.takeArray(4), .little);
    const format = std.meta.intToEnum(TextureFormat, format_val) catch {
        logger.err("Unsupported texture format {d} in {s}", .{ format_val, path });
        return error.UnsupportedFormat;
    };
    try file.seekBy(4); // skip unknown (flags?)

    _ = reader.takeArray(4) catch {}; // skip mW
    _ = reader.takeArray(4) catch {}; // skip mH
    _ = reader.takeArray(4) catch {}; // skip iW
    _ = reader.takeArray(4) catch {}; // skip iH

    _ = try reader.takeArray(4); // unknown
    _ = try reader.takeArray(8); // TEXB0003 or similar
    try file.seekBy(1); // skip null
    _ = try reader.takeArray(4); // image count

    const mip_count = std.mem.readInt(u32, try reader.takeArray(4), .little);
    const mipW = std.mem.readInt(u32, try reader.takeArray(4), .little);
    const mipH = std.mem.readInt(u32, try reader.takeArray(4), .little);
    _ = mip_count;

    const is_lz4 = std.mem.readInt(u32, try reader.takeArray(4), .little) == 1;
    const decompressed_size = std.mem.readInt(u32, try reader.takeArray(4), .little);
    const data_size = std.mem.readInt(u32, try reader.takeArray(4), .little);

    const compressed_data = try allocator.alloc(u8, data_size);
    defer allocator.free(compressed_data);
    try reader.readSliceAll(compressed_data);

    var raw_data: []u8 = undefined;
    if (is_lz4) {
        raw_data = try allocator.alloc(u8, decompressed_size);
        var decompressor = lzig4.decompress.Decompressor{
            .frame_header = undefined,
        };
        var r: usize = 0;
        var w: usize = 0;
        try decompressor.decompress(compressed_data, &r, raw_data, &w);
    } else {
        raw_data = try allocator.dupe(u8, compressed_data);
    }
    defer allocator.free(raw_data);

    // Handle embedded PNG/JPG (common in some TEX files)
    if (raw_data.len > 8 and std.mem.eql(u8, raw_data[0..8], "\x89PNG\r\n\x1a\n")) {
        logger.warn("Embedded PNG detected in {s}, but not supported yet.", .{path});
        return error.EmbeddedPngNotSupportedYet;
    }

    const pixel_count = @as(usize, mipW) * mipH;
    var pixels = try allocator.alloc(u8, pixel_count * 4);

    const dims = texzel.core.Dimensions{ .width = mipW, .height = mipH };

    switch (format) {
        .RGBA8 => {
            logger.Texture.debug("Decoding RGBA8 texture: {s}", .{path});
            const copy_size = @min(pixels.len, raw_data.len);
            @memcpy(pixels[0..copy_size], raw_data[0..copy_size]);
        },
        .DXT1, .DXT1_ALT => {
            logger.Texture.debug("Decoding DXT1 texture: {s}", .{path});
            const decoded = try texzel.decode(allocator, .bc1, texzel.pixel_formats.RGBA8U, dims, raw_data, .{});
            defer decoded.deinit();
            @memcpy(pixels, decoded.asBuffer());
        },
        .DXT5 => {
            logger.Texture.debug("Decoding DXT5 texture: {s}", .{path});
            const decoded = try texzel.decode(allocator, .bc3, texzel.pixel_formats.RGBA8U, dims, raw_data, .{});
            defer decoded.deinit();
            @memcpy(pixels, decoded.asBuffer());
        },
        .R8 => {
            logger.Texture.debug("Decoding R8 texture: {s}", .{path});
            const min_len = @min(pixel_count, raw_data.len);
            for (0..min_len) |i| {
                const val = raw_data[i];
                pixels[i * 4 + 0] = val;
                pixels[i * 4 + 1] = val;
                pixels[i * 4 + 2] = val;
                pixels[i * 4 + 3] = 255;
            }
        },
        .RG88 => {
            logger.Texture.debug("Decoding RG88 texture: {s}", .{path});
            const min_len = @min(pixel_count, raw_data.len / 2);
            for (0..min_len) |i| {
                const lum = raw_data[i * 2 + 0];
                const alpha = raw_data[i * 2 + 1];
                pixels[i * 4 + 0] = lum;
                pixels[i * 4 + 1] = lum;
                pixels[i * 4 + 2] = lum;
                pixels[i * 4 + 3] = alpha;
            }
        },
    }

    return DecodedTexture{
        .pixels = pixels,
        .width = mipW,
        .height = mipH,
    };
}
