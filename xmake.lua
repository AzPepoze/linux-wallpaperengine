add_rules("mode.debug", "mode.release")

option("vulkan")
    set_default(false)
    set_showmenu(true)
    set_description("Build with the experimental Sokol Vulkan backend")
option_end()

add_requires("imgui")
if has_config("vulkan") then
    add_requires("cmake::slang", {
        alias = "slang_shader",
        system = true,
        configs = {
            search_mode = "config",
            link_libraries = {"slang::slang"}
        }
    })
end

target("linux-wallpaperengine")
    set_kind("binary")
    add_packages("imgui")

    if has_config("vulkan") then
        add_packages("slang_shader")
        add_defines("LWE_SOKOL_VULKAN")
    end
    
    -- Source files
    add_files("src/main.cpp")
    add_files("src/**/*.cpp")
    add_files("libs/cJSON.c")
    add_files("libs/lz4.c")

    -- Include directories
    add_includedirs("libs")
    add_includedirs("libs/sokol")
    add_includedirs("src")

    -- System libraries
    if has_config("vulkan") then
        add_syslinks("vulkan", "X11", "Xcursor", "Xi", "dl", "m", "pthread")
    else
        add_syslinks("GL", "X11", "Xcursor", "Xi", "dl", "m", "pthread")
    end

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
        description = "Format code and run fast static analysis"
    }
    on_run(function ()
        -- 1. Format code first
        print("--> Formatting files...")
        os.execv("sh", {"-c", "find src/ -name '*.[ch]*' | xargs clang-format -i"})

        -- 2. Check Formatting (Fast)
        print("--> Checking formatting (clang-format)...")
        os.execv("sh", {"-c", "find src/ -name '*.[ch]*' | xargs clang-format --dry-run --Werror"})
        
        -- 3. Fast Static Analysis
        print("--> Running static analysis (cppcheck)...")
        -- We suppress libs/ and provide some common defines to avoid false positives or syntax errors in 3rd party headers
        os.execv("sh", {"-c", "cppcheck -j 8 --quiet --enable=warning --error-exitcode=1 " ..
                             "'-D__has_feature(x)=0' " ..
                             "--suppress=preprocessorErrorDirective " ..
                             "--suppress=*:libs/* " ..
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
        os.execv("sh", {"-c", "find src/ -name '*.[ch]*' | xargs clang-format -i"})
        print("Done!")
    end)

task("dev")
    set_menu {
        usage = "xmake dev [wallpaper_path]",
        description = "Run formatting check, static analysis, build and run the wallpaper",
        options = {
            {nil, "pkg", "v", nil, "Specify the wallpaper path"}
        }
    }
    on_run(function ()
        import("core.base.option")

        -- 1. Run Check
        print("--> Running checks...")
        os.exec("xmake check")

        -- 2. Build
        print("--> Building...")
        os.exec("xmake")

        -- 3. Run
        local path = option.get("pkg")
        if not path then
            local args = option.get("arguments")
            if args and #args > 0 then
                path = args[1]
            end
        end
        
        -- Resolve from environment variable
        if not path or path == "" then
            path = os.getenv("DEFAULT_WALLPAPER_PATH") or os.getenv("WALLPAPER_PATH")
        end

        -- Resolve from config.json
        if not path or path == "" then
            if os.isfile("config.json") then
                import("core.base.json")
                local cfg = try { function() return json.loadfile("config.json") end }
                if cfg and (cfg.default_wallpaper or cfg.wallpaper_path) then
                    path = cfg.default_wallpaper or cfg.wallpaper_path
                end
            end
        end

        if path and path ~= "" then
            print("--> Running with: " .. path)
            os.execv("xmake", {"run", "linux-wallpaperengine", path})
        else
            print("--> Running without wallpaper argument (configure 'default_wallpaper' in config.json or pass path)")
            os.execv("xmake", {"run", "linux-wallpaperengine"})
        end
    end)
