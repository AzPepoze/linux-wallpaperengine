const std = @import("std");
const sokol = @import("sokol");
const core = @import("core");
const engine = @import("engine");
const app = @import("app");
const assets = @import("assets");

const logger = core.logger;

const sokol_app = sokol.app;
const sokol_gfx = sokol.gfx;
const sokol_glue = sokol.glue;

// App state components
const AppContext = struct {
    allocator: std.mem.Allocator,
    runtime: app.lifecycle.RuntimeState,
    renderer: ?engine.engine_2d.Renderer = null,
    render_pipeline: ?engine.engine_2d.RenderPipeline = null,
    timer: core.time.FrameTimer,
    event_handler: app.events.EventHandler,
};

var app_context: ?*AppContext = null;
var scene_loaded = false;

fn printHelp() void {
    const help_text =
        \\Linux Wallpaper Engine (Zig Port)
        \\
        \\Usage:
        \\  linux_wallpaperengine [options] <wallpaper_folder>
        \\  linux_wallpaperengine --pkg <wallpaper_pkg_file>
        \\
        \\Options:
        \\  --pkg <file>     Load a Wallpaper Engine .pkg file
        \\  --debug          Enable debug logging
        \\  --help           Show this help message
        \\
        \\Examples:
        \\  linux_wallpaperengine /path/to/wallpaper/folder
        \\  linux_wallpaperengine --pkg wallpaper.pkg
        \\
    ;
    std.debug.print("{s}", .{help_text});
}

fn loadScene() !void {
    const ctx = app_context orelse return error.NoAppContext;
    const current_dir = std.fs.cwd();

    const allocator = ctx.allocator;
    var renderer = ctx.renderer orelse return error.RendererNotInitialized;

    // Reset asset root to default
    assets.loader.asset_root = "tmp";

    if (current_dir.access("tmp", .{})) |_| {
        current_dir.deleteTree("tmp") catch |e| {
            logger.Scene.warn("Failed to clean tmp directory: {any}", .{e});
        };
    } else |_| {
        logger.Scene.info("No tmp directory to clean", .{});
    }

    if (ctx.runtime.pkg_path) |pkg| {
        logger.Scene.info("Unpacking PKG: {s}", .{pkg});
        assets.unpack.extractPkg(allocator, pkg, "tmp") catch |e| {
            logger.Scene.err("Failed to extract PKG: {any}", .{e});
            return e;
        };
    } else if (ctx.runtime.folder_path) |folder| {
        const scene_pkg = try std.fs.path.join(allocator, &.{ folder, "scene.pkg" });
        defer allocator.free(scene_pkg);

        if (current_dir.access(scene_pkg, .{})) |_| {
            logger.Scene.info("Unpacking PKG from folder: {s}", .{scene_pkg});
            assets.unpack.extractPkg(allocator, scene_pkg, "tmp") catch |e| {
                logger.Scene.err("Failed to extract PKG from folder: {any}", .{e});
                return e;
            };
        } else |_| {
            logger.Scene.info("Direct loading from folder: {s}", .{folder});
            assets.loader.asset_root = folder;
        }
    } else {
        return error.NoWallpaperProvided;
    }

    // Read scene.json from disk
    const scene_json_path = try std.fs.path.join(allocator, &.{ assets.loader.asset_root, "scene.json" });
    defer allocator.free(scene_json_path);

    const content = current_dir.readFileAlloc(allocator, scene_json_path, 50 * 1024 * 1024) catch |e| {
        logger.Scene.err("Failed to read scene.json at {s}: {any}", .{ scene_json_path, e });
        return e;
    };
    defer allocator.free(content);

    // Parse scene.json and set it in the renderer
    ctx.runtime.scene_data = std.json.parseFromSlice(std.json.Value, allocator, content, .{}) catch |e| {
        logger.Scene.err("JSON parsing failed: {any}", .{e});
        return e;
    };

    if (ctx.runtime.scene_data) |parsed| {
        renderer.setScene(parsed.value) catch |e| {
            logger.Renderer.err("Failed to set scene: {any}", .{e});
            return e;
        };
    }

    scene_loaded = true;
    logger.Scene.info("Scene loaded and cached in RAM, tmp cleaned", .{});
}

export fn init() void {
    const ctx = app_context orelse return;

    sokol_gfx.setup(.{
        .environment = sokol_glue.environment(),
        .logger = .{ .func = sokol.log.func },
    });

    ctx.renderer = engine.engine_2d.Renderer.init(ctx.allocator, 1280, 720);
    ctx.render_pipeline = engine.engine_2d.RenderPipeline.init(ctx.allocator);
    ctx.runtime.lifecycle.markInitialized();

    logger.App.info("Sokol graphics initialized", .{});
    logger.Scene.info("Initial scene loading...", .{});
    loadScene() catch |e| {
        logger.Scene.warn("Scene loading failed (will render empty): {any}", .{e});
    };
}

export fn frame() void {
    const ctx = app_context orelse return;

    ctx.timer.tick();

    if (ctx.renderer) |*renderer| {
        const width = @as(f64, @floatFromInt(sokol_app.width()));
        const height = @as(f64, @floatFromInt(sokol_app.height()));

        renderer.updateViewport(width, height);

        // For now: render directly to backbuffer
        // Multi-pass support will be added as we refine the pipeline
        renderer.render();
    }
}

export fn cleanup() void {
    const ctx = app_context orelse return;

    if (ctx.render_pipeline) |*pipeline| {
        pipeline.deinit();
    }

    if (ctx.renderer) |*renderer| {
        renderer.deinit();
    }

    ctx.timer.deinit();
    ctx.runtime.deinit();
    sokol_gfx.shutdown();
    logger.App.info("Cleanup complete", .{});
}

pub fn main() void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const ctx = allocator.create(AppContext) catch |err| {
        logger.App.err("Failed to allocate app context: {any}", .{err});
        return;
    };
    defer allocator.destroy(ctx);
    app_context = ctx;

    ctx.allocator = allocator;
    ctx.runtime = app.lifecycle.RuntimeState.init(allocator);
    ctx.timer = core.time.FrameTimer.init(allocator);
    ctx.event_handler = app.events.EventHandler.init(&ctx.runtime);

    // Basic CLI parsing
    var args = std.process.args();
    _ = args.next();
    var has_wallpaper = false;

    while (args.next()) |arg| {
        if (std.mem.eql(u8, arg, "--pkg")) {
            if (args.next()) |pkg_path| {
                ctx.runtime.setWallpaperPath(true, pkg_path) catch |err| {
                    logger.App.err("Failed to set pkg path: {any}", .{err});
                };
                has_wallpaper = true;
            }
        } else if (std.mem.eql(u8, arg, "--debug")) {
            logger.current_level = .debug;
        } else if (std.mem.eql(u8, arg, "--help") or std.mem.eql(u8, arg, "-h")) {
            printHelp();
            return;
        } else if (!std.mem.startsWith(u8, arg, "-")) {
            ctx.runtime.setWallpaperPath(false, arg) catch |err| {
                logger.App.err("Failed to set folder path: {any}", .{err});
            };
            has_wallpaper = true;
        }
    }

    if (!has_wallpaper) {
        printHelp();
        return;
    }

    // Register event handler globally
    app.events.setGlobalEventHandler(&ctx.event_handler);

    logger.App.info("Starting Linux Wallpaper Engine...", .{});
    sokol_app.run(.{
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = cleanup,
        .event_cb = app.events.sokolEventCallback,
        .width = 1280,
        .height = 720,
        .window_title = "Linux Wallpaper Engine (Zig)",
        .logger = .{ .func = sokol.log.func },
    });
}
