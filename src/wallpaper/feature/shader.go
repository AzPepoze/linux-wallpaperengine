package feature

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"linux-wallpaperengine/src/convert"
	"linux-wallpaperengine/src/utils"
	"linux-wallpaperengine/src/wallpaper"

	rl "github.com/gen2brain/raylib-go/raylib"
)

func ApplyShaderEffects(obj *wallpaper.Object, alpha *float64) {
	for _, effect := range obj.Effects {
		if !effect.Visible.GetBool() {
			continue
		}

		// Handle basic shader-like properties that are stored in passes
		if effect.Name == "opacity" {
			if len(effect.Passes) > 0 {
				*alpha *= effect.Passes[0].ConstantValue
			} else {
				*alpha *= effect.Alpha.GetFloat()
			}
		}
	}
}

func PreprocessShader(source string, combos map[string]int) string {
	var sb strings.Builder
	sb.WriteString("#version 120\n")

	// Inject combos
	for k, v := range combos {
		sb.WriteString(fmt.Sprintf("#define %s %d\n", k, v))
	}

	// GLSL compatibility macros
	sb.WriteString("#define frac fract\n")
	sb.WriteString("#define lerp mix\n")
	sb.WriteString("#define texSample2D texture2D\n")

	// FIX: Swapped multiplication order for Raylib/OpenGL (Matrix * Vector)
	sb.WriteString("#define mul(a, b) ((b) * (a))\n")

	sb.WriteString("#define g_ModelViewProjectionMatrix mvp\n")
	sb.WriteString("#define g_Texture0 texture0\n")
	sb.WriteString("#define a_Position vertexPosition\n")
	sb.WriteString("#define a_TexCoord vertexTexCoord\n")
	sb.WriteString("#define CAST2(x) vec2(x)\n")
	sb.WriteString("#define CAST3(x) vec3(x)\n")
	sb.WriteString("#define CAST4(x) vec4(x)\n")
	sb.WriteString("#define CAST2X2(x) mat2(x)\n")
	sb.WriteString("#define CAST3X3(x) mat3(x)\n")
	sb.WriteString("#define saturate(x) clamp(x, 0.0, 1.0)\n")

	// Inject common.h
	if common, err := os.ReadFile("assets/shaders/common.h"); err == nil {
		sb.WriteString(strings.Trim(string(common), "\ufeff"))
		sb.WriteString("\n")
	}

	// Inject common_perspective.h
	if commonPersp, err := os.ReadFile("assets/shaders/common_perspective.h"); err == nil {
		sb.WriteString(strings.Trim(string(commonPersp), "\ufeff"))
		sb.WriteString("\n")
	}

	// Cleanup
	cleaned := strings.ReplaceAll(source, "#include \"common.h\"", "")
	cleaned = strings.ReplaceAll(cleaned, "#include \"common_perspective.h\"", "")
	cleaned = strings.Trim(cleaned, "\ufeff")

	sb.WriteString(cleaned)
	return sb.String()
}

func LoadShader(name string, combos map[string]int) rl.Shader {
	if name == "" {
		return rl.Shader{}
	}

	name = strings.ReplaceAll(name, "\\", "/")
	utils.Debug("Shader: Preprocessing %s (Combos: %v)", name, combos)

	vertPath := filepath.Join("tmp/shaders", name+".vert")
	fragPath := filepath.Join("tmp/shaders", name+".frag")

	var vSource, fSource string

	// Vertex Shader Loading ---
	if data, err := os.ReadFile(vertPath); err == nil {
		vSource = PreprocessShader(string(data), combos)
	} else {
		utils.Warn("Shader: %s - No vertex source found at %s", name, vertPath)
		vSource = "#version 120\nattribute vec3 a_Position; attribute vec2 a_TexCoord; varying vec4 v_TexCoord; uniform mat4 mvp; void main() { v_TexCoord = a_TexCoord.xyxy; gl_Position = mvp * vec4(a_Position, 1.0); }"
	}

	// Fragment Shader Loading ---
	if data, err := os.ReadFile(fragPath); err == nil {
		fSource = PreprocessShader(string(data), combos)
	} else {
		utils.Warn("Shader: %s - No fragment source found at %s", name, fragPath)
		fSource = "#version 120\nvarying vec4 v_TexCoord; uniform sampler2D g_Texture0; void main() { gl_FragColor = texture2D(g_Texture0, v_TexCoord.xy); }"
	}

	if vSource == "" && fSource == "" {
		utils.Warn("Shader: %s - Both vertex and fragment sources are empty", name)
		return rl.Shader{}
	}

	shader := rl.LoadShaderFromMemory(vSource, fSource)
	if shader.ID == 0 {
		utils.Error("Shader: %s - Failed to compile from memory", name)
	} else {
		utils.Info("Shader: %s - Loaded successfully (ID: %d)", name, shader.ID)
	}

	return shader
}

func LoadMaterial(path string) (*wallpaper.MaterialJSON, error) {
	fullPath := filepath.Join("tmp", path)
	data, err := os.ReadFile(fullPath)
	if err != nil {
		return nil, err
	}

	var material wallpaper.MaterialJSON
	if err := json.Unmarshal(data, &material); err != nil {
		return nil, err
	}
	return &material, nil
}

