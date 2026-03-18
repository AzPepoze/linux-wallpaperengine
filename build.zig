const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // Options for Sokol backend
    const opt_use_vulkan = b.option(bool, "vulkan", "Force Vulkan backend (default: false)") orelse false;
    const opt_use_gles3 = b.option(bool, "gles3", "Force GLES3 backend (default: false)") orelse false;

    const dep_sokol = b.dependency("sokol", .{
        .target = target,
        .optimize = optimize,
        .vulkan = opt_use_vulkan,
        .gles3 = opt_use_gles3,
    });
    const dep_texzel = b.dependency("texzel", .{
        .target = target,
        .optimize = optimize,
    });
    const dep_lzig4 = b.dependency("LZig4", .{
        .target = target,
        .optimize = optimize,
    });

    const mod_core = b.addModule("core", .{
        .root_source_file = b.path("src/core/root.zig"),
        .imports = &.{
            .{ .name = "sokol", .module = dep_sokol.module("sokol") },
        },
    });

    const mod_assets = b.addModule("assets", .{
        .root_source_file = b.path("src/assets/root.zig"),
        .imports = &.{
            .{ .name = "core", .module = mod_core },
            .{ .name = "sokol", .module = dep_sokol.module("sokol") },
            .{ .name = "texzel", .module = dep_texzel.module("texzel") },
            .{ .name = "lzig4", .module = dep_lzig4.module("lzig4") },
        },
    });

    const mod_engine = b.addModule("engine", .{
        .root_source_file = b.path("src/engine/root.zig"),
        .imports = &.{
            .{ .name = "core", .module = mod_core },
            .{ .name = "assets", .module = mod_assets },
            .{ .name = "sokol", .module = dep_sokol.module("sokol") },
        },
    });

    const mod_app = b.addModule("app", .{
        .root_source_file = b.path("src/app/root.zig"),
        .imports = &.{
            .{ .name = "core", .module = mod_core },
            .{ .name = "engine", .module = mod_engine },
            .{ .name = "assets", .module = mod_assets },
            .{ .name = "sokol", .module = dep_sokol.module("sokol") },
        },
    });

    const exe = b.addExecutable(.{
        .name = "linux_wallpaperengine",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{
                .{ .name = "app", .module = mod_app },
                .{ .name = "core", .module = mod_core },
                .{ .name = "engine", .module = mod_engine },
                .{ .name = "assets", .module = mod_assets },
                .{ .name = "sokol", .module = dep_sokol.module("sokol") },
                .{ .name = "texzel", .module = dep_texzel.module("texzel") },
                .{ .name = "lzig4", .module = dep_lzig4.module("lzig4") },
            },
        }),
    });

    exe.use_llvm = true;
    exe.use_lld = true;

    exe.linkLibC();
    exe.linkSystemLibrary("x11");
    exe.linkSystemLibrary("xcb");

    if (opt_use_vulkan) {
        exe.linkSystemLibrary("vulkan");
    } else {
        exe.linkSystemLibrary("GL");
    }

    b.installArtifact(exe);

    const run_step = b.step("run", "Run the app");
    const run_cmd = b.addRunArtifact(exe);
    run_step.dependOn(&run_cmd.step);
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }
}
