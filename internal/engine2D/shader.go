package engine2D

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"linux-wallpaperengine/internal/convert"
	"linux-wallpaperengine/internal/utils"
	"linux-wallpaperengine/internal/wallpaper"

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

func PreprocessShader(source string, combos map[string]int, name string) string {
	var sb strings.Builder
	sb.WriteString("#version 120\n")

	// Inject combos
	for k, v := range combos {
		sb.WriteString(fmt.Sprintf("#define %s %d\n", k, v))
	}
	if _, exists := combos["BLENDMODE"]; !exists {
		sb.WriteString("#define BLENDMODE 0\n")
	}

	// GLSL compatibility macros
	sb.WriteString("#define frac fract\n")
	sb.WriteString("#define lerp mix\n")
	sb.WriteString("#define texSample2D texture2D\n")
	sb.WriteString("#define atan2(y, x) atan(y, x)\n")

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

	// FIX: Waterripple speed is too slow due to squaring. Remove one factor.
	if strings.Contains(name, "waterripple") {
		source = strings.ReplaceAll(source, "g_Time * g_AnimationSpeed * g_AnimationSpeed", "g_Time * g_AnimationSpeed")
		utils.Debug("Shader: Applied waterripple speed fix for %s", name)
	}

	// FIX: Mask Y is inverted for most effects compared to depthparallax
	// Check for standard mask calc pattern
	if !strings.Contains(name, "depthparallax") {
		// Pattern 1
		if strings.Contains(source, "v_TexCoord.y * g_Texture2Resolution.w / g_Texture2Resolution.y") {
			source = strings.ReplaceAll(source,
				"v_TexCoord.y * g_Texture2Resolution.w / g_Texture2Resolution.y",
				"(1.0 - v_TexCoord.y) * g_Texture2Resolution.w / g_Texture2Resolution.y")
			utils.Debug("Shader: Applied mask flip fix (Pattern 1) for %s", name)
		}

		// Pattern 2: waterwaves uses v_TexCoord.w
		if strings.Contains(source, "v_TexCoord.w *= g_Texture1Resolution.w / g_Texture1Resolution.y;") {
			source = strings.ReplaceAll(source,
				"v_TexCoord.w *= g_Texture1Resolution.w / g_Texture1Resolution.y;",
				"v_TexCoord.w = (1.0 - v_TexCoord.w) * (g_Texture1Resolution.w / g_Texture1Resolution.y);")
			utils.Debug("Shader: Applied mask flip fix (Pattern 2 - Texture1) for %s", name)
		}

		// Pattern 2b: waterwaves TIMEOFFSET or others using Texture2
		if strings.Contains(source, "v_TexCoord.w *= g_Texture2Resolution.w / g_Texture2Resolution.y;") {
			source = strings.ReplaceAll(source,
				"v_TexCoord.w *= g_Texture2Resolution.w / g_Texture2Resolution.y;",
				"v_TexCoord.w = (1.0 - v_TexCoord.w) * (g_Texture2Resolution.w / g_Texture2Resolution.y);")
			utils.Debug("Shader: Applied mask flip fix (Pattern 2 - Texture2) for %s", name)
		}
	}

	// Pre-process includes to avoid duplicates
	included := make(map[string]bool)

	// Check if common.h is already in the source
	if !strings.Contains(source, "#include \"common.h\"") {
		commonPath := utils.ResolveAssetPath("shaders/common.h")
		if content, err := os.ReadFile(commonPath); err == nil {
			sb.WriteString(strings.Trim(string(content), "\ufeff"))
			sb.WriteString("\n")
			included["common.h"] = true
		}
	}

	lines := strings.Split(source, "\n")
	for _, line := range lines {
		trimmed := strings.TrimSpace(line)
		if strings.HasPrefix(trimmed, "#include \"") && strings.HasSuffix(trimmed, "\"") {
			includeFile := strings.Trim(trimmed[len("#include \""):len(trimmed)-1], " ")
			if included[includeFile] {
				continue
			}
			// Try to find in assets/shaders
			includePath := utils.ResolveAssetPath(filepath.Join("shaders", includeFile))
			if content, err := os.ReadFile(includePath); err == nil {
				sb.WriteString(strings.Trim(string(content), "\ufeff"))
				sb.WriteString("\n")
				included[includeFile] = true
				continue
			}
			utils.Warn("Shader: Could not resolve include: %s", includeFile)
			continue
		}
		sb.WriteString(line)
		sb.WriteString("\n")
	}

	return sb.String()
}

