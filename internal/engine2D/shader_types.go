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

type LoadedEffect struct {
	Config   *wallpaper.Effect
	Shaders  []rl.Shader
	Passes   []LoadedPass
	ShowMask bool
}

type LoadedPass struct {
	Textures  []*rl.Texture2D
	Constants ConstantShaderValues
}