func LoadMockShader(mockName string) rl.Shader {
	vSource := "#version 120\nattribute vec3 a_Position; attribute vec2 a_TexCoord; varying vec4 v_TexCoord; uniform mat4 mvp; void main() { v_TexCoord = a_TexCoord.xyxy; gl_Position = mvp * vec4(a_Position, 1.0); }"

	var fSource string
	path := filepath.Join("assets/shaders", "mock_"+mockName+".frag")
	if data, err := os.ReadFile(path); err == nil {
		fSource = string(data)
	} else {
		utils.Warn("Shader: Mock %s not found at %s, using fallback", mockName, path)
		fSource = "#version 120\nvarying vec4 v_TexCoord; uniform sampler2D g_Texture0; void main() { gl_FragColor = texture2D(g_Texture0, v_TexCoord.xy); }"
	}

	shader := rl.LoadShaderFromMemory(vSource, fSource)
	if shader.ID != 0 {
		utils.Info("Shader: Loaded mock %s successfully (ID: %d)", mockName, shader.ID)
	}
	return shader
}

func LoadEffect(effectConfig *wallpaper.Effect) wallpaper.LoadedEffect {
	// DEBUG: Skip loading if not depthparallax
	// if !strings.Contains(effectConfig.File, "depthparallax") {
	// 	return wallpaper.LoadedEffect{Config: effectConfig}
	// }

	loaded := wallpaper.LoadedEffect{
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
		loadedPass := wallpaper.LoadedPass{}

		var basePass *wallpaper.EffectPass
		if i < len(fullEffect.Passes) {
			basePass = &fullEffect.Passes[i]
		}

		// Property Merging (Scene Instance overrides Base Definition) ---
		// Merge ConstantShaderValues
		if basePass != nil && basePass.ConstantShaderValues != nil {
			if pass.ConstantShaderValues == nil {
				pass.ConstantShaderValues = make(wallpaper.ConstantShaderValues)
			}
			for k, v := range basePass.ConstantShaderValues {
				if _, exists := pass.ConstantShaderValues[k]; !exists {
					pass.ConstantShaderValues[k] = v
				}
			}
		}

		// DEBUG: Force sens to 0.02 and scale to "0.2 0.2"
		if pass.ConstantShaderValues != nil {
			if _, ok := pass.ConstantShaderValues["sens"]; ok {
				pass.ConstantShaderValues["sens"] = 0.02
				utils.Info("Effect: Debug override 'sens' to 0.02")
			}
			if _, ok := pass.ConstantShaderValues["scale"]; ok {
				pass.ConstantShaderValues["scale"] = "0.08 0.08"
				utils.Info("Effect: Debug override 'scale' to '0.08 0.08'")
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

		var mat *wallpaper.MaterialJSON
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
							pass.ConstantShaderValues = make(wallpaper.ConstantShaderValues)
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
		utils.Debug("Effect: [Pass %d] Final texture names: %v", i, finalTexNames)
		for tIdx, texName := range finalTexNames {
			if texName == nil || *texName == "" {
				utils.Debug("Effect: [Pass %d, Tex %d] Skipping empty texture name", i, tIdx)
				loadedPass.Textures = append(loadedPass.Textures, nil)
				continue
			}
			tPath := utils.FindTextureFile(*texName)
			if tPath != "" {
				utils.Debug("Effect: [Pass %d, Tex %d] Loading texture '%s' from '%s'", i, tIdx, *texName, tPath)
				if img, err := convert.LoadTextureNative(tPath); err == nil {
					utils.Debug("Effect: [Pass %d, Tex %d] Loaded SUCCESS", i, tIdx)
					loadedPass.Textures = append(loadedPass.Textures, img)
				} else {
					utils.Warn("Effect: [Pass %d, Tex %d] Loaded FAILED: %v", i, tIdx, err)
					loadedPass.Textures = append(loadedPass.Textures, nil)
				}
			} else {
				utils.Warn("Effect: [Pass %d, Tex %d] Texture path NOT FOUND for '%s'", i, tIdx, *texName)
				loadedPass.Textures = append(loadedPass.Textures, nil)
			}
		}
		utils.Debug("Effect: [Pass %d] Loaded %d textures", i, len(loadedPass.Textures))

		// Copy constants to the loaded pass
		loadedPass.Constants = pass.ConstantShaderValues

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
		}

		// Generic Texture Slot Fixer: Shift leading nils if the non-nil textures fall outside the shader's uniforms
		if shader.ID != 0 {
			maxSlotIdx := -1
			for n := 1; n < 8; n++ {
				loc := rl.GetShaderLocation(shader, fmt.Sprintf("g_Texture%d", n))
				if loc != -1 {
					maxSlotIdx = n - 1
				}
			}

			if maxSlotIdx >= 0 {
				lastNonNilIdx := -1
				for j := len(loadedPass.Textures) - 1; j >= 0; j-- {
					if loadedPass.Textures[j] != nil {
						lastNonNilIdx = j
						break
					}
				}

				if lastNonNilIdx > maxSlotIdx {
					shift := lastNonNilIdx - maxSlotIdx
					canShift := true
					for s := 0; s < shift; s++ {
						if s >= len(loadedPass.Textures) || loadedPass.Textures[s] != nil {
							canShift = false
							break
						}
					}

					if canShift {
						utils.Info("Effect: Auto-fixing slots for %s (shifting %d positions to align with max slot %d)", shaderName, shift, maxSlotIdx)
						loadedPass.Textures = loadedPass.Textures[shift:]
					}
				}
			}
		}

		loaded.Passes = append(loaded.Passes, loadedPass)
		loaded.Shaders = append(loaded.Shaders, shader)
	}

	return loaded
}

func ExtractTextureFromJSONPath(fullPath string) (string, error) {
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
		if name, err := ExtractTextureFromJSONPath(p); err == nil {
			return name, nil
		}

		// Fallback to searching
		searchPaths := []string{
			filepath.Join("tmp", model.Material),
			filepath.Join("assets", model.Material),
		}
		for _, p := range searchPaths {
			if name, err := ExtractTextureFromJSONPath(p); err == nil {
				return name, nil
			}
		}
	}

	return "", fmt.Errorf("no textures found in %s", fullPath)
}
