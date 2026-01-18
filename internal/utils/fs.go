package utils

import (
	"encoding/json"
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
		// Try extensions
		extensions := []string{".tex", ".png", ".jpg", ".jpeg", ".tex-json"}
		
		for _, ext := range extensions {
			// Try with cleanName + ext
			p := filepath.Join(dir, cleanName+ext)
			if _, err := os.Stat(p); err == nil {
				return p
			}

			// Try with original name + ext
			p = filepath.Join(dir, name+ext)
			if _, err := os.Stat(p); err == nil {
				return p
			}
			
			// Try exact match if name already has extension
			p = filepath.Join(dir, name)
			if _, err := os.Stat(p); err == nil {
				return p
			}
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
			if err == nil && !info.IsDir() {
				// Check for matching name ignoring extension first
				base := filepath.Base(path)
				ext := filepath.Ext(base)
				nameNoExt := strings.TrimSuffix(base, ext)
				
				targetBase := filepath.Base(cleanName)
				
				if (nameNoExt == cleanName || nameNoExt == targetBase) && 
				   (ext == ".tex" || ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
					foundPath = path
					return fmt.Errorf("found")
				}
			}
			return nil
		})
		if foundPath != "" {
			break
		}
	}

	return foundPath
}

// ExtractTexturePathFromJSON attempts to find a texture path inside a JSON file.
func ExtractTexturePathFromJSON(jsonPath string) (string, error) {
	data, err := os.ReadFile(jsonPath)
	if err != nil {
		return "", err
	}

	// Simple struct for partial unmarshaling
	var config struct {
		Image    string `json:"image"`
		Material string `json:"material"`
		Passes   []struct {
			Textures []string `json:"textures"`
		} `json:"passes"`
	}

	if err := json.Unmarshal(data, &config); err != nil {
		return "", err
	}

	if config.Image != "" {
		return config.Image, nil
	}
	if config.Material != "" {
		return config.Material, nil
	}
	if len(config.Passes) > 0 && len(config.Passes[0].Textures) > 0 {
		return config.Passes[0].Textures[0], nil
	}

	return "", fmt.Errorf("no texture found in JSON")
}

// ResolveTexturePath finds the real texture file handling JSON redirection.
func ResolveTexturePath(imagePath, modelPath string) string {
	path := imagePath
	if path == "" {
		path = modelPath
	}

	if path == "" {
		return ""
	}

	textureName := strings.TrimSuffix(filepath.Base(path), ".json")

	if strings.HasSuffix(path, ".json") {
		// Try to find the file in typical locations
		searchPaths := []string{
			filepath.Join("tmp", path),
			ResolveAssetPath(path),
			path,
		}

		var fullPath string
		for _, p := range searchPaths {
			if _, err := os.Stat(p); err == nil {
				fullPath = p
				break
			}
		}

		if fullPath != "" {
			if name, err := ExtractTexturePathFromJSON(fullPath); err == nil {
				// Recursive check (simplified level 1)
				if strings.HasSuffix(name, ".json") {
					nextPath := FindTextureFile(name)
					if nextPath == "" {
						// Try to locate the json file directly
						for _, sp := range []string{filepath.Join("tmp", name), ResolveAssetPath(name)} {
							if _, err := os.Stat(sp); err == nil {
								nextPath = sp
								break
							}
						}
					}
					if nextPath != "" {
						if name2, err := ExtractTexturePathFromJSON(nextPath); err == nil {
							textureName = name2
						}
					}
				} else {
					textureName = name
				}
			}
		}
	}

	return FindTextureFile(textureName)
}

func FindTexJSON(texturePath string) string {
	if texturePath == "" {
		return ""
	}
	
	p1 := texturePath + "-json"
	if _, err := os.Stat(p1); err == nil {
		return p1
	}

	ext := filepath.Ext(texturePath)
	if ext != "" {
		p2 := strings.TrimSuffix(texturePath, ext) + ".tex-json"
		if _, err := os.Stat(p2); err == nil {
			return p2
		}
		
		p3 := strings.TrimSuffix(texturePath, ext) + ".tex" + "-json"
		if _, err := os.Stat(p3); err == nil {
			return p3
		}
	}

	return ""
}
