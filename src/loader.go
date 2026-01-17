package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"linux-wallpaperengine/src/convert"
	"linux-wallpaperengine/src/utils"
	"linux-wallpaperengine/src/wallpaper"
	"linux-wallpaperengine/src/wallpaper/feature"

	rl "github.com/gen2brain/raylib-go/raylib"
)

func resolveTexturePath(object *wallpaper.Object) string {
	path := object.Image
	if path == "" {
		path = object.Model
	}

	if path == "" {
		return ""
	}

	textureName := strings.TrimSuffix(filepath.Base(path), ".json")

	if strings.HasSuffix(path, ".json") {
		// Try to find the file in typical locations
		searchPaths := []string{
			filepath.Join("tmp", path),
			filepath.Join("assets", path),
			path, // In case it is absolute or relative to root
		}

		var fullPath string
		for _, p := range searchPaths {
			if _, err := os.Stat(p); err == nil {
				fullPath = p
				break
			}
		}

		if fullPath != "" {
			if name, err := extractTextureFromJSONPath(fullPath); err == nil {
				textureName = name
			}
		} else {
			// Fallback: try just the filename in tmp
			if name, err := extractTextureFromJSONPath(filepath.Join("tmp", path)); err == nil {
				textureName = name
			}
		}
	}

	return findTextureFile(textureName)
}

func LoadModelConfig(path string) (*wallpaper.ModelJSON, error) {
	searchPaths := []string{
		filepath.Join("tmp", path),
		filepath.Join("assets", path),
		path,
	}

	for _, p := range searchPaths {
		data, err := os.ReadFile(p)
		if err == nil {
			var config wallpaper.ModelJSON
			if err := json.Unmarshal(data, &config); err == nil {
				return &config, nil
			}
		}
	}
	return nil, fmt.Errorf("model config not found or invalid: %s", path)
}

