add_rules("mode.debug", "mode.release", "mode.asan", "mode.ubsan")
set_languages("cxx20")
set_policy("check.auto_ignore_flags", false)
add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

-- Track the upstream heads. Run `xmake require --upgrade` when you want to
-- refresh the cached dependency revisions.
add_requires("sokol master")
add_requires("linmath.h master")
add_requires("vulkan-headers")
add_requires("lz4")
add_requires("cjson")
add_requires("stb")
add_requires("miniaudio")
add_requires("imgui", {optional = true})
-- SceneScript (clock/date text) runs on an embedded QuickJS engine.
add_requires("quickjs")

-- Off by default: the core engine build has no Qt dependency. Enable with
-- `xmake f --web=y` to build the QtWebEngine helper used by web wallpapers.
option("web")
    set_default(false)
    set_showmenu(true)
    set_description("Enable Qt6 WebEngine web wallpaper rendering")
option_end()

if has_config("web") then
    add_requires("pkgconfig::Qt6WebEngineWidgets")
end

-- On by default when libwayland-client and wayland-scanner are installed. Draws the wallpaper
-- on a wlr-layer-shell surface when --screen-root/--layer is given and a Wayland session is running.
option("layer_shell")
    set_showmenu(true)
    set_description("Enable the Wayland wlr-layer-shell desktop wallpaper backend")
    on_check(function (option)
        import("lib.detect.find_tool")
        import("lib.detect.find_package")
        if find_tool("wayland-scanner") and find_package("pkgconfig::wayland-client") then
            option:enable(true)
        end
    end)
option_end()

-- Generates the client protocol C glue with wayland-scanner at configure time into the build tree.
local function generate_wayland_protocols(target)
    local scanner = import("lib.detect.find_tool")("wayland-scanner")
    local shared = os.iorunv("pkg-config", {"--variable=pkgdatadir", "wayland-protocols"}):trim()
    local protocols = {
        path.join(os.projectdir(), "third_party/wayland-protocols/wlr-layer-shell-unstable-v1.xml"),
        path.join(shared, "stable/xdg-shell/xdg-shell.xml")
    }
    local outdir = path.join(target:autogendir(), "wayland")
    os.mkdir(outdir)
    for _, xml in ipairs(protocols) do
        local name = path.basename(xml)
        local header = path.join(outdir, name .. "-client-protocol.h")
        local code = path.join(outdir, name .. "-protocol.c")
        if not os.isfile(code) or os.mtime(xml) > os.mtime(code) then
            os.vrunv(scanner.program, {"client-header", xml, header})
            os.vrunv(scanner.program, {"private-code", xml, code})
        end
        target:add("files", code)
    end
    target:add("includedirs", outdir)
end

local layer_exclude = has_config("layer_shell") and "" or "|app/platform/wayland_layer/**.cpp"

target("linux-wallpaperengine")
    set_kind("binary")
    set_targetdir("bin/$(mode)")
    set_rundir("$(projectdir)")
    set_warnings("all", "extra")
    add_packages("sokol", "linmath.h", "vulkan-headers", "lz4", "cjson", "stb", "miniaudio", "quickjs")
    add_includedirs("src", "/usr/include/libdrm", "/usr/include/shader-slang")
    add_syslinks("slang-compiler", "slang-rt", "vulkan", "X11", "Xcursor", "Xi", "avformat", "avcodec", "avutil", "swscale", "swresample", "va", "va-drm", "drm", "dl", "m", "pthread")
    add_defines("LWE_WEB=" .. (has_config("web") and "1" or "0"))
    add_defines("LWE_LAYER_SHELL=" .. (has_config("layer_shell") and "1" or "0"))

    if has_config("layer_shell") then
        add_syslinks("wayland-client")
        on_load(generate_wayland_protocols)
    end

    if is_mode("debug", "asan", "ubsan") then
        add_files("src/**.cpp|wallpaper/web/web_renderer_main.cpp" .. layer_exclude)
        add_defines("DEBUG_BUILD=1")
        add_packages("imgui")
    else
        add_files("src/**.cpp|ui/**.cpp|wallpaper/web/web_renderer_main.cpp" .. layer_exclude)
        add_defines("DEBUG_BUILD=0")
        set_symbols("hidden")
        set_optimize("fastest")
        set_strip("all")
    end

-- Optional out-of-process QtWebEngine renderer for web wallpapers. Kept in a
-- separate target so Qt headers and warning flags never touch the core build.
if has_config("web") then
    target("linux-wallpaperengine-webrender")
        set_kind("binary")
        set_targetdir("bin/$(mode)")
        set_rundir("$(projectdir)")
        add_files("src/wallpaper/web/web_renderer_main.cpp")
        add_includedirs("src")
        add_packages("pkgconfig::Qt6WebEngineWidgets")
        add_syslinks("pthread", "dl")
        -- Recent GCC emits copy relocations against Qt's protected
        -- staticMetaObject symbols; a PIC object avoids them.
        add_cxflags("-fPIC")
        add_ldflags("-pie")
        if is_mode("debug", "asan", "ubsan") then
            add_defines("DEBUG_BUILD=1")
        else
            add_defines("DEBUG_BUILD=0")
            set_optimize("fastest")
            set_strip("all")
        end
    target_end()
end

-- Synthetic unit checks. Not built by default; run explicitly with `xmake build tests`.
target("tests")
    set_kind("binary")
    set_default(false)
    set_targetdir("bin/$(mode)")
    set_warnings("all", "extra")
    add_packages("lz4", "stb")
    add_includedirs("src")
    add_files("tests/tex_video_detect_test.cpp", "src/shared/assets/tex_decoder.cpp",
              "src/shared/assets/tex_format.cpp", "src/shared/assets/tex_header.cpp",
              "src/shared/assets/tex_payload.cpp", "src/shared/core/logger.cpp", "src/shared/core/vfs.cpp")

    if is_mode("debug", "asan", "ubsan") then
        add_defines("DEBUG_BUILD=1")
    else
        add_defines("DEBUG_BUILD=0")
    end

