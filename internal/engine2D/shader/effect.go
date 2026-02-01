package shader

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"

	"linux-wallpaperengine/internal/convert"
	"linux-wallpaperengine/internal/utils"
	"linux-wallpaperengine/internal/wallpaper"

	rl "github.com/gen2brain/raylib-go/raylib"
)

// LoadEffect loads an effect configuration and all its passes with textures and shaders.
func LoadEffect(effectConfig *wallpaper.Effect) LoadedEffect {
	if strings.Contains(effectConfig.File, "bokeh_blur") {
		utils.Info("Effect: Unsupported effect 'bokeh_blur' detected, skipping loading.")
		return LoadedEffect{Config: effectConfig}
	}

	loaded := LoadedEffect{
		Config: effectConfig,
	}

	effectFile := effectConfig.File
	if effectFile != "" {
		utils.Debug("Effect: Loading %s", effectFile)
	}

	// 1. Load full effect config for base definition
	var fullEffect wallpaper.Effect
	if effectFile != "" {
		path := filepath.Join("tmp", effectFile)
		if data, err := os.ReadFile(path); err == nil {
			json.Unmarshal(data, &fullEffect)
		}
	}

	// Prefer passes from scene config, fallback to effect definition
	passes := effectConfig.Passes
	if len(passes) == 0 {
		passes = fullEffect.Passes
	}

	for i, pass := range passes {
		var basePass *wallpaper.EffectPass
		if i < len(fullEffect.Passes) {
			basePass = &fullEffect.Passes[i]
		}

		// Property Merging (Scene Instance overrides Base Definition) ---
		// Merge ConstantShaderValues
		if basePass != nil && basePass.ConstantShaderValues != nil {
			if pass.ConstantShaderValues == nil {
				pass.ConstantShaderValues = make(ConstantShaderValues)
			}
			for k, v := range basePass.ConstantShaderValues {
				if _, exists := pass.ConstantShaderValues[k]; !exists {
					pass.ConstantShaderValues[k] = v
				}
			}
		}

		// Merge Combos
		combos := pass.Combos
		if basePass != nil && basePass.Combos != nil {
			if combos == nil {
				combos = make(map[string]int)
			}
			for k, v := range basePass.Combos {
				if _, exists := combos[k]; !exists {
					combos[k] = v
				}
			}
		}

		// Resolution Logic
		shaderName := pass.Shader
		materialPath := pass.Material
		if shaderName == "" && basePass != nil {
			shaderName = basePass.Shader
			if materialPath == "" {
				materialPath = basePass.Material
			}
		}

		var mat *MaterialJSON
		if shaderName == "" && materialPath != "" {
			if m, err := LoadMaterial(materialPath); err == nil {
				mat = m
				if len(mat.Passes) > 0 {
					shaderName = mat.Passes[0].Shader
					// Merge combos from material
					if mat.Passes[0].Combos != nil {
						if combos == nil {
							combos = make(map[string]int)
						}
						for k, v := range mat.Passes[0].Combos {
							if _, exists := combos[k]; !exists {
								combos[k] = v
							}
						}
					}
					// Merge constants from material
					if mat.Passes[0].ConstantShaderValues != nil {
						if pass.ConstantShaderValues == nil {
							pass.ConstantShaderValues = make(ConstantShaderValues)
						}
						for k, v := range mat.Passes[0].ConstantShaderValues {
							if _, exists := pass.ConstantShaderValues[k]; !exists {
								pass.ConstantShaderValues[k] = v
							}
						}
					}
				}
			}
		}

		// Texture Merging (Scene > Effect > Material)
		var finalTexNames []*string

		// Fill from Material if exists
		if mat != nil && len(mat.Passes) > 0 {
			for _, t := range mat.Passes[0].Textures {
				s := t
				finalTexNames = append(finalTexNames, &s)
			}
		}

		// Overlay from Effect definition
		if basePass != nil {
			for j, t := range basePass.Textures {
				if j < len(finalTexNames) {
					if t != nil {
						finalTexNames[j] = t
					}
				} else {
					finalTexNames = append(finalTexNames, t)
				}
			}
		}

		// Final overlay from Scene instance
		for j, t := range pass.Textures {
			if j < len(finalTexNames) {
				if t != nil {
					finalTexNames[j] = t
				}
			} else {
				finalTexNames = append(finalTexNames, t)
			}
		}

		// Load actual textures
		var loadedTextures []*rl.Texture2D
		utils.Debug("Effect: [Pass %d] Final texture names: %v", i, finalTexNames)
		for tIdx, texName := range finalTexNames {
			if texName == nil || *texName == "" {
				utils.Debug("Effect: [Pass %d, Tex %d] Skipping empty texture name", i, tIdx)
				loadedTextures = append(loadedTextures, nil)
				continue
			}
			tPath := utils.FindTextureFile(*texName)
			if tPath != "" {
				utils.Debug("Effect: [Pass %d, Tex %d] Loading texture '%s' from '%s'", i, tIdx, *texName, tPath)
				if img, err := convert.LoadTextureNative(tPath); err == nil {
					utils.Debug("Effect: [Pass %d, Tex %d] Loaded SUCCESS", i, tIdx)
					loadedTextures = append(loadedTextures, img)
				} else {
					utils.Warn("Effect: [Pass %d, Tex %d] Loaded FAILED: %v", i, tIdx, err)
					loadedTextures = append(loadedTextures, nil)
				}
			} else {
				utils.Warn("Effect: [Pass %d, Tex %d] Texture path NOT FOUND for '%s'", i, tIdx, *texName)
				loadedTextures = append(loadedTextures, nil)
			}
		}
		utils.Debug("Effect: [Pass %d] Loaded %d textures", i, len(loadedTextures))
		for ti, t := range loadedTextures {
			if t != nil {
				utils.Debug("Effect: [Pass %d, Tex %d] Texture ID: %d", i, ti, t.ID)
			} else {
				utils.Debug("Effect: [Pass %d, Tex %d] Texture is nil", i, ti)
			}
		}

		// Load Shader
		var shader rl.Shader
		if shaderName != "" {
			utils.Debug("Effect: Pass %d using shader: %s", i, shaderName)

			if combos == nil {
				combos = make(map[string]int)
			}
			if _, exists := combos["MASK"]; !exists {
				utils.Info("Effect: Auto-enabling MASK combo for %s (Default to 1)", shaderName)
				combos["MASK"] = 1
			}

			shader = LoadShader(shaderName, combos)

			// Load defaults from shader comments
			defaults := GetShaderDefaults(shaderName)
			if pass.ConstantShaderValues == nil {
				pass.ConstantShaderValues = make(ConstantShaderValues)
			}
			for k, v := range defaults {
				if _, exists := pass.ConstantShaderValues[k]; !exists {
					utils.Debug("Effect: Applying default for %s = %v", k, v)
					pass.ConstantShaderValues[k] = v
				}
			}
		}

		loadedPass := SetupPass(shader, shaderName, pass.ConstantShaderValues, loadedTextures)
		loaded.Passes = append(loaded.Passes, loadedPass)
	}

	return loaded
}

