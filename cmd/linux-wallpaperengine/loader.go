package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"linux-wallpaperengine/internal/convert"
	"linux-wallpaperengine/internal/utils"
	"linux-wallpaperengine/internal/wallpaper"
	"linux-wallpaperengine/internal/engine2D"

	rl "github.com/gen2brain/raylib-go/raylib"
)

func resolveTexturePath(object *wallpaper.Object) string {
	return utils.ResolveTexturePath(object.Image, object.Model)
}

func LoadModelConfig(path string) (*wallpaper.ModelJSON, error) {
	searchPaths := []string{
		filepath.Join("tmp", path),
		utils.ResolveAssetPath(path),
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

func loadParticleSystem(name string, particlePath string, override *wallpaper.InstanceOverride) *engine2D.ParticleSystem {
	// Try multiple root paths for the particle JSON itself
	possibleParticlePaths := []string{
		particlePath,
		filepath.Join("tmp", particlePath),
		utils.ResolveAssetPath(particlePath),
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
				utils.ResolveAssetPath(config.Material),
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
				var material engine2D.MaterialJSON
				if err := json.Unmarshal(mData, &material); err == nil {
					if len(material.Passes) > 0 {
						pass := material.Passes[0]
						if len(pass.Textures) > 0 {
							textureName = pass.Textures[0]
							textureNames = pass.Textures
						}
						// Check blending
						switch pass.Blending {
						case "additive":
							blendMode = rl.BlendAdditive
						case "alpha":
							blendMode = rl.BlendAlpha
						default:
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
				tPath := utils.FindTextureFile(tName)
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

	return engine2D.NewParticleSystem(engine2D.ParticleSystemOptions{
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
