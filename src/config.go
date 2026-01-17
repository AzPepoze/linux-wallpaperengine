package main

import (
	"os"
	"path/filepath"
	"linux-wallpaperengine/src/utils"
)

var AssetsPath string

func DiscoverAssets(customPath string) {
	if customPath != "" {
		if _, err := os.Stat(customPath); err == nil {
			AssetsPath = customPath
			utils.Info("Using custom assets path: %s", AssetsPath)
			return
		}
		utils.Warn("Custom assets path NOT FOUND: %s", customPath)
		utils.Info("Falling back to automatic discovery...")
	}

	home, _ := os.UserHomeDir()
	
	possiblePaths := []string{
		filepath.Join(home, ".local/share/Steam/steamapps/common/wallpaper_engine/assets"),
		filepath.Join(home, ".steam/steam/steamapps/common/wallpaper_engine/assets"),
		filepath.Join(home, ".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets"),
		"/usr/share/wallpaper_engine/assets",
	}

	for _, p := range possiblePaths {
		if _, err := os.Stat(p); err == nil {
			AssetsPath = p
			utils.Info("Discovered Wallpaper Engine assets at: %s", AssetsPath)
			return
		}
	}

	utils.Warn("Could not find Wallpaper Engine assets folder in any of the expected locations.")
	utils.Warn("Shaders, textures, and effects from the core engine might fail to load.")
}