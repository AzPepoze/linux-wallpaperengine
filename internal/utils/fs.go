package utils

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

var WallpaperEngineAssets string

func ResolveAssetPath(relPath string) string {
	// Try local assets first
	localPath := filepath.Join("assets", relPath)
	if _, err := os.Stat(localPath); err == nil {
		return localPath
	}

	// Try discovered Steam assets
	if WallpaperEngineAssets != "" {
		steamPath := filepath.Join(WallpaperEngineAssets, relPath)
		if _, err := os.Stat(steamPath); err == nil {
			return steamPath
		}
	}

	return localPath // Fallback to local even if not exists
}

func FindTextureFile(name string) string {
	if name == "" {
		return ""
	}

	cleanName := strings.TrimPrefix(name, "materials/")
	cleanName = strings.TrimSuffix(cleanName, ".tex")

	searchDirs := []string{
		"converted",
		"tmp/materials",
		"tmp/materials/workshop",
		"tmp/materials/presets",
		"tmp",
		"assets/materials",
		"assets",
	}

	if WallpaperEngineAssets != "" {
		searchDirs = append(searchDirs,
			filepath.Join(WallpaperEngineAssets, "materials"),
			WallpaperEngineAssets,
		)
	}

	for _, dir := range searchDirs {
		p := filepath.Join(dir, cleanName+".tex")
		if _, err := os.Stat(p); err == nil {
			return p
		}

		// Try with original name inside dir too
		p = filepath.Join(dir, name+".tex")
		if _, err := os.Stat(p); err == nil {
			return p
		}
	}

	// Recursive fallback for assets folder specifically (deep search)
	var foundPath string
	dirsToWalk := []string{"assets"}
	if WallpaperEngineAssets != "" {
		dirsToWalk = append(dirsToWalk, WallpaperEngineAssets)
	}

	for _, d := range dirsToWalk {
		if _, err := os.Stat(d); err != nil {
			continue
		}
		filepath.Walk(d, func(path string, info os.FileInfo, err error) error {
			if err == nil && !info.IsDir() && (strings.HasSuffix(path, cleanName+".tex") || strings.HasSuffix(path, filepath.Base(cleanName)+".tex")) {
				foundPath = path
				return fmt.Errorf("found")
			}
			return nil
		})
		if foundPath != "" {
			break
		}
	}

	return foundPath
}
