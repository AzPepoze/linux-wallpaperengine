add_rules("mode.debug", "mode.release")

add_requires("imgui")

target("linux-wallpaperengine")
    set_kind("binary")
    add_packages("imgui")
    
    -- Source files
    add_files("src/*.cpp")
    add_files("src/core/*.c")
    add_files("src/core/*.cpp")
    add_files("src/asset/*.c")
    add_files("src/asset/*.cpp")
    add_files("src/render/*.c")
    add_files("src/sokol/*.cpp")
    add_files("src/ui/*.cpp")
    add_files("src/scene/*.cpp")
    add_files("src/scene/**/*.cpp")
    
    add_files("libs/cJSON.c")
    add_files("libs/lz4.c")

    -- Include directories
    add_includedirs("libs")
    add_includedirs("libs/sokol")
    add_includedirs("src")

    -- System libraries
    add_syslinks("GL", "X11", "Xcursor", "Xi", "m", "pthread")

    -- Optimization for release
    if is_mode("release") then
        set_symbols("hidden")
        set_optimize("fastest")
        set_strip("all")
    end

    -- After build, copy to bin/ for compatibility with your previous workflow
    after_build(function (target)
        os.mkdir("bin")
        os.cp(target:targetfile(), "bin/linux_wallpaperengine")
    end)

task("check")
    set_menu {
        usage = "xmake check",
        description = "Check code formatting and run fast static analysis"
    }
    on_run(function ()
        -- 1. Check Formatting (Fast)
        print("--> Checking formatting (clang-format)...")
        os.execv("sh", {"-c", "find src/ -name '*.[ch]*' | xargs clang-format --dry-run --Werror"})
        
        -- 2. Fast Static Analysis (Limit to our src and avoid deep template expansion)
        print("--> Running static analysis (cppcheck fast mode)...")
        os.execv("sh", {"-c", "cppcheck -j 8 --quiet --enable=warning --error-exitcode=1 src/"})
        
        print("All checks passed!")
    end)

task("format")
    set_menu {
        usage = "xmake format",
        description = "Format all source files"
    }
    on_run(function ()
        print("--> Formatting files...")
        os.execv("sh", {"-c", "find src/ -name '*.[ch]*' | xargs clang-format -i"})
        print("Done!")
    end)
