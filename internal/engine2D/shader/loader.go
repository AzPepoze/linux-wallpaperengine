package shader

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"linux-wallpaperengine/internal/utils"

	rl "github.com/gen2brain/raylib-go/raylib"
)

// PreprocessShader handles shader preprocessing, including:
// - Injecting combo defines
// - Adding GLSL compatibility macros
// - Processing includes
// - Applying mask fixes
func PreprocessShader(source string, combos map[string]int, name string) string {
	var sb strings.Builder
	sb.WriteString("#version 120\n")

	for k, v := range combos {
		sb.WriteString(fmt.Sprintf("#define %s %d\n", k, v))
	}
	if _, exists := combos["BLENDMODE"]; !exists {
		sb.WriteString("#define BLENDMODE 0\n")
	}

	sb.WriteString("#define frac fract\n")
	sb.WriteString("#define lerp mix\n")
	sb.WriteString("#define texSample2D texture2D\n")
	sb.WriteString("#define atan2(y, x) atan(y, x)\n")

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

	if !strings.Contains(name, "depthparallax") {
		if strings.Contains(source, "v_TexCoord.y * g_Texture2Resolution.w / g_Texture2Resolution.y") {
			source = strings.ReplaceAll(source,
				"v_TexCoord.y * g_Texture2Resolution.w / g_Texture2Resolution.y",
				"(1.0 - v_TexCoord.y) * g_Texture2Resolution.w / g_Texture2Resolution.y")
			utils.Debug("Shader: Applied mask flip fix (Pattern 1) for %s", name)
		}

		if strings.Contains(source, "v_TexCoord.w *= g_Texture1Resolution.w / g_Texture1Resolution.y;") {
			source = strings.ReplaceAll(source,
				"v_TexCoord.w *= g_Texture1Resolution.w / g_Texture1Resolution.y;",
				"v_TexCoord.w = (1.0 - v_TexCoord.w) * (g_Texture1Resolution.w / g_Texture1Resolution.y);")
			utils.Debug("Shader: Applied mask flip fix (Pattern 2 - Texture1) for %s", name)
		}

		if strings.Contains(source, "v_TexCoord.w *= g_Texture2Resolution.w / g_Texture2Resolution.y;") {
			source = strings.ReplaceAll(source,
				"v_TexCoord.w *= g_Texture2Resolution.w / g_Texture2Resolution.y;",
				"v_TexCoord.w = (1.0 - v_TexCoord.w) * (g_Texture2Resolution.w / g_Texture2Resolution.y);")
			utils.Debug("Shader: Applied mask flip fix (Pattern 2 - Texture2) for %s", name)
		}
	}

	included := make(map[string]bool)

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

// LoadShader loads a shader from file with combo support and error recovery.
// Returns an empty shader if loading fails.
func LoadShader(name string, combos map[string]int) rl.Shader {
	if name == "" {
		return rl.Shader{}
	}

	name = strings.ReplaceAll(name, "\\", "/")

	vertPath := filepath.Join("tmp/shaders", name+".vert")
	fragPath := filepath.Join("tmp/shaders", name+".frag")

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

	if data, err := os.ReadFile(vertPath); err == nil {
		vSource = PreprocessShader(string(data), combos, name)
	} else {
		utils.Warn("Shader: %s - No vertex source found at %s", name, vertPath)
		vSource = "#version 120\nattribute vec3 a_Position; attribute vec2 a_TexCoord; varying vec4 v_TexCoord; uniform mat4 mvp; void main() { v_TexCoord = a_TexCoord.xyxy; gl_Position = mvp * vec4(a_Position, 1.0); }"
	}

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

	var shader rl.Shader
	func() {
		defer func() {
			if r := recover(); r != nil {
				utils.Error("Shader: %s - Compilation panic (skipping): %v", name, r)
				shader = rl.Shader{}
			}
		}()
		shader = rl.LoadShaderFromMemory(vSource, fSource)
	}()

	if shader.ID == 0 {
		utils.Warn("Shader: %s - Failed to compile from memory (returning empty shader)", name)
	} else {
		utils.Info("Shader: %s - Loaded successfully (ID: %d)", name, shader.ID)
	}

	return shader
}

// LoadMockShader loads a mock shader from assets or uses a fallback.
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

	var shader rl.Shader
	func() {
		defer func() {
			if r := recover(); r != nil {
				utils.Error("Shader: Mock %s - Compilation panic (skipping): %v", mockName, r)
				shader = rl.Shader{}
			}
		}()
		shader = rl.LoadShaderFromMemory(vSource, fSource)
	}()

	if shader.ID != 0 {
		utils.Info("Shader: Loaded mock %s successfully (ID: %d)", mockName, shader.ID)
	}
	return shader
}

// LoadMaterial loads a material JSON configuration from file.
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