func loadParticleSystem(name string, particlePath string, override *wallpaper.InstanceOverride) *feature.ParticleSystem {
	// Try multiple root paths for the particle JSON itself
	possibleParticlePaths := []string{
		particlePath,
		filepath.Join("tmp", particlePath),
		filepath.Join("assets", particlePath),
		filepath.Join("tmp/particles", filepath.Base(particlePath)),
	}

	var data []byte
	var err error
	var fullParticlePath string
	for _, p := range possibleParticlePaths {
		data, err = os.ReadFile(p)
		if err == nil {
			fullParticlePath = p
			break
		}
	}

	if err != nil {
		// Final attempt: recursive search in tmp
		filepath.Walk("tmp", func(path string, info os.FileInfo, walkErr error) error {
			if walkErr == nil && !info.IsDir() && (strings.HasSuffix(path, particlePath) || strings.HasSuffix(path, filepath.Base(particlePath))) {
				data, err = os.ReadFile(path)
				if err == nil {
					fullParticlePath = path
					return fmt.Errorf("found")
				}
			}
			return nil
		})
	}

	if len(data) == 0 {
		utils.Error("Failed to load particle JSON for %s (Path: %s)", name, particlePath)
		return nil
	}

	var config wallpaper.ParticleJSON
	if err := json.Unmarshal(data, &config); err != nil {
		utils.Error("Failed to unmarshal particle JSON for %s: %s", name, err)
		return nil
	}

	var texture *rl.Texture2D
	var extraTextures []*rl.Texture2D
	textureName := ""
	var textureNames []string
	blendMode := rl.BlendAdditive 
	var texInfo *wallpaper.TexJSON

	if config.Material != "" {
		var materialPath string
		if strings.HasSuffix(config.Material, ".json") {
			possibleMaterialPaths := []string{
				filepath.Join("tmp", config.Material),
				filepath.Join("assets", config.Material),
				filepath.Join("tmp/materials", config.Material),
				filepath.Join(filepath.Dir(fullParticlePath), config.Material),
			}

			for _, p := range possibleMaterialPaths {
				if _, err := os.Stat(p); err == nil {
					materialPath = p
					break
				}
			}
		}

		if materialPath != "" {
			mData, err := os.ReadFile(materialPath)
			if err == nil {
				var material wallpaper.MaterialJSON
				if err := json.Unmarshal(mData, &material); err == nil {
					if len(material.Passes) > 0 {
						pass := material.Passes[0]
						if len(pass.Textures) > 0 {
							textureName = pass.Textures[0]
							textureNames = pass.Textures
						}
						// Check blending
						if pass.Blending == "additive" {
							blendMode = rl.BlendAdditive
						} else if pass.Blending == "alpha" {
							blendMode = rl.BlendAlpha
						} else {
							blendMode = rl.BlendAdditive 
						}
					}
				}
			}
		} else {
			// Material is just a texture name?
			textureName = config.Material
			textureNames = []string{textureName}
		}

		if len(textureNames) > 0 {
			primaryIndex := 0
			// If the first texture is "blank", try to find a better primary texture
			if strings.Contains(strings.ToLower(textureNames[0]), " blank") {
				for i := 1; i < len(textureNames); i++ {
					if !strings.Contains(strings.ToLower(textureNames[i]), " blank") {
						primaryIndex = i
						break
					}
				}
			}

			for i, tName := range textureNames {
				tPath := findTextureFile(tName)
				if tPath != "" {
					if image, err := convert.LoadTextureNative(tPath); err == nil {
						if i == primaryIndex {
							texture = image
							
							// Check for .tex-json only for the primary texture for now
							texJsonPath := tPath + "-json"
							if _, err := os.Stat(texJsonPath); err == nil {
								if data, err := os.ReadFile(texJsonPath); err == nil {
									var info wallpaper.TexJSON
									if err := json.Unmarshal(data, &info); err == nil {
										texInfo = &info
									}
								}
							}
						} else {
							extraTextures = append(extraTextures, image)
						}
					}
				}
			}
		}
	}

	return feature.NewParticleSystem(feature.ParticleSystemOptions{
		Name:          name,
		Config:        config,
		Texture:       texture,
		ExtraTextures: extraTextures,
		TextureName:   textureName,
		Override:      override,
		BlendMode:     blendMode,
		TexInfo:       texInfo,
	})
}

func extractTextureFromJSONPath(fullPath string) (string, error) {
	data, err := os.ReadFile(fullPath)
	if err != nil {
		return "", err
	}

	var material wallpaper.MaterialJSON
	if err := json.Unmarshal(data, &material); err == nil {
		if len(material.Passes) > 0 && len(material.Passes[0].Textures) > 0 {
			tex := material.Passes[0].Textures[0]
			if tex != "" {
				return tex, nil
			}
		}
	}

	var model wallpaper.ModelJSON
	if err := json.Unmarshal(data, &model); err == nil && model.Material != "" {
		// Try relative to the current file's directory first
		baseDir := filepath.Dir(fullPath)
		p := filepath.Join(baseDir, model.Material)
		if name, err := extractTextureFromJSONPath(p); err == nil {
			return name, nil
		}

		// Fallback to searching
		searchPaths := []string{
			filepath.Join("tmp", model.Material),
			filepath.Join("assets", model.Material),
		}
		for _, p := range searchPaths {
			if name, err := extractTextureFromJSONPath(p); err == nil {
				return name, nil
			}
		}
	}

	return "", fmt.Errorf("no textures found in %s", fullPath)
}

func findTextureFile(name string) string {
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
	filepath.Walk("assets", func(path string, info os.FileInfo, err error) error {
		if err == nil && !info.IsDir() && (strings.HasSuffix(path, cleanName+".tex") || strings.HasSuffix(path, filepath.Base(cleanName)+".tex")) {
			foundPath = path
			return fmt.Errorf("found")
		}
		return nil
	})

	return foundPath
}
