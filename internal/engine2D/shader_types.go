package engine2D

import (
	"strings"

	"linux-wallpaperengine/internal/wallpaper"

	rl "github.com/gen2brain/raylib-go/raylib"
)

type ConstantShaderValues map[string]interface{}

func (c ConstantShaderValues) GetFloat(key string) float64 {
	val, ok := c[key]
	if !ok {
		// Try lowercase
		val, ok = c[strings.ToLower(key)]
		if !ok {
			return 0
		}
	}

	switch v := val.(type) {
	case float64:
		return v
	case map[string]interface{}:
		if val, ok := v["value"].(float64); ok {
			return val
		}
	}
	return 0
}

type MaterialJSON struct {
	Passes []struct {
		Textures             []string             `json:"textures"`
		Blending             string               `json:"blending"`
		CullMode             string               `json:"cullmode"`
		DepthTest            string               `json:"depthtest"`
		DepthWrite           string               `json:"depthwrite"`
		Shader               string               `json:"shader"`
		Combos               map[string]int       `json:"combos"`
		ConstantShaderValues ConstantShaderValues `json:"constantshadervalues"`
	} `json:"passes"`
}

type PrecomputedUniform struct {
	Location int32
	Type     rl.ShaderUniformDataType
	Values   []float32
}

type ShaderParameters struct {
	Time               int32
	Pointer            int32
	Parallax           int32
	TexelSize          int32
	TextureResolutions [8]int32
	TextureSamplers    [8]int32

	// Matrices
	MVP      int32 // g_ModelViewProjectionMatrix
	Proj     int32 // g_EffectTextureProjectionMatrix
	ProjInv  int32 // g_EffectTextureProjectionMatrixInverse
	ModelInv int32 // g_EffectModelViewProjectionMatrixInverse
}

type GlobalState struct {
	Time           float64
	MouseX, MouseY float64
	ParallaxX      float64
	ParallaxY      float64
}

type LoadedPass struct {
	ShaderName string
	Shader     rl.Shader
	Textures   []*rl.Texture2D
	Uniforms   []PrecomputedUniform
	Parameters ShaderParameters
	Constants  ConstantShaderValues
}

type LoadedEffect struct {
	Config   *wallpaper.Effect
	Passes   []LoadedPass
	ShowMask bool
}
