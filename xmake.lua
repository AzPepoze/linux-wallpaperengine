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

-- On by default when libwayland-client, wayland-scanner and the lib/wlr-protocols submodule are present. Draws the wallpaper
-- on a wlr-layer-shell surface when --screen-root/--layer is given and a Wayland session is running.
option("layer_shell")
    set_showmenu(true)
    set_description("Enable the Wayland wlr-layer-shell desktop wallpaper backend")
    on_check(function (option)
        import("lib.detect.find_tool")
        import("lib.detect.find_package")
        if not os.isfile(path.join(os.projectdir(), "lib/wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml")) then
            cprint("${yellow}layer_shell disabled: run `git submodule update --init` to fetch lib/wlr-protocols")
            return
        end
        if find_tool("wayland-scanner") and find_package("pkgconfig::wayland-client") then
            option:enable(true)
        end
    end)
option_end()

-- On by default when libsystemd is installed. Exposes the MPRIS media session
-- source over sd-bus; disable with `xmake f --mpris=n`.
option("mpris")
    set_showmenu(true)
    set_description("Enable the MPRIS (sd-bus) media session source")
    on_check(function (option)
        import("lib.detect.find_package")
        if find_package("pkgconfig::libsystemd") then
            option:enable(true)
        end
    end)
option_end()

-- Generates the client protocol C glue with wayland-scanner at configure time into the build tree.
local function generate_wayland_protocols(target)
    local scanner = import("lib.detect.find_tool")("wayland-scanner")
    local shared = os.iorunv("pkg-config", {"--variable=pkgdatadir", "wayland-protocols"}):trim()
    local outdir = path.join(target:autogendir(), "wayland")
    os.mkdir(outdir)

    -- The request argument "namespace" is a C++ keyword in the generated header, so the build uses a copy of the
    -- upstream XML (lib/wlr-protocols submodule) with that argument spelled "namespace_".
    local layer_shell = path.join(outdir, "wlr-layer-shell-unstable-v1.xml")
    local patched = (io.readfile(path.join(os.projectdir(), "lib/wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml"))
                         :gsub('name="namespace"', 'name="namespace_"'))
    if not os.isfile(layer_shell) or io.readfile(layer_shell) ~= patched then
        io.writefile(layer_shell, patched)
    end

    local protocols = {
        layer_shell,
        path.join(shared, "stable/xdg-shell/xdg-shell.xml")
    }
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
    add_defines("LWE_MPRIS=" .. (has_config("mpris") and "1" or "0"))

    if has_config("mpris") then
        add_syslinks("systemd")
    end

    if has_config("layer_shell") then
        add_syslinks("wayland-client")
        on_load(generate_wayland_protocols)
    end

    if is_mode("debug", "asan", "ubsan") then
        add_files("src/**.cpp|wallpaper/web/web_renderer_main.cpp|shared/graphics/diagnostics/**.cpp" .. layer_exclude)
        -- Capture export hashes and diffs every pass image pixel by pixel, which takes minutes unoptimised.
        add_files("src/shared/graphics/diagnostics/**.cpp", {cxxflags = "-O2"})
        add_defines("DEBUG_BUILD=1")
        add_packages("imgui")
    else
        add_files("src/**.cpp|ui/**.cpp|wallpaper/web/web_renderer_main.cpp" .. layer_exclude)
        add_defines("DEBUG_BUILD=0")
        set_symbols("hidden")
        set_optimize("fastest")
        set_strip("all")
        set_policy("build.optimization.lto", true)
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

-- Plain-main unit checks. None build by default: `xmake build <name>` builds one, `xmake test` builds and runs them all.
-- Each takes its own source list so it links only what it exercises.
local function add_test(name, files, packages, syslinks)
    target(name)
        set_kind("binary")
        set_default(false)
        set_targetdir("bin/$(mode)")
        set_warnings("all", "extra")
        add_includedirs("src")
        if packages then add_packages(table.unpack(packages)) end
        if syslinks then add_syslinks(table.unpack(syslinks)) end
        add_files(table.unpack(files))
        add_tests("default")
        if is_mode("debug", "asan", "ubsan") then
            add_defines("DEBUG_BUILD=1")
        else
            add_defines("DEBUG_BUILD=0")
        end
    target_end()
end

add_test("tests", {"tests/tex_video_detect_test.cpp", "src/shared/assets/tex_decoder.cpp",
                   "src/shared/assets/tex_format.cpp", "src/shared/assets/tex_header.cpp",
                   "src/shared/assets/tex_payload.cpp", "src/shared/core/logger.cpp", "src/shared/core/vfs.cpp"},
         {"lz4", "stb"})

add_test("alpha_tests", {"tests/alpha_curve_test.cpp", "src/wallpaper/2d/alpha_curve.cpp",
                         "src/wallpaper/2d/animation_curve.cpp"})

add_test("cli_tests", {"tests/cli_args_test.cpp", "src/app/cli_args.cpp"})

add_test("layer_tests", {"tests/layer_options_test.cpp", "src/app/platform/layer_options.cpp"})

add_test("pointer_input_tests", {"tests/pointer_input_test.cpp", "src/wallpaper/2d/input/pointer_input.cpp"})