func LoadShader(name string, combos map[string]int) rl.Shader {
	if name == "" {
		return rl.Shader{}
	}

	name = strings.ReplaceAll(name, "\\", "/")

	vertPath := filepath.Join("tmp/shaders", name+".vert")
	fragPath := filepath.Join("tmp/shaders", name+".frag")

	// Extract default combos from frag shader comments if not already present
	if data, err := os.ReadFile(fragPath); err == nil {
		lines := strings.Split(string(data), "\n")
		for _, line := range lines {
			if strings.HasPrefix(line, "// [COMBO]") {
				jsonPart := line[len("// [COMBO] "):]
				var comboInfo struct {
					Combo   string `json:"combo"`
					Default int    `json:"default"`
				}
				if err := json.Unmarshal([]byte(jsonPart), &comboInfo); err == nil {
					if _, exists := combos[comboInfo.Combo]; !exists {
						utils.Debug("Shader: Setting default combo %s = %d", comboInfo.Combo, comboInfo.Default)
						combos[comboInfo.Combo] = comboInfo.Default
					}
				}
			}
		}
	}

	utils.Debug("Shader: Preprocessing %s (Combos: %v)", name, combos)

	var vSource, fSource string

	// Vertex Shader Loading ---
	if data, err := os.ReadFile(vertPath); err == nil {
		vSource = PreprocessShader(string(data), combos, name)
	} else {
		utils.Warn("Shader: %s - No vertex source found at %s", name, vertPath)
		vSource = "#version 120\nattribute vec3 a_Position; attribute vec2 a_TexCoord; varying vec4 v_TexCoord; uniform mat4 mvp; void main() { v_TexCoord = a_TexCoord.xyxy; gl_Position = mvp * vec4(a_Position, 1.0); }"
	}

	// Fragment Shader Loading ---
	if data, err := os.ReadFile(fragPath); err == nil {
		fSource = PreprocessShader(string(data), combos, name)
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

func LoadMaterial(path string) (*MaterialJSON, error) {
	fullPath := filepath.Join("tmp", path)
	data, err := os.ReadFile(fullPath)
	if err != nil {
		return nil, err
	}

	var material MaterialJSON
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

func ResolveShaderLocations(shader rl.Shader) ShaderParameters {
	parameters := ShaderParameters{
		Time:      rl.GetShaderLocation(shader, "g_Time"),
		Pointer:   rl.GetShaderLocation(shader, "g_PointerPosition"),
		Parallax:  rl.GetShaderLocation(shader, "g_ParallaxPosition"),
		TexelSize: rl.GetShaderLocation(shader, "g_TexelSize"),
		MVP:       rl.GetShaderLocation(shader, "g_ModelViewProjectionMatrix"),
		Proj:      rl.GetShaderLocation(shader, "g_EffectTextureProjectionMatrix"),
		ProjInv:   rl.GetShaderLocation(shader, "g_EffectTextureProjectionMatrixInverse"),
		ModelInv:  rl.GetShaderLocation(shader, "g_EffectModelViewProjectionMatrixInverse"),
	}

	if parameters.Pointer == -1 {
		parameters.Pointer = rl.GetShaderLocation(shader, "g_Pointer")
	}

	for i := 0; i < 8; i++ {
		parameters.TextureResolutions[i] = rl.GetShaderLocation(shader, fmt.Sprintf("g_Texture%dResolution", i))
		parameters.TextureSamplers[i] = rl.GetShaderLocation(shader, fmt.Sprintf("g_Texture%d", i))

		// Fallback for Texture0 -> texture0 (Raylib default)
		if i == 0 && parameters.TextureSamplers[i] == -1 {
			parameters.TextureSamplers[i] = rl.GetShaderLocation(shader, "texture0")
		}
	}

	return parameters
}

func SetupPass(shader rl.Shader, shaderName string, constants ConstantShaderValues, textures []*rl.Texture2D) LoadedPass {
	pass := LoadedPass{
		ShaderName: shaderName,
		Shader:     shader,
		Textures:   textures,
		Parameters: ResolveShaderLocations(shader),
		Uniforms:   make([]PrecomputedUniform, 0),
		Constants:  constants, // Store the source map
	}

	UpdatePassUniforms(&pass)

	return pass
}

// UpdatePassUniforms rebuilds the precomputed uniforms list from the Constants map.
// Call this after modifying pass.Constants at runtime.
func UpdatePassUniforms(pass *LoadedPass) {
	pass.Uniforms = make([]PrecomputedUniform, 0)

	// Create a temporary map to store uniforms to avoid duplicates if necessary,
	// but simple append is fine since we clear the list first.

	for k, v := range pass.Constants {
		// Try variations of the key to find the location
		names := []string{
			"g_" + k,
			k,
			"g_" + strings.Title(k),
		}
		// Common mappings
		if k == "ripplestrength" {
			names = append(names, "g_Strength")
		}
		if k == "animationspeed" {
			names = append(names, "g_AnimationSpeed")
		}
		if k == "sens" || k == "sensitivity" {
			names = append(names, "g_Sensitivity", "sensitivity")
		}
		if k == "center" {
			names = append(names, "g_Center", "center")
		}
		if k == "scale" {
			names = append(names, "g_Scale", "scale")
		}

		var loc int32 = -1
		for _, name := range names {
			loc = rl.GetShaderLocation(pass.Shader, name)
			if loc != -1 {
				break
			}
		}

		if loc != -1 {
			// Determine value type and convert to []float32
			var floats []float32
			var uType rl.ShaderUniformDataType

			switch val := v.(type) {
			case float64:
				floats = []float32{float32(val)}
				uType = rl.ShaderUniformFloat
			case string:
				parts := strings.Fields(val)
				if len(parts) == 1 {
					if f, err := strconv.ParseFloat(parts[0], 64); err == nil {
						floats = []float32{float32(f)}
						uType = rl.ShaderUniformFloat
					}
				} else if len(parts) == 2 {
					f1, _ := strconv.ParseFloat(parts[0], 64)
					f2, _ := strconv.ParseFloat(parts[1], 64)
					floats = []float32{float32(f1), float32(f2)}
					uType = rl.ShaderUniformVec2
				} else if len(parts) == 3 {
					f1, _ := strconv.ParseFloat(parts[0], 64)
					f2, _ := strconv.ParseFloat(parts[1], 64)
					f3, _ := strconv.ParseFloat(parts[2], 64)
					floats = []float32{float32(f1), float32(f2), float32(f3)}
					uType = rl.ShaderUniformVec3
				}
			case map[string]interface{}:
				// Handle complex objects like {"value": ...}
				if innerVal, ok := val["value"]; ok {
					switch iv := innerVal.(type) {
					case float64:
						floats = []float32{float32(iv)}
						uType = rl.ShaderUniformFloat
					}
				}
			}

			if len(floats) > 0 {
				// Special fix for depthparallax scale
				if k == "scale" && strings.Contains(pass.ShaderName, "depthparallax") {
					for i := range floats {
						floats[i] /= 40.0
					}
				}

				pass.Uniforms = append(pass.Uniforms, PrecomputedUniform{
					Location: loc,
					Type:     uType,
					Values:   floats,
				})
			}
		}
	}
}

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

var BlackTexture *rl.Texture2D

func InitDefaults() {
	if BlackTexture == nil {
		img := rl.GenImageColor(1, 1, rl.Black)
		tex := rl.LoadTextureFromImage(img)
		rl.SetTextureWrap(tex, rl.TextureWrapRepeat)
		rl.UnloadImage(img)
		BlackTexture = &tex
	}
}

// Helper to reverse map location to name for debugging
func getUniformName(loc int32, pass *LoadedPass) string {
	for k := range pass.Constants {
		// This is a rough guess, as we don't store the exact mapping.
		// But valid for most params.
		if strings.Contains(strings.ToLower(k), "speed") {
			return k
		}
	}
	return "?"
}

func ApplyPass(pass *LoadedPass, state GlobalState, mainTexture *rl.Texture2D) {
	shader := pass.Shader
	parameters := &pass.Parameters

	// 1. Set Standard Uniforms
	if parameters.Time != -1 {
		rl.SetShaderValue(shader, parameters.Time, []float32{float32(state.Time)}, rl.ShaderUniformFloat)
	}
	if parameters.Pointer != -1 {
		rl.SetShaderValue(shader, parameters.Pointer, []float32{float32(state.MouseX*0.5 + 0.5), float32(state.MouseY*0.5 + 0.5)}, rl.ShaderUniformVec2)
	}
	if parameters.Parallax != -1 {
		rl.SetShaderValue(shader, parameters.Parallax, []float32{float32(state.ParallaxX*0.5 + 0.5), float32(state.ParallaxY*0.5 + 0.5)}, rl.ShaderUniformVec2)
	}

	// Matrices (Identity for now)
	identity := rl.MatrixIdentity()
	if parameters.MVP != -1 {
		rl.SetShaderValueMatrix(shader, parameters.MVP, identity)
	}
	if parameters.Proj != -1 {
		rl.SetShaderValueMatrix(shader, parameters.Proj, identity)
	}
	if parameters.ProjInv != -1 {
		rl.SetShaderValueMatrix(shader, parameters.ProjInv, identity)
	}
	if parameters.ModelInv != -1 {
		rl.SetShaderValueMatrix(shader, parameters.ModelInv, identity)
	}

	// 2. Set Precomputed Constants
	for _, uniform := range pass.Uniforms {
		rl.SetShaderValue(shader, uniform.Location, uniform.Values, uniform.Type)
	}

	// 3. Bind Textures and Resolutions
	// We iterate up to 8 to cover all potential slots, as g_Texture0Resolution might be needed even if tex is nil
	for i := 0; i < 8; i++ {
		var texture *rl.Texture2D

		// Determine which texture to use for this slot
		if i < len(pass.Textures) {
			texture = pass.Textures[i]
		}

		// If it's slot 0 and no override texture is provided, use the main input texture
		if i == 0 && mainTexture != nil {
			texture = mainTexture
		}

		// Fallback for missing textures in slots > 0 (e.g. Depth Maps)
		if texture == nil && i > 0 && parameters.TextureSamplers[i] != -1 {
			if BlackTexture == nil {
				InitDefaults() // Lazy init if needed
			}
			texture = BlackTexture
		}

		if texture == nil {
			continue
		}

		// Set Sampler Unit (e.g. g_Texture0 = 0)
		if parameters.TextureSamplers[i] != -1 {
			rl.SetShaderValue(shader, parameters.TextureSamplers[i], []float32{float32(i)}, rl.ShaderUniformSampler2d)
		}

		// Set Resolution
		if parameters.TextureResolutions[i] != -1 {
			w, h := float32(texture.Width), float32(texture.Height)
			rl.SetShaderValue(shader, parameters.TextureResolutions[i], []float32{w, h, w, h}, rl.ShaderUniformVec4)

			// Set TexelSize if this is the main texture (slot 0)
			if i == 0 && parameters.TexelSize != -1 {
				rl.SetShaderValue(shader, parameters.TexelSize, []float32{1.0 / w, 1.0 / h}, rl.ShaderUniformVec2)
			}
		}

		if i >= 0 {
			rl.SetShaderValueTexture(shader, rl.GetShaderLocation(shader, fmt.Sprintf("g_Texture%d", i)), *texture)
		}
	}
}
