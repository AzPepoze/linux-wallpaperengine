package shader

import (
	"fmt"
	"strconv"
	"strings"

	rl "github.com/gen2brain/raylib-go/raylib"
)

// ResolveShaderLocations queries a shader for all uniform locations needed for rendering.
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

// SetupPass initializes a shader pass with resolved locations and precomputed uniforms.
func SetupPass(shader rl.Shader, shaderName string, constants ConstantShaderValues, textures []*rl.Texture2D) LoadedPass {
	pass := LoadedPass{
		ShaderName: shaderName,
		Shader:     shader,
		Textures:   textures,
		Parameters: ResolveShaderLocations(shader),
		Uniforms:   make([]PrecomputedUniform, 0),
		Constants:  constants,
	}

	UpdatePassUniforms(&pass)

	return pass
}

// UpdatePassUniforms rebuilds the precomputed uniforms list from the Constants map.
// Call this after modifying pass.Constants at runtime.
func UpdatePassUniforms(pass *LoadedPass) {
	pass.Uniforms = make([]PrecomputedUniform, 0)

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