add_test("scene_script_tests", {"tests/scene_script_test.cpp", "src/wallpaper/2d/script/scene_script.cpp",
                                "src/wallpaper/2d/script/script_engine.cpp",
                                "src/wallpaper/2d/script/script_value_js.cpp", "src/shared/core/logger.cpp"},
         {"quickjs"})

add_test("mdl_tests", {"tests/mdl/*.cpp", "src/wallpaper/2d/puppet/mdl_parser.cpp",
                       "src/wallpaper/2d/puppet/puppet_pose.cpp", "src/wallpaper/2d/tree/scene_tree.cpp"},
         {"linmath.h"})

add_test("shader_tests", {"tests/shader_preprocess_test.cpp", "src/shared/graphics/shader/shader_processor.cpp",
                          "src/shared/graphics/shader/shader_processor_metadata.cpp",
                          "src/shared/graphics/shader/shader_preprocessor_fix.cpp",
                          "src/shared/graphics/shader/shader_vector_rewrite.cpp",
                          "src/shared/graphics/shader/shader_swizzle_rewrite.cpp", "src/shared/core/vfs.cpp",
                          "src/shared/core/logger.cpp", "src/shared/core/disk_cache.cpp"},
         {"sokol"})

add_test("project_tests", {"tests/project_info_test.cpp", "src/wallpaper/project_info.cpp",
                           "src/wallpaper/video/video_properties.cpp", "src/shared/core/vfs.cpp",
                           "src/shared/core/logger.cpp"},
         {"cjson"})

add_test("user_properties_tests", {"tests/user_properties_test.cpp", "src/wallpaper/user_properties.cpp",
                                   "src/wallpaper/project_info.cpp", "src/wallpaper/video/video_properties.cpp",
                                   "src/shared/core/vfs.cpp", "src/shared/core/logger.cpp"},
         {"cjson"})

add_test("scene_parser_tests", {"tests/scene_parser_test.cpp", "src/wallpaper/2d/parser/scene_parser.cpp",
                                "src/wallpaper/user_properties.cpp", "src/shared/core/utils.cpp",
                                "src/shared/core/vfs.cpp", "src/shared/core/logger.cpp"},
         {"cjson"})

add_test("video_tests", {"tests/video_test.cpp", "src/wallpaper/video/video_properties.cpp"}, {"cjson"})

add_test("media_tests", {"tests/media_source_test.cpp", "src/shared/assets/media/media_source.cpp",
                         "src/shared/core/vfs.cpp", "src/shared/core/logger.cpp"},
         nil, {"avformat", "avutil"})

add_test("animation_timelines_tests", {"tests/animation_timelines_test.cpp",
                                       "src/wallpaper/2d/animation/animation_timelines.cpp",
                                       "src/wallpaper/2d/animation_curve.cpp"})

add_test("transition_catalog_tests", {"tests/transition_catalog_test.cpp",
                                      "src/wallpaper/transition/transition_catalog.cpp"})

add_test("control_protocol_tests", {"tests/control_protocol_test.cpp", "src/app/control/control_protocol.cpp"},
         {"cjson"})

add_test("control_endpoint_tests", {"tests/control_endpoint_test.cpp", "src/app/control/control_endpoint.cpp"})

add_test("control_socket_tests", {"tests/control_socket_test.cpp", "src/app/control/control_server.cpp",
                                  "src/app/control/control_client.cpp", "src/app/control/control_protocol.cpp",
                                  "src/app/control/control_endpoint.cpp"}, {"cjson"})

add_test("media_session_tests", {"tests/media_session_test.cpp", "src/shared/media/thumbnail_colors.cpp",
                                 "src/shared/core/logger.cpp"}, {"stb"})

add_test("media_events_tests", {"tests/media_events_test.cpp", "src/wallpaper/2d/script/scene_script.cpp",
                                "src/wallpaper/2d/script/script_engine.cpp", "src/wallpaper/2d/script/media_events.cpp",
                                "src/wallpaper/2d/script/script_value_js.cpp",
                                "src/shared/media/mpris_source.cpp", "src/shared/core/logger.cpp"},
         {"quickjs"})

-- Loads every SceneScript block of a Workshop folder and reports script errors (no GPU). See utils/script_corpus.cpp.
target("script_corpus")
    set_kind("binary")
    set_default(false)
    set_targetdir("bin/$(mode)")
    set_warnings("all", "extra")
    add_includedirs("src")
    add_packages("cjson", "quickjs")
    add_files("utils/script_corpus.cpp", "src/wallpaper/2d/script/scene_script.cpp",
              "src/wallpaper/2d/script/script_engine.cpp", "src/wallpaper/2d/script/script_value_js.cpp",
              "src/shared/assets/unpack.cpp",
              "src/shared/core/utils.cpp", "src/shared/core/vfs.cpp", "src/shared/core/logger.cpp",
              "src/wallpaper/2d/parser/scene_parser.cpp", "src/wallpaper/user_properties.cpp",
              "src/wallpaper/2d/animation/animation_timelines.cpp",
              "src/wallpaper/2d/animation_curve.cpp")
    if is_mode("debug", "asan", "ubsan") then
        add_defines("DEBUG_BUILD=1")
    else
        add_defines("DEBUG_BUILD=0")
    end
target_end()

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
