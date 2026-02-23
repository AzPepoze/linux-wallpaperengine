package shader

import (
	rl "github.com/gen2brain/raylib-go/raylib"

	"linux-wallpaperengine/internal/wallpaper"
)

// ApplyShaderEffects applies effect properties to shader rendering,
// particularly opacity effects.
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

// ApplyPass applies a shader pass with all uniforms, textures, and matrices.
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

		// If it's slot 1 and no override texture is provided, use the background texture
		if i == 1 && texture == nil && state.Background != nil {
			texture = state.Background
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

		// Select and bind texture to appropriate unit
		if parameters.TextureSamplers[i] != -1 {
			// Set Sampler Unit (e.g. g_Texture0 = 0)
			rl.SetShaderValue(shader, parameters.TextureSamplers[i], []float32{float32(i)}, rl.ShaderUniformSampler2d)
			// Bind texture
			rl.SetShaderValueTexture(shader, parameters.TextureSamplers[i], *texture)
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
	}
}
