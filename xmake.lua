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

target("linux-wallpaperengine")
    set_kind("binary")
    set_targetdir("bin/$(mode)")
    set_rundir("$(projectdir)")
    set_warnings("all", "extra")
    add_packages("sokol", "linmath.h", "vulkan-headers", "lz4", "cjson", "stb", "miniaudio")
    add_includedirs("src", "/usr/include/libdrm", "/usr/include/shader-slang")
    add_syslinks("slang-compiler", "slang-rt", "vulkan", "X11", "Xcursor", "Xi", "avformat", "avcodec", "avutil", "swscale", "swresample", "va", "va-drm", "drm", "dl", "m", "pthread")
    add_defines("LWE_WEB=" .. (has_config("web") and "1" or "0"))

    if is_mode("debug", "asan", "ubsan") then
        add_files("src/**.cpp|wallpaper/web/web_renderer_main.cpp")
        add_defines("DEBUG_BUILD=1")
        add_packages("imgui")
    else
        add_files("src/**.cpp|ui/**.cpp|wallpaper/web/web_renderer_main.cpp")
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
    add_files("tests/tex_video_detect_test.cpp", "src/shared/assets/tex_decoder.cpp", "src/shared/core/logger.cpp")

    if is_mode("debug", "asan", "ubsan") then
        add_defines("DEBUG_BUILD=1")
    else
        add_defines("DEBUG_BUILD=0")
    end

-- Synthetic MDLV parser checks. Not built by default; run with `xmake build mdl_tests`.
target("mdl_tests")
    set_kind("binary")
    set_default(false)
    set_targetdir("bin/$(mode)")
    set_warnings("all", "extra")
    add_includedirs("src")
    add_files("tests/mdl/*.cpp", "src/wallpaper/2d/puppet/mdl_parser.cpp",
              "src/wallpaper/2d/puppet/puppet_pose.cpp")

-- Synthetic shader preprocessing checks. Not built by default; run with `xmake build shader_tests`.
target("shader_tests")
    set_kind("binary")
    set_default(false)
    set_targetdir("bin/$(mode)")
    set_warnings("all", "extra")
    add_includedirs("src")
    add_packages("sokol")
    add_files("tests/shader_preprocess_test.cpp", "src/shared/graphics/shader/shader_processor.cpp")

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