-- Alpha keyframe curve checks. Not built by default; run with `xmake build alpha_tests`.
target("alpha_tests")
    set_kind("binary")
    set_default(false)
    set_targetdir("bin/$(mode)")
    set_warnings("all", "extra")
    add_includedirs("src")
    add_files("tests/alpha_curve_test.cpp", "src/wallpaper/2d/alpha_curve.cpp",
              "src/wallpaper/2d/animation_curve.cpp")

-- Command-line scanning checks. Not built by default; run with `xmake build cli_tests`.
target("cli_tests")
    set_kind("binary")
    set_default(false)
    set_targetdir("bin/$(mode)")
    set_warnings("all", "extra")
    add_includedirs("src")
    add_files("tests/cli_args_test.cpp", "src/app/cli_args.cpp")

-- Layer-shell option parsing checks. Not built by default; run with `xmake build layer_tests`.
target("layer_tests")
    set_kind("binary")
    set_default(false)
    set_targetdir("bin/$(mode)")
    set_warnings("all", "extra")
    add_includedirs("src")
    add_files("tests/layer_options_test.cpp", "src/app/platform/layer_options.cpp")

-- SceneScript (QuickJS) runtime checks. Not built by default; run with `xmake build scene_script_tests`.
target("scene_script_tests")
    set_kind("binary")
    set_default(false)
    set_targetdir("bin/$(mode)")
    set_warnings("all", "extra")
    add_includedirs("src")
    add_packages("quickjs")
    add_files("tests/scene_script_test.cpp", "src/wallpaper/2d/script/scene_script.cpp",
              "src/shared/core/logger.cpp")

-- Synthetic MDLV parser checks. Not built by default; run with `xmake build mdl_tests`.
target("mdl_tests")
    set_kind("binary")
    set_default(false)
    set_targetdir("bin/$(mode)")
    set_warnings("all", "extra")
    add_includedirs("src")
    add_packages("linmath.h")
    add_files("tests/mdl/*.cpp", "src/wallpaper/2d/puppet/mdl_parser.cpp",
              "src/wallpaper/2d/puppet/puppet_pose.cpp", "src/wallpaper/2d/tree/scene_tree.cpp")

-- Synthetic shader preprocessing checks. Not built by default; run with `xmake build shader_tests`.
target("shader_tests")
    set_kind("binary")
    set_default(false)
    set_targetdir("bin/$(mode)")
    set_warnings("all", "extra")
    add_includedirs("src")
    add_packages("sokol")
    add_files("tests/shader_preprocess_test.cpp", "src/shared/graphics/shader/shader_processor.cpp",
              "src/shared/graphics/shader/shader_processor_metadata.cpp",
              "src/shared/graphics/shader/shader_preprocessor_fix.cpp",
              "src/shared/graphics/shader/shader_vector_rewrite.cpp",
              "src/shared/graphics/shader/shader_swizzle_rewrite.cpp", "src/shared/core/vfs.cpp",
              "src/shared/core/logger.cpp")

-- Synthetic project detection checks. Not built by default; run with `xmake build project_tests`.
target("project_tests")
    set_kind("binary")
    set_default(false)
    set_targetdir("bin/$(mode)")
    set_warnings("all", "extra")
    add_packages("cjson")
    add_includedirs("src")
    add_files("tests/project_info_test.cpp", "src/wallpaper/project_info.cpp",
              "src/wallpaper/video/video_properties.cpp", "src/shared/core/vfs.cpp", "src/shared/core/logger.cpp")

-- Synthetic video property and rate checks. Not built by default; run with `xmake build video_tests`.
target("video_tests")
    set_kind("binary")
    set_default(false)
    set_targetdir("bin/$(mode)")
    set_warnings("all", "extra")
    add_packages("cjson")
    add_includedirs("src")
    add_files("tests/video_test.cpp", "src/wallpaper/video/video_properties.cpp")

task("check")
    set_menu {
        usage = "xmake check",
        description = "Validate formatting and run fast static analysis"
    }
    on_run(function ()
        print("--> Checking formatting (clang-format)...")
        os.execv("sh", {"-c", "find src -name '*.[ch]*' | xargs clang-format --dry-run --Werror"})

        print("--> Running static analysis (cppcheck)...")
        os.execv("sh", {"-c", "cppcheck -j 8 --quiet --enable=warning --error-exitcode=1 " ..
                             "'-D__has_feature(x)=0' " ..
                             "--suppress=preprocessorErrorDirective " ..
                             "--suppress=uninitMemberVarNoCtor " ..
                             "src/"})
        print("All checks passed!")
    end)

task("format")
    set_menu {
        usage = "xmake format",
        description = "Format all source files"
    }
    on_run(function ()
        print("--> Formatting files...")
        os.execv("sh", {"-c", "find src -name '*.[ch]*' | xargs clang-format -i"})
        print("Done!")
    end)

task("sandbox")
    set_menu {
        usage = "xmake sandbox",
        description = "Validate, build the debug binary, and launch the effect sandbox"
    }
    on_run(function ()
        print("--> Configuring debug build...")
        os.exec("xmake f -m debug")
        print("--> Running validation...")
        os.exec("xmake check")
        print("--> Building debug sandbox...")
        os.exec("xmake build linux-wallpaperengine")
        print("--> Launching sandbox...")
        os.exec("bin/debug/linux-wallpaperengine --sandbox")
    end)