// GetShaderDefaults extracts default uniform values from shader source comments.
func GetShaderDefaults(name string) map[string]interface{} {
	defaults := make(map[string]interface{})

	processFile := func(ext string) {
		path := filepath.Join("tmp/shaders", name+ext)
		// Try asset path if tmp doesn't exist, though usually shaders are in tmp
		if _, err := os.Stat(path); os.IsNotExist(err) {
			path = utils.ResolveAssetPath("shaders/" + name + ext)
		}

		data, err := os.ReadFile(path)
		if err != nil {
			utils.Debug("ShaderDefaults: Failed to read %s", path)
			return
		}

		lines := strings.Split(string(data), "\n")
		for _, line := range lines {
			// Robust parsing for JSON in comments
			if idx := strings.Index(line, "//"); idx != -1 {
				comment := strings.TrimSpace(line[idx+2:])
				if strings.HasPrefix(comment, "{") {
					var meta struct {
						Material string      `json:"material"`
						Default  interface{} `json:"default"`
					}
					if err := json.Unmarshal([]byte(comment), &meta); err == nil {
						if meta.Material != "" && meta.Default != nil {
							defaults[meta.Material] = meta.Default
						}
					}
				}
			}
		}
	}

	processFile(".vert")
	processFile(".frag")

	return defaults
}
