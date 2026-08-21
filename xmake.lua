add_rules("mode.debug", "mode.release")
set_languages("cxx17")

-- Track the upstream heads. Run `xmake require --upgrade` when you want to
-- refresh the cached dependency revisions.
add_requires("sokol master")
add_requires("linmath.h master")
add_requires("vulkan-headers")
add_requires("lz4")
add_requires("cjson")
add_requires("stb")
add_requires("imgui", {optional = true})
add_requires("cmake::slang", {
    alias = "slang_shader",
    system = true,
    configs = {
        search_mode = "config",
        link_libraries = {"slang::slang"}
    }
})

target("linux-wallpaperengine")
    set_kind("binary")
    set_targetdir("bin/$(mode)")
    add_packages("sokol", "linmath.h", "slang_shader", "vulkan-headers", "lz4", "cjson", "stb")
    add_includedirs("src")
    add_syslinks("vulkan", "X11", "Xcursor", "Xi", "dl", "m", "pthread")

    if is_mode("debug") then
        add_files("src/**.cpp")
        add_defines("DEBUG_BUILD=1")
        add_packages("imgui")
    else
        add_files("src/**.cpp|ui/**.cpp|render/diagnostics/**.cpp")
        add_defines("DEBUG_BUILD=0")
        set_symbols("hidden")
        set_optimize("fastest")
        set_strip("all")
    end

target("test_diagnostics")
    set_kind("binary")
    set_default(false)
    set_targetdir("bin/$(mode)")
    add_defines("DEBUG_BUILD=1")
    add_packages("sokol", "linmath.h", "cjson", "stb")
    add_files("tests/test_diagnostics.cpp")
    add_files("src/ui/sandbox_catalog.cpp")
    add_files("src/render/diagnostics/diagnostic_config.cpp")
    add_files("src/render/diagnostics/image_stats.cpp")
    add_files("src/render/diagnostics/render_graph.cpp")
    add_files("src/render/diagnostics/uniform_provenance.cpp")
    add_files("src/render/shader/shader_processor.cpp")
    add_files("src/wallpaper/scene/2d/effects/effect_configuration.cpp")
    add_includedirs("src")

task("check")
    set_menu {
        usage = "xmake check",
        description = "Validate formatting and run fast static analysis"
    }
    on_run(function ()
        print("--> Checking formatting (clang-format)...")
        os.execv("sh", {"-c", "find src tests -name '*.[ch]*' | xargs clang-format --dry-run --Werror"})

        print("--> Running static analysis (cppcheck)...")
        os.execv("sh", {"-c", "cppcheck -j 8 --quiet --enable=warning --error-exitcode=1 " ..
                             "'-D__has_feature(x)=0' " ..
                             "--suppress=preprocessorErrorDirective " ..
                             "--suppress=uninitMemberVarNoCtor " ..
                             "src/ tests/"})
        print("All checks passed!")
    end)

task("format")
    set_menu {
        usage = "xmake format",
        description = "Format all source files"
    }
    on_run(function ()
        print("--> Formatting files...")
        os.execv("sh", {"-c", "find src tests -name '*.[ch]*' | xargs clang-format -i"})
        print("Done!")
    end)

task("dev")
    set_menu {
        usage = "xmake dev",
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
        os.execv("bin/debug/linux_wallpaperengine", {"--sandbox"})
    end)
